/*
 * Copyright (C) 2007-2009 Patrick Ohly <patrick.ohly@gmx.de>
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

#ifndef INCL_SQLITECONTACTSOURCE
#define INCL_SQLITECONTACTSOURCE

#include "TrackingSyncSource.h"
#include "SQLiteUtil.h"

#ifdef ENABLE_SQLITE

/**
 * Uses SQLiteUtil for contacts with a schema inspired by the one used
 * by Mac OS X.  That schema has hierarchical tables which is not
 * supported by SQLiteUtil, therefore SQLiteContactSource uses a
 * simplified schema where each contact consists of one row in the
 * database table.
 *
 * The handling of the "N" and "ORG" property shows how mapping
 * between one property and multiple different columns works.
 *
 * Properties which can occur more than once per contact like address,
 * email and phone numbers are not supported. They would have to be
 * stored in additional tables.
 *
 * Change tracking is done by implementing a modification date as part
 * of each contact and using that as the revision string required by
 * TrackingSyncSource, which then takes care of change tracking.
 *
 * The database file is created automatically if the database ID is
 * file:///<path>.
 */
class SQLiteContactSource : public TrackingSyncSource
{
  public:
    SQLiteContactSource(const SyncSourceParams &params) :
        TrackingSyncSource(params)
        {}

 protected:
    /* implementation of SyncSource interface */
    virtual void open();
    virtual void close();
    virtual Databases getDatabases();
    virtual const char *getMimeType() const { return "text/x-vcard"; }
    virtual const char *getMimeVersion() const { return "2.1"; }
    virtual const char *getSupportedTypes()const { return "text/vcard:3.0,text/x-vcard:2.1"; }

    /* implementation of TrackingSyncSource interface */
    virtual void listAllItems(RevisionMap_t &revisions);
    virtual InsertItemResult insertItem(const string &uid, const std::string &item, bool raw);
    void readItem(const std::string &luid, std::string &item, bool raw);
    virtual void removeItem(const string &uid);

 private:
    /** encapsulates access to database */
    SQLiteUtil m_sqlite;
};

#endif // ENABLE_SQLITE
#endif // INCL_SQLITECONTACTSOURCE
