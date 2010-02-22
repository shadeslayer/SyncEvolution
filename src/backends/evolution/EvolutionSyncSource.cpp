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

#include "EvolutionSyncSource.h"
#include <syncevo/SmartPtr.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

#ifdef HAVE_EDS

ESource *EvolutionSyncSource::findSource( ESourceList *list, const string &id )
{
    string finalID;
    if (!id.empty()) {
        finalID = id;
    } else {
        // Nothing selected specifically, use the one marked as default
        // by the backend. If none is marked, use the first one
        // (not expected to happen, though).
        BOOST_FOREACH(const Database &db, getDatabases()) {
            if (db.m_isDefault) {
                finalID = db.m_uri;
                break;
            }
        }
    }

    for (GSList *g = e_source_list_peek_groups (list); g; g = g->next) {
        ESourceGroup *group = E_SOURCE_GROUP (g->data);
        GSList *s;
        for (s = e_source_group_peek_sources (group); s; s = s->next) {
            ESource *source = E_SOURCE (s->data);
            GString uri(e_source_get_uri(source));
            bool found = finalID.empty() ||
                !finalID.compare(e_source_peek_name(source)) ||
                (uri && !finalID.compare(uri));
            if (found) {
                return source;
            }
        }
    }
    return NULL;
}

void EvolutionSyncSource::throwError(const string &action, GError *gerror)
{
    string gerrorstr;
    if (gerror) {
        gerrorstr += ": ";
        gerrorstr += gerror->message;
        g_clear_error(&gerror);
    } else {
        gerrorstr = ": failure";
    }

    throwError(action + gerrorstr);
}
#endif // HAVE_EDS

SE_END_CXX
