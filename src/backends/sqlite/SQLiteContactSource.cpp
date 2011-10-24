/*
 * Copyright (C) 2007-2009 Patrick Ohly <patrick.ohly@gmx.de>
 * Copyright (C) 2009 Intel Corporation
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

#include <synthesis/SDK_util.h>
#ifdef ENABLE_SQLITE

#include "SQLiteContactSource.h"

#include <syncevo/SynthesisEngine.h>
#include <synthesis/sync_dbapi.h>

#include <algorithm>
#include <cctype>
#include <sstream>


#include <boost/algorithm/string/case_conv.hpp>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

enum {
    PERSON_LAST,
    PERSON_MIDDLE,
    PERSON_FIRST,
    PERSON_PREFIX,
    PERSON_SUFFIX,
    PERSON_LASTSORT,
    PERSON_FIRSTSORT,
    PERSON_ORGANIZATION,
    PERSON_DEPARTMENT,
    PERSON_UNIT,
    PERSON_NOTE,
    PERSON_BIRTHDAY,
    PERSON_JOBTITLE,
    PERSON_TITLE,
    PERSON_NICKNAME,
    PERSON_FULLNAME,
    PERSON_CATEGORIES,

    PERSON_AIM,
    PERSON_GROUPWISE,
    PERSON_ICQ,
    PERSON_YAHOO,

    PERSON_FILEAS,
    PERSON_ANNIVERSARY,

    PERSON_ASSISTANT,
    PERSON_MANAGER,
    PERSON_SPOUSE,

    PERSON_URL,
    PERSON_BLOG_URL,
    PERSON_VIDEO_URL,

    LAST_COL
};

void SQLiteContactSource::open()
{
    static const SQLiteUtil::Mapping mapping[LAST_COL + 1] = {
        { "Last", "ABPerson", "N_LAST"},
        { "Middle", "ABPerson", "N_MIDDLE"},
        { "First", "ABPerson", "N_FIRST" },
        { "Prefix", "ABPerson", "N_PREFIX" },
        { "Suffix", "ABPerson", "N_SUFFIX" },
        { "FirstSort", "ABPerson", "" },
        { "LastSort", "ABPerson", "" },
        { "Organization", "ABPerson", "ORG_NAME" },
        { "Department", "ABPerson", "ORG_DIVISION" },
        { "Unit", "ABPerson", "ORG_OFFICE" },
        //{"Team", "ABPerson", "ORG_TEAM" }
        { "Note", "ABPerson", "NOTE" },
        { "Birthday", "ABPerson", "BDAY" },
        { "JobTitle", "ABPerson", "ROLE" },
        { "Title", "ABPerson", "TITLE" },
        { "Nickname", "ABPerson", "NICKNAME" },
        { "CompositeNameFallback", "ABPerson", "FN" },
        { "Categories", "ABPerson", "CATEGORIES" },

        { "AIM", "ABPerson", "AIM_HANDLE" },
        { "Groupwise", "ABPerson", "GROUPWISE_HANDLE" },
        { "ICQ", "ABPerson", "ICQ_HANDLE" },
        { "Yahoo", "ABPerson", "YAHOO_HANDLE" },

        { "FileAs", "ABPerson", "FILE-AS" },
        { "Anniversary", "ABPerson", "ANNIVERSARY" },

        { "Assistant", "ABPerson", "ASSISTANT" },
        { "Manager", "ABPerson", "MANAGER" },
        { "Spouse", "ABPerson", "SPOUSE" },

    
        { "URL", "ABPerson", "WEB" },
        { "BlogURL", "ABPerson", "BLOGURL" },
        { "VideoURL", "ABPerson", "VIDEOURL" },

        { NULL }
    };
    static const char *schema = 
        "BEGIN TRANSACTION;"
        "CREATE TABLE ABPerson (ROWID INTEGER PRIMARY KEY AUTOINCREMENT, "
        "First TEXT, "
        "Last TEXT, "
        "Middle TEXT, "
        "FirstPhonetic TEXT, "
        "MiddlePhonetic TEXT, "
        "LastPhonetic TEXT, "
        "Organization TEXT, "
        "Department TEXT, "
        "Unit TEXT, "
        "Note TEXT, "
        "Kind INTEGER, "
        "Birthday TEXT, "
        "JobTitle TEXT, "
        "Title TEXT, "
        "Nickname TEXT, "
        "Prefix TEXT, "
        "Suffix TEXT, "
        "FirstSort TEXT, "
        "LastSort TEXT, "
        "CreationDate INTEGER, "
        "ModificationDate INTEGER, "
        "CompositeNameFallback TEXT, "
        "Categories TEXT, "
        "AIM TEXT, "
        "Groupwise TEXT, "
        "ICQ Text, "
        "Yahoo TEXT, "
        "Anniversary TEXT, "
        "Assistant TEXT, "
        "Manager TEXT, "
        "Spouse TEXT, "
        "URL TEXT, "
        "BlogURL TEXT, "
        "VideoURL TEXT, "
        "FileAs TEXT);"
        "COMMIT;";

    string id = getDatabaseID();
    m_sqlite.open(getName(),
                  id.c_str(),
                  mapping,
                  schema);
}

void SQLiteContactSource::close()
{
    m_sqlite.close();
}

void SQLiteContactSource::getSynthesisInfo(SynthesisInfo &info, XMLConfigFragments &fragment)
{
    SourceType sourceType = getSourceType();
    string type;
    if (!sourceType.m_format.empty()) {
        type = sourceType.m_format;
    }
    info.m_native = "vCard21";
    info.m_fieldlist = "contacts";
    info.m_datatypes =
        "        <use datatype='vCard21' mode='rw' preferred='yes'/>\n"
        "        <use datatype='vCard30' mode='rw'/>\n";

    if (type == "text/x-vcard:2.1" || type == "text/x-vcard") {
        info.m_datatypes =
            "        <use datatype='vCard21' mode='rw' preferred='yes'/>\n";
        if (!sourceType.m_forceFormat) {
            info.m_datatypes +=
                "        <use datatype='vCard30' mode='rw'/>\n";
        }
    } else if (type == "text/vcard:3.0" || type == "text/vcard") {
        info.m_datatypes =
            "        <use datatype='vCard30' mode='rw' preferred='yes'/>\n";
        if (!sourceType.m_forceFormat) {
            info.m_datatypes +=
                "        <use datatype='vCard21' mode='rw'/>\n";
        }
    } else {
        throwError(string("configured MIME type not supported: ") + type);
    }
 
}


SQLiteContactSource::Databases SQLiteContactSource::getDatabases()
{
    Databases result;

    result.push_back(Database("select database via file path",
                              "file:///<absolute path>"));
    return result;
}

bool SQLiteContactSource::isEmpty()
{
    // there are probably more efficient ways to do this, but this is just
    // a proof-of-concept anyway
    sqliteptr all(m_sqlite.prepareSQL("SELECT ROWID FROM ABPerson;"));
    while (m_sqlite.checkSQL(sqlite3_step(all)) == SQLITE_ROW) {
        return false;
    }
    return true;
}

void SQLiteContactSource::listAllItems(RevisionMap_t &revisions)
{
    sqliteptr all(m_sqlite.prepareSQL("SELECT ROWID, CreationDate, ModificationDate FROM ABPerson;"));
    while (m_sqlite.checkSQL(sqlite3_step(all)) == SQLITE_ROW) {
        string uid = m_sqlite.toString(SQLITE3_COLUMN_KEY(all, 0));
        string modTime = m_sqlite.time2str(m_sqlite.getTimeColumn(all, 2));
        revisions.insert(RevisionMap_t::value_type(uid, modTime));
    }
}

sysync::TSyError SQLiteContactSource::readItemAsKey(sysync::cItemID aID, sysync::KeyH aItemKey)
{
    string uid = aID->item;

    sqliteptr contact(m_sqlite.prepareSQL("SELECT * FROM ABPerson WHERE ROWID = '%s';", uid.c_str()));
    if (m_sqlite.checkSQL(sqlite3_step(contact)) != SQLITE_ROW) {
        throwError(STATUS_NOT_FOUND, string("contact not found: ") + uid);
    }

    for (int i = 0; i<LAST_COL; i++) {
        SQLiteUtil::Mapping map = m_sqlite.getMapping(i);
        string field = map.fieldname;
        if(!field.empty()) {
            string value = m_sqlite.getTextColumn(contact, map.colindex);
            sysync::TSyError res = getSynthesisAPI()->setValue(aItemKey, field, value.c_str(), value.size());
            if (res != sysync::LOCERR_OK) {
                SE_LOG_WARNING (NULL, NULL, "SQLite backend: set field %s value %s failed", field.c_str(), value.c_str());
            }
        }
    }
    return sysync::LOCERR_OK;
}

sysync::TSyError SQLiteContactSource::insertItemAsKey(sysync::KeyH aItemKey, sysync::cItemID aID, sysync::ItemID newID)
{
    string uid = aID ? aID->item :"";
    string newuid = uid;
    string creationTime;
    string first, last;

    int numparams = 0;
    stringstream cols;
    stringstream values;

    std::list<string> insValues;
    for (int i = 0; i<LAST_COL; i++) {
        SQLiteUtil::Mapping map = m_sqlite.getMapping(i);
        string field = map.fieldname;
        SharedBuffer data;
        if (!field.empty() && !getSynthesisAPI()->getValue (aItemKey, field, data)) {
            insValues.push_back (string (data.get()));
            cols << m_sqlite.getMapping(i).colname << ", ";
            values <<"?, ";
            numparams ++;
            if (field == "N_FIRST") {
                first = insValues.back();
            } else if (field == "N_LAST") {
                last = insValues.back();
            }
        }
    }

    // synthesize sort keys: upper case with specific order of first/last name
    string firstsort = first + " " + last;
    boost::to_upper(firstsort);
    string lastsort = last + " " + first;
    boost::to_upper(lastsort);

    cols << "FirstSort, LastSort";
    values << "?, ?";
    numparams += 2;
    insValues.push_back(firstsort);
    insValues.push_back(lastsort);

    // optional fixed UID, potentially fixed creation time
    if (uid.size()) {
        creationTime = m_sqlite.findColumn("ABPerson", "ROWID", uid.c_str(), "CreationDate", "");
        cols << ", ROWID";
        values << ", ?";
        numparams++;
    }
    cols << ", CreationDate, ModificationDate";
    values << ", ?, ?";
    numparams += 2;

    // delete complete row so that we can recreate it
    if (uid.size()) {
        sqliteptr remove(m_sqlite.prepareSQL("DELETE FROM ABPerson WHERE ROWID == ?;"));
        m_sqlite.checkSQL(sqlite3_bind_text(remove, 1, uid.c_str(), -1, SQLITE_TRANSIENT));
        m_sqlite.checkSQL(sqlite3_step(remove));
    }

    string cols_str = cols.str();
    string values_str = values.str();

    sqliteptr insert(m_sqlite.prepareSQL("INSERT INTO ABPerson( %s ) VALUES( %s );", cols.str().c_str(), values.str().c_str()));

    // now bind parameter values in the same order as the columns specification above
    int param = 1;
    BOOST_FOREACH (string &value, insValues) {
        m_sqlite.checkSQL(sqlite3_bind_text(insert, param++, value.c_str(), -1, SQLITE_TRANSIENT));
    }
    if (uid.size()) {
        m_sqlite.checkSQL(sqlite3_bind_text(insert, param++, uid.c_str(), -1, SQLITE_TRANSIENT));
        m_sqlite.checkSQL(sqlite3_bind_text(insert, param++, creationTime.c_str(), -1, SQLITE_TRANSIENT));
    } else {
        m_sqlite.checkSQL(sqlite3_bind_int64(insert, param++, (long long)time(NULL)));
    }
    SQLiteUtil::syncml_time_t modificationTime = time(NULL);
    m_sqlite.checkSQL(sqlite3_bind_int64(insert, param++, modificationTime));
                      
    m_sqlite.checkSQL(sqlite3_step(insert));
                      
    if (!uid.size()) {
        // figure out which UID was assigned to the new contact
        newuid = m_sqlite.findColumn("SQLITE_SEQUENCE", "NAME", "ABPerson", "SEQ", "");
    }
    newID->item = StrAlloc(newuid.c_str());

    updateRevision(*m_trackingNode, uid, newuid, m_sqlite.time2str(modificationTime));
    return sysync::LOCERR_OK;
}


void SQLiteContactSource::deleteItem(const string& uid)
{
    sqliteptr del;

    del.set(m_sqlite.prepareSQL("DELETE FROM ABPerson WHERE "
                                "ABPerson.ROWID = ?;"));
    m_sqlite.checkSQL(sqlite3_bind_text(del, 1, uid.c_str(), -1, SQLITE_TRANSIENT));
    m_sqlite.checkSQL(sqlite3_step(del));
    // TODO: throw STATUS_NOT_FOUND exception when nothing was deleted
    deleteRevision(*m_trackingNode, uid);
}

void SQLiteContactSource::enableServerMode()
{
    SyncSourceAdmin::init(m_operations, this);
    SyncSourceBlob::init(m_operations, getCacheDir());
}

bool SQLiteContactSource::serverModeEnabled() const
{
    return m_operations.m_loadAdminData;
}


void SQLiteContactSource::beginSync(const std::string &lastToken, const std::string &resumeToken)
{
    detectChanges(*m_trackingNode, CHANGES_FULL);
}


std::string SQLiteContactSource::endSync(bool success)
{
    if (success) {
        m_trackingNode->flush();
    } else {
        // The Synthesis docs say that we should rollback in case of
        // failure. Cannot do that for data, so lets at least keep
        // the revision map unchanged.
    }

    // no token handling at the moment (not needed for clients)
    return "";
}

SE_END_CXX

#endif /* ENABLE_SQLITE */

#ifdef ENABLE_MODULES
# include "SQLiteContactSourceRegister.cpp"
#endif
