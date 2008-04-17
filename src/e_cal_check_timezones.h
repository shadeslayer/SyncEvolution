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

#ifndef E_CAL_CHECK_TIMEZONES_H
#define E_CAL_CHECK_TIMEZONES_H

#include <libical/ical.h>
#include <glib.h>

G_BEGIN_DECLS

/**
 * This function cleans up VEVENT, VJOURNAL, VTODO and VTIMEZONE
 * items which are to be imported into Evolution.
 *
 * Using VTIMEZONE definitions is problematic because they cannot be
 * updated properly when timezone definitions change. They are also
 * incomplete (for compatibility reason only one set of rules for
 * summer saving changes can be included, even if different rules
 * apply in different years). This function looks for matches of the
 * used TZIDs against system timezones and replaces such TZIDs with
 * the corresponding system timezone. This works for TZIDs containing
 * a location (found via a fuzzy string search) and for Outlook TZIDs
 * (via a hard-coded lookup table).
 *
 * Some programs generate broken meeting invitations with TZID, but
 * without including the corresponding VTIMEZONE. Importing such
 * invitations unchanged causes problems later on (meeting displayed
 * incorrectly, e_cal_get_component_as_string() fails). The situation
 * where this occurred in the past (found by a SyncEvolution user) is
 * now handled via the location based mapping.
 *
 * If this mapping fails, this function also deals with VTIMEZONE
 * conflicts: such conflicts occur when the calendar already contains
 * an old VTIMEZONE definition with the same TZID, but different
 * summer saving rules. Replacing the VTIMEZONE potentially breaks
 * displaying of old events, whereas not replacing it breaks the new
 * events (the behavior in Evolution <= 2.22.1).
 *
 * The way this problem is resolved is by renaming the new VTIMEZONE
 * definition until the TZID is unique. A running count is appended to
 * the TZID. All items referencing the renamed TZID are adapted
 * accordingly.
 *
 * @param comp      a VCALENDAR containing a list of
 *                  VTIMEZONE and arbitrary other components, in
 *                  arbitrary order: these other components are
 *                  modified by this call
 * @param tzlookup  a callback function which is called to retrieve
 *                  a calendar's VTIMEZONE definition; the returned
 *                  definition is *not* freed by e_cal_check_timezones()
 *                  (to be compatible with e_cal_get_timezone());
 *                  NULL indicates that no such timezone exists
 *                  or an error occurred
 * @param custom    an arbitrary pointer which is passed through to
 *                  the tzlookup function
 * @retval error    an error description in case of a failure
 * @return TRUE if successful, FALSE otherwise.
 */
gboolean e_cal_check_timezones(icalcomponent *comp,
                               icaltimezone *(*tzlookup)(const char *tzid,
                                                         const void *custom,
                                                         GError **error),
                               const void *custom,
                               GError **error);

/**
 * An implementation of the tzlookup callback which clients
 * can use. Calls e_cal_get_timezone().
 *
 * @param custom     must be a valid ECal pointer
 */
icaltimezone *e_cal_tzlookup_ecal(const char *tzid,
                                  const void *custom,
                                  GError **error);

/**
 * An implementation of the tzlookup callback which backends
 * like the file backend can use. Searches for the timezone
 * in the component list.
 *
 * @param custom     must be a icalcomponent pointer which contains
 *                   either a VCALENDAR with VTIMEZONEs or VTIMEZONES
 *                   directly
 */
icaltimezone *e_cal_tzlookup_icomp(const char *tzid,
                                   const void *custom,
                                   GError **error);

G_END_DECLS

#endif /* E_CAL_CHECK_TIMEZONES_H */
