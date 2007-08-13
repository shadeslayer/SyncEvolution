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

#include "SQLiteSyncSource.h"

#ifdef ENABLE_SQLITE

/**
 * Specialization of SQLiteSyncSource for contacts
 * with a schema as used by Mac OS X.
 */
class SQLiteContactSource : public SQLiteSyncSource
{
  public:
    SQLiteContactSource( const string name, SyncSourceConfig *sc, const string &changeId, const string &id ) :
    SQLiteSyncSource( name, sc, changeId, id)
        {}
    virtual ~SQLiteContactSource() {}

 protected:
    /* implementation of EvolutionSyncSource interface */
    virtual void open();
    virtual SyncItem *createItem( const string &uid, SyncState state );
    virtual void exportData(ostream &out);
    virtual string fileSuffix() { return "vcf"; }
    virtual const char *getMimeType() { return "text/x-vcard:2.1"; }
    virtual const char *getMimeVersion() { return "2.1"; }
    virtual const char *getSupportedTypes() { return "text/vcard:3.0,text/x-vcard:2.1"; }
    virtual ArrayElement* clone() { new SQLiteContactSource(getName(), &getConfig(), m_changeId, m_id); }
    virtual void beginSyncThrow(bool needAll,
                                bool needPartial,
                                bool deleteLocal);

    virtual void endSyncThrow();
    virtual int addItemThrow(SyncItem& item);
    virtual int updateItemThrow(SyncItem& item);
    virtual int deleteItemThrow(SyncItem& item);

    virtual void logItem(const string &uid, const string &info, bool debug = false);
    virtual void logItem(SyncItem &item, const string &info, bool debug = false);

    /* implementation of SQLiteSyncSource interface */
    virtual const char *getDefaultSchema();
    virtual const Mapping *getConstMapping();

 private:
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

    /** same as deleteItemThrow() but works with the uid directly */
    virtual int deleteItemThrow(const string &uid);

    /**
     * inserts the contact under a specific UID (if given) or
     * adds under a new UID
     */
    virtual int insertItemThrow(SyncItem &item, const char *uid, const string &creationTime);

};

#endif // ENABLE_SQLITE
#endif // INCL_SQLITECONTACTSOURCE
