/*
 * Copyright (C) 2008 Patrick Ohly
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY, TITLE, NONINFRINGEMENT or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307  USA
 */

#include "libical/icalstrdup.h"

#ifndef LIBICAL_MEMFIXES

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
