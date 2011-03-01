/*
 * Copyright (C) 2008-2009 Patrick Ohly <patrick.ohly@gmx.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) version 3.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301  USA
 */

#ifndef ICALSTRDUP_H
#define ICALSTRDUP_H

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#ifdef ENABLE_ICAL

#ifndef HANDLE_LIBICAL_MEMORY
# define HANDLE_LIBICAL_MEMORY 1
#endif
#include <libical/ical.h>

#include <syncevo/eds_abi_wrapper.h>

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#if !defined(LIBICAL_MEMFIXES) || defined(EVOLUTION_COMPATIBILITY)
/**
 * The patch in http://bugzilla.gnome.org/show_bug.cgi?id=516408
 * changes the ownership of strings returned by some libical and libecal
 * functions: previously, the memory was owned by the library.
 * After the patch the caller owns the copied string and must free it.
 *
 * The upstream SourceForge libical has incorporated the patch, but
 * without changing the semantic of the existing calls. Instead they
 * added _r variants which return memory that the caller must free.
 * As soon as Evolution switches to upstream libical (planned for 2.25),
 * it probably will have to bump the libecal version because the API
 * is reverted so that binaries which free strings will crash.
 * When EVOLUTION_COMPATIBILITY is defined, SyncEvolution deals with
 * this by always checking at runtime what the memory handling is.
 *
 * This utility function ensures that the caller *always* owns the
 * returned string. When compiled against a current Evolution
 * Dataserver, the function becomes a NOP macro, unless compatibility
 * mode is on (in which case the current binary might later run with
 * an older Evolution release!). If not a NOP macro, then the function
 * duplicates the string; it handles NULL by passing it through.
 *
 * When compiled against an old Evolution Dataserver, then a runtime
 * check can be enabled to to determine whether the string needs to be
 * duplicated. If uncertain, it always duplicates the string. If the
 * check fails, then memory is leaked, which also happens when running
 * programs which do not know about the patch.
 *
 * To enable the runtime check, compile the .c file with -D_GNU_SOURCE
 * and -DHAVE_DLFCN_H. If HAVE_CONFIG_H is set, then config.h is included
 * and can be set to set some of these defines. If enabled, then link with
 * -ldl.
 *
 * ical_strdup() must be wrapped around the following functions:
 * - icalreqstattype_as_string
 * - icalproperty_as_ical_string
 * - icalproperty_get_parameter_as_string
 * - icalproperty_get_value_as_string
 * - icallangbind_property_eval_string
 * - icalperiodtype_as_ical_string
 * - icaltime_as_ical_string
 * - icalvalue_as_ical_string
 * - icalcomponent_as_ical_string
 * - e_cal_component_get_recurid_as_string
 *
 * @param x    result of one of the functions above
 * @return string which has to be freed by caller, may be NULL if x was NULL
 */
extern char *ical_strdup(const char *x);
#else
# define ical_strdup(_x) (_x)
#endif

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* ENABLE_ICAL */
#endif /* ICALSTRDUP_H */
