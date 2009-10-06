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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef ENABLE_SQLITE

#include "SQLiteUtil.h"
#include <syncevo/util.h>

#include <stdarg.h>
#include <sstream>
#include <cstring>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

void SQLiteUtil::throwError(const string &operation)
{
    string descr = m_name + ": '" + m_fileid + "': " + operation + " failed";

    if (m_db) {
        const char *error = sqlite3_errmsg(m_db);
        descr += ": ";
        descr += error ? error : "unspecified error";
    }

    throw runtime_error(descr);
}

sqlite3_stmt *SQLiteUtil::prepareSQLWrapper(const char *sql, const char **nextsql)
{
    sqlite3_stmt *stmt = NULL;

    checkSQL(sqlite3_prepare(m_db, sql, -1, &stmt, nextsql), sql);
    return stmt;
}

sqlite3_stmt *SQLiteUtil::prepareSQL(const char *sqlfmt, ...)
{
    va_list ap;
   
    va_start(ap, sqlfmt);
    string s = StringPrintfV (sqlfmt, ap);
    va_end(ap);
    return prepareSQLWrapper(s.c_str());
}


SQLiteUtil::key_t SQLiteUtil::findKey(const char *database, const char *keyname, const char *key)
{
    sqliteptr query(prepareSQL("SELECT ROWID FROM %s WHERE %s = '%s';", database, keyname, key));

    int res = checkSQL(sqlite3_step(query), "getting key");
    if (res == SQLITE_ROW) {
        return sqlite3_column_int64(query, 0);
    } else {
        return -1;
    }
}

string SQLiteUtil::findColumn(const char *database, const char *keyname, const char *key, const char *column, const char *def)
{
    sqliteptr query(prepareSQL("SELECT %s FROM %s WHERE %s = '%s';", column, database, keyname, key));

    int res = checkSQL(sqlite3_step(query), "getting key");
    if (res == SQLITE_ROW) {
        const unsigned char *text = sqlite3_column_text(query, 0);

        return text ? (const char *)text : def;
    } else {
        return def;
    }
}

string SQLiteUtil::getTextColumn(sqlite3_stmt *stmt, int col, const char *def)
{
    const unsigned char *text = sqlite3_column_text(stmt, col);
    return text ? (const char *)text : def;
}

SQLiteUtil::syncml_time_t SQLiteUtil::getTimeColumn(sqlite3_stmt *stmt, int col)
{
    // assumes that the database stores the result of time() directly
    return sqlite3_column_int64(stmt, col);
}

string SQLiteUtil::time2str(SQLiteUtil::syncml_time_t t)
{
    char buffer[128];
    sprintf(buffer, "%lu", t);
    return buffer;
}

void SQLiteUtil::open(const string &name,
                      const string &fileid,
                      const SQLiteUtil::Mapping *mapping,
                      const char *schema)
{
    close();
    m_name = name;
    m_fileid = fileid;

    const string prefix("file://");
    bool create = fileid.substr(0, prefix.size()) == prefix;
    string filename = create ? fileid.substr(prefix.size()) : fileid;

    if (!create && access(filename.c_str(), F_OK)) {
        throw runtime_error(m_name + ": no such database: '" + filename + "'");
    }

    sqlite3 *db;
    int res = sqlite3_open(filename.c_str(), &db);
    m_db = db;
    checkSQL(res, "opening");

    // check whether file is empty = newly created, define schema if that's the case
    sqliteptr check(prepareSQLWrapper("SELECT * FROM sqlite_master;"));
    switch (sqlite3_step(check)) {
    case SQLITE_ROW:
        // okay
        break;
    case SQLITE_DONE: {
        // empty
        const char *nextsql = schema;
        while (nextsql && *nextsql) {
            const char *sql = nextsql;
            sqliteptr create(prepareSQLWrapper(sql, &nextsql));
            while (true) {
                int res = sqlite3_step(create);
                if (res == SQLITE_DONE) {
                    break;
                } else if (res == SQLITE_ROW) {
                    // continue
                } else {
                    throwError("creating database");\
                }
            }
        }
        break;
    }
    default:
        throwError("checking content");
        break;
    }

    // query database schema to find columns we need
    int i;
    for (i = 0; mapping[i].colname; i++) ;
    m_mapping.set(new Mapping[i + 1]);
    sqliteptr query;
    string tablename;
    for (i = 0; mapping[i].colname; i++) {
        m_mapping[i] = mapping[i];

        // switching to a different table?
        if (tablename != m_mapping[i].tablename) {
            tablename = m_mapping[i].tablename;
            query.set(prepareSQL("SELECT * FROM %s;", tablename.c_str()));
        }

        // search for this column name
        for (m_mapping[i].colindex = sqlite3_column_count(query) - 1;
             m_mapping[i].colindex >= 0;
             m_mapping[i].colindex--) {
            const char *name = sqlite3_column_name(query, m_mapping[i].colindex);
            if (name && !strcasecmp(m_mapping[i].colname, name)) {
                break;
            }
        }
    }
    memset(&m_mapping[i], 0, sizeof(m_mapping[i]));
}

void SQLiteUtil::close()
{
    m_db = NULL;
}


SE_END_CXX

#endif /* ENABLE_SQLITE */

