/*
 * Copyright (C) 2005-2009 Patrick Ohly <patrick.ohly@gmx.de>
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
