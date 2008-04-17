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

#include "e_cal_check_timezones.h"
#include <libecal/e-cal.h>
#include <string.h>

/**
 * Matches a location to a system timezone definition via a fuzzy
 * search and returns the matching TZID, or NULL if none found.
 *
 * Currently simply strips a suffix introduced by a hyphen,
 * as in "America/Denver-(Standard)".
 */
static const char *e_cal_match_location(const char *location)
{
    icaltimezone *icomp;
    const char *tail;
    size_t len;
    char *buffer;

    icomp = icaltimezone_get_builtin_timezone (location);
    if (icomp) {
        return icaltimezone_get_tzid(icomp);
    }

    /* try a bit harder by stripping trailing suffix */
    tail = strrchr(location, '-');
    len = tail ? (tail - location) : strlen(location);
    buffer = g_malloc(len + 1);

    if (buffer) {
        memcpy(buffer, location, len);
        buffer[len] = 0;
        icomp = icaltimezone_get_builtin_timezone (buffer);
        g_free(buffer);
        if (icomp) {
            return icaltimezone_get_tzid(icomp);
        }
    }

    return NULL;
}

/**
 * matches a TZID against the system timezone definitions
 * and returns the matching TZID, or NULL if none found
 */
static const char *e_cal_match_tzid(const char *tzid)
{
    const char *location;
    const char *systzid;

    /*
     * old-style Evolution: /softwarestudio.org/Olson_20011030_5/America/Denver
     *
     * jump from one slash to the next and check whether the remainder
     * is a known location; start with the whole string (just in case)
     */
    for (location = tzid;
         location && location[0];
         location = strchr(location + 1, '/')) {
        systzid = e_cal_match_location(location[0] == '/' ?
                                       location + 1 :
                                       location);
        if (systzid) {
            return systzid;
        }
    }

    /* TODO: lookup table for Exchange TZIDs */

    return NULL;
}



gboolean e_cal_check_timezones(icalcomponent *comp,
                               icaltimezone *(*tzlookup)(const char *tzid,
                                                         const void *custom,
                                                         GError **error),
                               const void *custom,
                               GError **error)
{
    gboolean success = TRUE;
    icalcomponent *subcomp = NULL;
    icaltimezone *zone = icaltimezone_new();
    char *key = NULL, *value = NULL;

    /** a hash from old to new tzid; strings dynamically allocated */
    GHashTable *mapping = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

    *error = NULL;

    if (!mapping || !zone) {
        goto nomem;
    }

    /* iterate over all VTIMEZONE definitions */
    subcomp = icalcomponent_get_first_component(comp,
                                                ICAL_VTIMEZONE_COMPONENT);
    while (subcomp) {
        const char *tzid;
        if (icaltimezone_set_component(zone, subcomp)) {
            tzid = icaltimezone_get_tzid(zone);
            if (tzid) {
                const char *newtzid = e_cal_match_tzid(tzid);
                if (newtzid) {
                    g_free(key);
                    key = g_strdup(tzid);
                    if (!key) {
                        goto nomem;
                    }

                    g_free(value);
                    value = g_strdup(newtzid);
                    if (!value) {
                        goto nomem;
                    }

                    g_hash_table_insert(mapping, key, value);
                    key =
                        value = NULL;
                }
            }
        }

        subcomp = icalcomponent_get_next_component(comp,
                                                   ICAL_VTIMEZONE_COMPONENT);
    }

    /*
     * now replace all TZID parameters in place
     */
    subcomp = icalcomponent_get_first_component(comp,
                                                ICAL_ANY_COMPONENT);
    while (subcomp) {
        /*
         * Leave VTIMEZONE unchanged, iterate over properties of
         * everything else.
         *
         * Note that no attempt is made to remove unused VTIMEZONE
         * definitions. That would just make the code more complex for
         * little additional gain.
         */
        if (icalcomponent_isa(subcomp) != ICAL_VTIMEZONE_COMPONENT) {
            icalproperty *prop = icalcomponent_get_first_property(subcomp,
                                                                  ICAL_ANY_PROPERTY);
            while (prop) {
                icalparameter *param = icalproperty_get_first_parameter(prop,
                                                                        ICAL_ANY_PARAMETER);
                while (param) {
                    if (icalparameter_isa(param) == ICAL_TZID_PARAMETER) {
                        const char *tzid = icalparameter_get_tzid(param);
                        const char *oldtzid;
                        const char *newtzid;
                        
                        if (!g_hash_table_lookup_extended(mapping,
                                                          tzid,
                                                          (gpointer *)&oldtzid,
                                                          (gpointer *)&newtzid)) {
                            /* Corresponding VTIMEZONE not seen before! */
                            newtzid = e_cal_match_tzid(tzid);
                        }
                        if (newtzid) {
                            icalparameter_set_tzid(param, newtzid);
                        }
                    }
                    param = icalproperty_get_next_parameter(prop,
                                                            ICAL_ANY_PARAMETER);
                }
                prop = icalcomponent_get_next_property(subcomp,
                                                       ICAL_ANY_PROPERTY);
            }
        }
        subcomp = icalcomponent_get_next_component(comp,
                                                   ICAL_ANY_COMPONENT);
    }
    
    goto done;
 nomem:
    /* TODO: set gerror for "out of memory" */
 failed:
    /* gerror should have been set already */
    g_assert(*error);
    success = FALSE;
 done:
    if (mapping) {
        g_hash_table_destroy(mapping);
    }
    if (zone) {
        icaltimezone_free(zone, 1);
    }
    g_free(key);
    g_free(value);
    
    return success;
}

icaltimezone *e_cal_tzlookup_ecal(const char *tzid,
                                  const void *custom,
                                  GError **error)
{
    ECal *ecal = (ECal *)custom;
    icaltimezone *zone = NULL;

    if (e_cal_get_timezone(ecal, tzid, &zone, error)) {
        g_assert(zone);
        return zone;
    } else {
        return NULL;
    }
}

icaltimezone *e_cal_tzlookup_icomp(const char *tzid,
                                   const void *custom,
                                   GError **error)
{
    const icalcomponent *icomp = custom;
    /* TODO */

    return NULL;
}
