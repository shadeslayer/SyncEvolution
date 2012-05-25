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

#include "config.h"
#include <EvolutionCalendarSource.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

#ifdef ENABLE_ECAL

/**
 * Implements access to Evolution memo lists (stored as calendars),
 * exporting/importing the memos in plain UTF-8 text. Only the DESCRIPTION
 * part of a memo is synchronized.
 */
class EvolutionMemoSource : public EvolutionCalendarSource
{
  public:
    EvolutionMemoSource(const SyncSourceParams &params) :
        EvolutionCalendarSource(EVOLUTION_CAL_SOURCE_TYPE_MEMOS, 
                                params) {}        
    
    //
    // implementation of SyncSource
    //
    virtual InsertItemResult insertItem(const string &uid, const std::string &item, bool raw);
    void readItem(const std::string &luid, std::string &item, bool raw);
    virtual std::string getMimeType() const { return "text/plain"; }
    virtual std::string getMimeVersion() const { return "1.0"; }

 private:
    bool isNativeType(const char *type);
};

#endif // ENABLE_ECAL


SE_END_CXX
#endif // INCL_EVOLUTIONMEMOSOURCE
