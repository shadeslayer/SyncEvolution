/*
 * Copyright (C) 2005 Patrick Ohly
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "EvolutionSyncSource.h"

ESource *EvolutionSyncSource::findSource( ESourceList *list, const string &id, bool isName )
{
    for (GSList *g = e_source_list_peek_groups (list); g; g = g->next) {
        ESourceGroup *group = E_SOURCE_GROUP (g->data);
        GSList *s;
        for (s = e_source_group_peek_sources (group); s; s = s->next) {
            ESource *source = E_SOURCE (s->data);
            if ( !id.compare( isName ?
                              e_source_peek_name(source) :
                              e_source_get_uri(source) ) )
                return source;
        }
    }
    return NULL;
}

void EvolutionSyncSource::throwError( const string &action, GError *gerror )
{
    string gerrorstr;
    if (gerror) {
        gerrorstr += " ";
        gerrorstr += gerror->message;
        g_clear_error(&gerror);
    } else {
        gerrorstr = ": failed";
    }
        
    throw m_id + ": " + action + gerrorstr;
}

string EvolutionSyncSource::getData(SyncItem& item)
{
    char *mem = (char *)malloc(item.getDataSize() + 1);
    memcpy(mem, item.getData(), item.getDataSize());
    mem[item.getDataSize()] = 0;

    string res(mem);
    free(mem);
    return res;
}
