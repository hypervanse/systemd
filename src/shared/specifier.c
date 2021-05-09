/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/utsname.h>

#include "sd-id128.h"

#include "alloc-util.h"
#include "architecture.h"
#include "format-util.h"
#include "fs-util.h"
#include "hostname-util.h"
#include "macro.h"
#include "os-util.h"
#include "specifier.h"
#include "string-util.h"
#include "strv.h"
#include "user-util.h"

/*
 * Generic infrastructure for replacing %x style specifiers in
 * strings. Will call a callback for each replacement.
 */

/* Any ASCII character or digit: our pool of potential specifiers,
 * and "%" used for escaping. */
#define POSSIBLE_SPECIFIERS ALPHANUMERICAL "%"

int specifier_printf(const char *text, const Specifier table[], const void *userdata, char **ret) {
        size_t l, allocated = 0;
        _cleanup_free_ char *result = NULL;
        char *t;
        const char *f;
        bool percent = false;
        int r;

        assert(text);
        assert(table);

        l = strlen(text);
        if (!GREEDY_REALLOC(result, allocated, l + 1))
                return -ENOMEM;
        t = result;

        for (f = text; *f; f++, l--)
                if (percent) {
                        if (*f == '%')
                                *(t++) = '%';
                        else {
                                const Specifier *i;

                                for (i = table; i->specifier; i++)
                                        if (i->specifier == *f)
                                                break;

                                if (i->lookup) {
                                        _cleanup_free_ char *w = NULL;
                                        size_t k, j;

                                        r = i->lookup(i->specifier, i->data, userdata, &w);
                                        if (r < 0)
                                                return r;

                                        j = t - result;
                                        k = strlen(w);

                                        if (!GREEDY_REALLOC(result, allocated, j + k + l + 1))
                                                return -ENOMEM;
                                        memcpy(result + j, w, k);
                                        t = result + j + k;
                                } else if (strchr(POSSIBLE_SPECIFIERS, *f))
                                        /* Oops, an unknown specifier. */
                                        return -EBADSLT;
                                else {
                                        *(t++) = '%';
                                        *(t++) = *f;
                                }
                        }

                        percent = false;
                } else if (*f == '%')
                        percent = true;
                else
                        *(t++) = *f;

        /* If string ended with a stray %, also end with % */
        if (percent)
                *(t++) = '%';
        *(t++) = 0;

        /* Try to deallocate unused bytes, but don't sweat it too much */
        if ((size_t)(t - result) < allocated) {
                t = realloc(result, t - result);
                if (t)
                        result = t;
        }

        *ret = TAKE_PTR(result);
        return 0;
}

/* Generic handler for simple string replacements */

int specifier_string(char specifier, const void *data, const void *userdata, char **ret) {
        char *n;

        n = strdup(strempty(data));
        if (!n)
                return -ENOMEM;

        *ret = n;
        return 0;
}

int specifier_machine_id(char specifier, const void *data, const void *userdata, char **ret) {
        sd_id128_t id;
        char *n;
        int r;

        r = sd_id128_get_machine(&id);
        if (r < 0)
                return r;

        n = new(char, SD_ID128_STRING_MAX);
        if (!n)
                return -ENOMEM;

        *ret = sd_id128_to_string(id, n);
        return 0;
}

int specifier_boot_id(char specifier, const void *data, const void *userdata, char **ret) {
        sd_id128_t id;
        char *n;
        int r;

        r = sd_id128_get_boot(&id);
        if (r < 0)
                return r;

        n = new(char, SD_ID128_STRING_MAX);
        if (!n)
                return -ENOMEM;

        *ret = sd_id128_to_string(id, n);
        return 0;
}

int specifier_host_name(char specifier, const void *data, const void *userdata, char **ret) {
        char *n;

        n = gethostname_malloc();
        if (!n)
                return -ENOMEM;

        *ret = n;
        return 0;
}

int specifier_short_host_name(char specifier, const void *data, const void *userdata, char **ret) {
        char *n;

        n = gethostname_short_malloc();
        if (!n)
                return -ENOMEM;

        *ret = n;
        return 0;
}

int specifier_kernel_release(char specifier, const void *data, const void *userdata, char **ret) {
        struct utsname uts;
        char *n;
        int r;

        r = uname(&uts);
        if (r < 0)
                return -errno;

        n = strdup(uts.release);
        if (!n)
                return -ENOMEM;

        *ret = n;
        return 0;
}

int specifier_architecture(char specifier, const void *data, const void *userdata, char **ret) {
        char *t;

        t = strdup(architecture_to_string(uname_architecture()));
        if (!t)
                return -ENOMEM;

        *ret = t;
        return 0;
}

static int specifier_os_release_common(const char *field, char **ret) {
        char *t = NULL;
        int r;

        r = parse_os_release(NULL, field, &t);
        if (r < 0)
                return r;
        if (!t) {
                /* fields in /etc/os-release might quite possibly be missing, even if everything is entirely
                 * valid otherwise. Let's hence return "" in that case. */
                t = strdup("");
                if (!t)
                        return -ENOMEM;
        }

        *ret = t;
        return 0;
}

int specifier_os_id(char specifier, const void *data, const void *userdata, char **ret) {
        return specifier_os_release_common("ID", ret);
}

int specifier_os_version_id(char specifier, const void *data, const void *userdata, char **ret) {
        return specifier_os_release_common("VERSION_ID", ret);
}

int specifier_os_build_id(char specifier, const void *data, const void *userdata, char **ret) {
        return specifier_os_release_common("BUILD_ID", ret);
}

int specifier_os_variant_id(char specifier, const void *data, const void *userdata, char **ret) {
        return specifier_os_release_common("VARIANT_ID", ret);
}

int specifier_os_image_id(char specifier, const void *data, const void *userdata, char **ret) {
        return specifier_os_release_common("IMAGE_ID", ret);
}

int specifier_os_image_version(char specifier, const void *data, const void *userdata, char **ret) {
        return specifier_os_release_common("IMAGE_VERSION", ret);
}

int specifier_group_name(char specifier, const void *data, const void *userdata, char **ret) {
        char *t;

        t = gid_to_name(getgid());
        if (!t)
                return -ENOMEM;

        *ret = t;
        return 0;
}

int specifier_group_id(char specifier, const void *data, const void *userdata, char **ret) {
        if (asprintf(ret, UID_FMT, getgid()) < 0)
                return -ENOMEM;

        return 0;
}

int specifier_user_name(char specifier, const void *data, const void *userdata, char **ret) {
        char *t;

        /* If we are UID 0 (root), this will not result in NSS, otherwise it might. This is good, as we want to be able
         * to run this in PID 1, where our user ID is 0, but where NSS lookups are not allowed.

         * We don't use getusername_malloc() here, because we don't want to look at $USER, to remain consistent with
         * specifer_user_id() below.
         */

        t = uid_to_name(getuid());
        if (!t)
                return -ENOMEM;

        *ret = t;
        return 0;
}

int specifier_user_id(char specifier, const void *data, const void *userdata, char **ret) {

        if (asprintf(ret, UID_FMT, getuid()) < 0)
                return -ENOMEM;

        return 0;
}

int specifier_user_home(char specifier, const void *data, const void *userdata, char **ret) {

        /* On PID 1 (which runs as root) this will not result in NSS,
         * which is good. See above */

        return get_home_dir(ret);
}

int specifier_user_shell(char specifier, const void *data, const void *userdata, char **ret) {

        /* On PID 1 (which runs as root) this will not result in NSS,
         * which is good. See above */

        return get_shell(ret);
}

int specifier_tmp_dir(char specifier, const void *data, const void *userdata, char **ret) {
        const char *p;
        char *copy;
        int r;

        r = tmp_dir(&p);
        if (r < 0)
                return r;

        copy = strdup(p);
        if (!copy)
                return -ENOMEM;

        *ret = copy;
        return 0;
}

int specifier_var_tmp_dir(char specifier, const void *data, const void *userdata, char **ret) {
        const char *p;
        char *copy;
        int r;

        r = var_tmp_dir(&p);
        if (r < 0)
                return r;

        copy = strdup(p);
        if (!copy)
                return -ENOMEM;

        *ret = copy;
        return 0;
}

int specifier_escape_strv(char **l, char ***ret) {
        char **z, **p, **q;

        assert(ret);

        if (strv_isempty(l)) {
                *ret = NULL;
                return 0;
        }

        z = new(char*, strv_length(l)+1);
        if (!z)
                return -ENOMEM;

        for (p = l, q = z; *p; p++, q++) {

                *q = specifier_escape(*p);
                if (!*q) {
                        strv_free(z);
                        return -ENOMEM;
                }
        }

        *q = NULL;
        *ret = z;

        return 0;
}
