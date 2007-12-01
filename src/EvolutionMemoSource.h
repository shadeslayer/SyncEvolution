/*
 * Copyright (C) 2005-2006 Patrick Ohly
 * Copyright (C) 2007 Funambol
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

#ifndef INCL_EVOLUTIONMEMOSOURCE
#define INCL_EVOLUTIONMEMOSOURCE

#include <config.h>
#include "EvolutionCalendarSource.h"

#ifdef ENABLE_ECAL

/**
 * Implements access to Evolution memo lists (stored as calendars),
 * exporting/importing the memos in plain UTF-8 text. Only the DESCRIPTION
 * part of a memo is synchronized.
 */
class EvolutionMemoSource : public EvolutionCalendarSource
{
  public:
    EvolutionMemoSource(ECalSourceType type,
                        const string &name,
                        SyncSourceConfig *sc,
                        const string &changeId,
                        const string &id) :
        EvolutionCalendarSource(type, name, sc, changeId, id) {}
    
    //
    // implementation of EvolutionSyncSource
    //
    virtual SyncItem *createItem( const string &uid, SyncState state );
    virtual const char *getMimeType() { return "text/plain"; }
    virtual const char *getMimeVersion() { return "1.0"; }
    virtual const char *getSupportedTypes() { return "text/plain:1.0"; }

    //
    // implementation of SyncSource
    //
    virtual ArrayElement *clone() { return new EvolutionMemoSource(*this); }

  private:
    virtual int insertItem(SyncItem &item, bool update);
};

#endif // ENABLE_ECAL

#endif // INCL_EVOLUTIONMEMOSOURCE
