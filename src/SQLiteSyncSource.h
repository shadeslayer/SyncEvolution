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

#ifndef INCL_SQLITESYNCSOURCE
#define INCL_SQLITESYNCSOURCE

#include "EvolutionSyncSource.h"

#ifdef ENABLE_SQLITE

#include <sqlite3.h>
#include "EvolutionSmartPtr.h"

namespace vocl {
    class VObject;
}
void inline unref(sqlite3 *db) { sqlite3_close(db); }
void inline unref(sqlite3_stmt *stmt) { sqlite3_finalize(stmt); }

/**
 * This class implements access to SQLite database files:
 * - opening the database file
 * - error reporting
 * - creating a database file in debugging mode
 */
class SQLiteSyncSource : public EvolutionSyncSource
{
  public:
    /**
     * Creates a new Evolution sync source.
     *
     * @param    name        the named needed by SyncSource
     * @param    sc          obligatory config for this source, must remain valid throughout the lifetime of the source
     * @param    changeId    is used to track changes in the Evolution backend
     * @param    id          identifies the backend; not specifying it makes this instance
     *                       unusable for anything but listing backend databases
     */
    SQLiteSyncSource( const string name, SyncSourceConfig *sc, const string &changeId, const string &id ) :
    EvolutionSyncSource( name, sc, changeId, id),
        m_db(NULL)
        {}
    virtual ~SQLiteSyncSource() {}

    /* implementation of EvolutionSyncSource interface */
    virtual sources getSyncBackends() { return sources(); /* we cannot list available databases */ }
    virtual void open();
    virtual void close();


 protected:
    /** throw error for a specific sqlite3 operation on m_db */
    void throwError(const string &operation);

    /**
     * wrapper around sqlite3_prepare() which operates on the current
     * database and throws an error if the call fails
     *
     * @param sqlfmt         printf-style format string for query, followed by parameters for sprintf
     */
    sqlite3_stmt *prepareSQL(const char *sqlfmt, ...);

    /**
     * wrapper around sqlite3_prepare() which operates on the current
     * database and throws an error if the call fails
     *
     * @param sql       preformatted SQL statement(s)
     * @param nextsql   pointer to next statement in sql
     */
    sqlite3_stmt *prepareSQLWrapper(const char *sql, const char **nextsql = NULL);


    /** checks the result of an sqlite3 call, throws an error if faulty, otherwise returns the result */
    int checkSQL(int res, const char *operation = "SQLite call") {
        if (res != SQLITE_OK && res != SQLITE_ROW && res != SQLITE_DONE) {
            throwError(operation);
        }
        return res;
    }

    /** type used for row keys */
    typedef long long key_t;
    string toString(key_t key) { char buffer[32]; sprintf(buffer, "%lld", key); return buffer; }
#define SQLITE3_COLUMN_KEY sqlite3_column_int64

    /** return row ID for a certain row */
    key_t findKey(const char *database, const char *keyname, const char *key);

    /** return a specific column for a row identified by a certain key column as text, returns default text if not found */
    string findColumn(const char *database, const char *keyname, const char *key, const char *column, const char *def);

    /** a wrapper for sqlite3_column_test() which will check for NULL and returns default text instead */
    string getTextColumn(sqlite3_stmt *stmt, int col, const char *def = "");

    typedef unsigned long syncml_time_t;
    /** transform column to same time base as used by SyncML libary (typically time()) */
    syncml_time_t getTimeColumn(sqlite3_stmt *stmt, int col);

    /** copies all columns which directly map to a property into the vobj */
    void rowToVObject(sqlite3_stmt *stmt, vocl::VObject &vobj);

    /** database schema to use when creating new databases, may be NULL */
    virtual const char *getDefaultSchema() = 0;

    /** information about the database mapping */
    struct Mapping {
        const char *colname;        /**< column name in SQL table */
        const char *tablename;      /**< name of the SQL table which has this column */
        const char *propname;       /**< optional: vcard/vcalendar property which corresponds to this */
        int colindex;               /**< determined dynamically in open(): index of the column, -1 if not present */
    };

    /**
     * array with database mapping, terminated by NULL colname:
     * variable fields are stored in a copy maintained by the SQLiteSyncSource class
     */
    virtual const Mapping *getConstMapping() = 0;

    /** filled in by SQLiteSyncSource::open() */
    arrayptr<Mapping> m_mapping;

    /** after opening: current databse */
    eptr<sqlite3> m_db;
};

#endif // ENABLE_SQLITE
#endif // INCL_SQLITESYNCSOURCE
