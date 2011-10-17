/*
 * Copyright (C) 2005-2009 Patrick Ohly <patrick.ohly@gmx.de>
 * Copyright (C) 2009 Intel Corporation
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

#include <memory>
using namespace std;

#include "config.h"

#ifdef ENABLE_ECAL

#include "EvolutionMemoSource.h"

#include <syncevo/Logging.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

void EvolutionMemoSource::readItem(const string &luid, std::string &item, bool raw)
{
    if (raw) {
        EvolutionCalendarSource::readItem(luid, item, false);
        return;
    }

    ItemID id(luid);
    eptr<icalcomponent> comp(retrieveItem(id));
    icalcomponent *cal = icalcomponent_get_first_component(comp, ICAL_VCALENDAR_COMPONENT);
    if (!cal) {
        cal = comp;
    }
    icalcomponent *journal = icalcomponent_get_first_component(cal, ICAL_VJOURNAL_COMPONENT);
    if (!journal) {
        journal = comp;
    }
    icalproperty *summaryprop = icalcomponent_get_first_property(journal, ICAL_SUMMARY_PROPERTY);
    string summary;
    if (summaryprop) {
        const char *summaryptr = icalproperty_get_summary(summaryprop);
        if (summaryptr) {
            summary = summaryptr;
        }
    }
    icalproperty *desc = icalcomponent_get_first_property(journal, ICAL_DESCRIPTION_PROPERTY);
    if (desc) {
        const char *text = icalproperty_get_description(desc);
        if (text) {
            size_t len = strlen(text);
            bool insertSummary = false;
            const char *eol;

            // Check the first line: if it is the same as the summary,
            // then ignore the summary. Otherwise include the summary
            // as first line in the text body. At a receiving Evolution
            // the summary will remain part of the text for compatibility
            // with other clients which might use the first line as part
            // of the normal text.
            eol = strchr(text, '\n');
            if (!eol) {
                eol = text + len;
            }
            if (summary.size() &&
                summary.compare(0, summary.size(), text, eol - text)) {
                insertSummary = true;
            }

            // Replace all \n with \r\n: in the worst case the text
            // becomes twice as long. Also make room for summary.
            eptr<char> dostext((char *)malloc(len * 2 + 1 +
                                              (insertSummary ? summary.size() + 2 : 0)));
            const char *from = text;
            char *to = dostext;
            if (insertSummary) {
                memcpy(to, summary.c_str(), summary.size());
                memcpy(to + summary.size(), "\r\n", 2);
                to += summary.size() + 2;
            }
            eol = strchr(from, '\n');
            while (eol) {
                size_t linelen = eol - from;
                memcpy(to, from, linelen);
                to += linelen;
                from += linelen;
                to[0] = '\r';
                to[1] = '\n';
                to += 2;
                from++;

                eol = strchr(from, '\n');
            }
            memcpy(to, from, strlen(from) + 1);
            item = dostext.get();
        }
    }

    if (item.empty()) {
        // no description, use summary
        item = summary;
    }
}

EvolutionCalendarSource::InsertItemResult EvolutionMemoSource::insertItem(const string &luid, const std::string &item, bool raw)
{
    if (raw) {
        return EvolutionCalendarSource::insertItem(luid, item, false);
    }
    
    bool update = !luid.empty();
    InsertItemResultState state = ITEM_OKAY;
    string newluid = luid;
    string modTime;

    eptr<char> text;
    text.set((char *)malloc(item.size() + 1), "copy of item");
    memcpy(text, item.c_str(), item.size());
    text.get()[item.size()] = 0;

    // replace all \r\n with \n
    char *from = text, *to = text;
    const char *eol = strstr(from, "\r\n");
    while (eol) {
        size_t linelen = eol - from;
        if (to != from) {
            memmove(to, from, linelen);
        }
        to += linelen;
        from += linelen;
        *to = '\n';
        to++;
        from += 2;

        eol = strstr(from, "\r\n");
    }
    if (to != from) {
        memmove(to, from, strlen(from) + 1);
    }

    eol = strchr(text, '\n');
    string summary;
    if (eol) {
        summary.insert(0, (char *)text, eol - (char *)text);
    } else {
        summary = (char *)text;
    }

    eptr<icalcomponent> subcomp(icalcomponent_vanew(
                                    ICAL_VJOURNAL_COMPONENT,
                                    icalproperty_new_summary(summary.c_str()),
                                    icalproperty_new_description(text),
                                    0));

    if( !subcomp ) {
        throwError(string("failure creating vjournal " ) + summary);
    }

    GError *gerror = NULL;
    if (!update) {
        const char *uid = NULL;

        if(!e_cal_create_object(m_calendar, subcomp, (char **)&uid, &gerror)) {
            if (gerror->domain == E_CALENDAR_ERROR &&
                gerror->code == E_CALENDAR_STATUS_OBJECT_ID_ALREADY_EXISTS) {
                // Deal with error due to adding already existing item.
                // Should never happen for plain text journal entries because
                // they have no embedded ID, but who knows...
                state = ITEM_NEEDS_MERGE;
                uid = icalcomponent_get_uid(subcomp);
                if (!uid) {
                    throwError("storing new memo item, no UID set", gerror);
                }
                g_clear_error(&gerror);
            } else {
                throwError( "storing new memo item", gerror );
            }
        }
        ItemID id(uid, "");
        newluid = id.getLUID();
        if (state != ITEM_NEEDS_MERGE) {
            modTime = getItemModTime(id);
        }
    } else {
        ItemID id(newluid);

        // ensure that the component has the right UID
        if (update && !id.m_uid.empty()) {
            icalcomponent_set_uid(subcomp, id.m_uid.c_str());
        }

        if (!e_cal_modify_object(m_calendar, subcomp, CALOBJ_MOD_ALL, &gerror)) {
            throwError(string("updating memo item ") + luid, gerror);
        }
        ItemID newid = getItemID(subcomp);
        newluid = newid.getLUID();
        modTime = getItemModTime(newid);
    }

    return InsertItemResult(newluid, modTime, state);
}

bool EvolutionMemoSource::isNativeType(const char *type)
{
    return type &&
        (!strcasecmp(type, "raw") ||
         !strcasecmp(type, "text/x-vcalendar") ||
         !strcasecmp(type, "text/calendar"));
}

SE_END_CXX

#endif /* ENABLE_ECAL */

