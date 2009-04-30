/*
 * Copyright (C) 2005-2009 Patrick Ohly <patrick.ohly@gmx.de>
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
    EvolutionMemoSource(const EvolutionSyncSourceParams &params) :
        EvolutionCalendarSource(E_CAL_SOURCE_TYPE_JOURNAL, params) {}
    
    //
    // implementation of EvolutionSyncSource
    //
    virtual SyncItem *createItem(const string &uid);
    virtual InsertItemResult insertItem(const string &luid, const SyncItem &item);
    virtual const char *getMimeType() const { return "text/plain"; }
    virtual const char *getMimeVersion() const { return "1.0"; }
    virtual const char *getSupportedTypes() const { return "text/plain:1.0"; }
};

#endif // ENABLE_ECAL

#endif // INCL_EVOLUTIONMEMOSOURCE
