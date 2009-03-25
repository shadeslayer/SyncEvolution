/*
 * Copyright (C) 2008-2009 Patrick Ohly <patrick.ohly@gmx.de>
 */

#include "libical/icalstrdup.h"

#if !defined(LIBICAL_MEMFIXES) || defined(EVOLUTION_COMPATIBILITY)

#if defined(HAVE_CONFIG_H)
# include <config.h>
#endif

#if defined(_GNU_SOURCE) && defined(HAVE_DLFCN_H)
# include <dlfcn.h>
# define LIBICAL_RUNTIME_CHECK
#endif

#include <string.h>

char *ical_strdup(const char *x)
{
#ifdef LIBICAL_RUNTIME_CHECK
    static enum {
        PATCH_UNCHECKED,
        PATCH_FOUND,
        PATCH_NOT_FOUND
    } patch_status;

    if (patch_status == PATCH_UNCHECKED) {
        patch_status = dlsym(RTLD_NEXT, "ical_memfixes") != NULL ?
            PATCH_FOUND : PATCH_NOT_FOUND;
    }

    if (patch_status == PATCH_FOUND) {
        /* patch applied, no need to copy */
        return (char *)x;
    }
#endif

    return x ? strdup(x) : NULL;
}

#endif /* !LIBICAL_MEMFIXES */
