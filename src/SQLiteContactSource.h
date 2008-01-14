/*
 * Copyright (C) 2007 Patrick Ohly
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

#ifndef INCL_SQLITECONTACTSOURCE
#define INCL_SQLITECONTACTSOURCE

#include "TrackingSyncSource.h"
#include "SQLiteUtil.h"

#ifdef ENABLE_SQLITE

/**
 * Uses SQLiteUtil for contacts
 * with a schema as used by Mac OS X.
 * The schema has hierarchical tables, which is not
 * supported by SQLiteUtil, so only the properties which
 * have a 1:1 mapping are currently stored.
 */
class SQLiteContactSource : public TrackingSyncSource
{
  public:
    SQLiteContactSource( const string name, SyncSourceConfig *sc, const string &changeId, const string &id, eptr<spdm::DeviceManagementNode> trackingNode) :
        TrackingSyncSource(name, sc, changeId, id, trackingNode)
        {}

 protected:
    /* implementation of EvolutionSyncSource interface */
    virtual void open();
    virtual void close();
    virtual sources getSyncBackends() { return sources(); }
    virtual SyncItem *createItem(const string &uid);
    virtual string fileSuffix() { return "vcf"; }
    virtual const char *getMimeType() { return "text/x-vcard:2.1"; }
    virtual const char *getMimeVersion() { return "2.1"; }
    virtual const char *getSupportedTypes() { return "text/vcard:3.0,text/x-vcard:2.1"; }
    virtual void logItem(const string &uid, const string &info, bool debug = false);
    virtual void logItem(SyncItem &item, const string &info, bool debug = false);

    /* implementation of TrackingSyncSource interface */
    virtual void listAllItems(RevisionMap_t &revisions);
    virtual string insertItem(string &uid, const SyncItem &item);
    virtual void deleteItem(const string &uid);
    virtual void flush();

 private:
    /** encapsulates access to database */
    SQLiteUtil m_sqlite;

    /** constant key values defined by tables in the database, queried during open() */
    key_t m_addrCountryCode,
        m_addrCity,
        m_addrStreet,
        m_addrState,
        m_addrZIP,
        m_addrCountry,
        m_typeMobile,
        m_typeHome,
        m_typeWork;
};

#endif // ENABLE_SQLITE
#endif // INCL_SQLITECONTACTSOURCE
