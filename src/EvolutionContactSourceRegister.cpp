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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "EvolutionContactSource.h"

static EvolutionSyncSource *createSource(const EvolutionSyncSourceParams &params)
{
    pair <string, string> sourceType = EvolutionSyncSource::getSourceType(params.m_nodes);
    bool isMe = sourceType.first == "Evolution Contacts";

#ifndef ENABLE_EBOOK
    return isMe ? RegisterSyncSource::InactiveSource : NULL;
#else
    bool maybeMe = sourceType.first == "addressbook";
    
    if (isMe || maybeMe) {
        if (sourceType.second == "" || sourceType.second == "text/x-vcard") {
            return new EvolutionContactSource(params, EVC_FORMAT_VCARD_21);
        } else if (sourceType.second == "text/vcard") {
            return new EvolutionContactSource(params, EVC_FORMAT_VCARD_30);
        }
    }
    return NULL;
#endif
}

static RegisterSyncSource registerMe("Evolution Address Book",
#ifdef ENABLE_EBOOK
                                     true,
#else
                                     false,
#endif
                                     createSource,
                                     "Evolution Address Book = addressbook = contacts = evolution-contacts\n"
                                     "   vCard 2.1 (default) = text/x-vcard\n"
                                     "   vCard 3.0 = text/vcard\n"
                                     "   The later is the internal format of Evolution and preferred with\n"
                                     "   servers that support it. One such server is ScheduleWorld\n"
                                     "   together with the \"card3\" uri.\n",
                                     Values() +
                                     (Aliases("Evolution Address Book") + "evolution-contacts"));
