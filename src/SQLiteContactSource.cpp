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

#include "config.h"

#ifdef ENABLE_SQLITE

#include "SQLiteContactSource.h"

#include <common/base/Log.h>
#include "vocl/VConverter.h"

#include <algorithm>
#include <cctype>
#include <sstream>

using namespace vocl;

const char *SQLiteContactSource::getDefaultSchema()
{
    return 
        "BEGIN TRANSACTION;"
        "CREATE TABLE ABMultiValue (UID INTEGER PRIMARY KEY, record_id INTEGER, property INTEGER, identifier INTEGER, label INTEGER, value TEXT);"
        "CREATE TABLE ABMultiValueEntry (parent_id INTEGER, key INTEGER, value TEXT, UNIQUE(parent_id, key));"
        "CREATE TABLE ABMultiValueEntryKey (value TEXT, UNIQUE(value));"
        "CREATE TABLE ABMultiValueLabel (value TEXT, UNIQUE(value));"
        "CREATE TABLE ABPerson (ROWID INTEGER PRIMARY KEY AUTOINCREMENT, First TEXT, Last TEXT, Middle TEXT, FirstPhonetic TEXT, MiddlePhonetic TEXT, LastPhonetic TEXT, Organization TEXT, Department TEXT, Note TEXT, Kind INTEGER, Birthday TEXT, JobTitle TEXT, Nickname TEXT, Prefix TEXT, Suffix TEXT, FirstSort TEXT, LastSort TEXT, CreationDate INTEGER, ModificationDate INTEGER, CompositeNameFallback TEXT);"
        "INSERT INTO ABMultiValueLabel VALUES('_$!<Mobile>!$_');"
        "INSERT INTO ABMultiValueLabel VALUES('_$!<Home>!$_');"
        "INSERT INTO ABMultiValueLabel VALUES('_$!<Work>!$_');"
        "INSERT INTO ABMultiValueEntryKey VALUES('CountryCode');"
        "INSERT INTO ABMultiValueEntryKey VALUES('City');"
        "INSERT INTO ABMultiValueEntryKey VALUES('Street');"
        "INSERT INTO ABMultiValueEntryKey VALUES('State');"
        "INSERT INTO ABMultiValueEntryKey VALUES('ZIP');"
        "INSERT INTO ABMultiValueEntryKey VALUES('Country');"
        "COMMIT;"
        ;
}

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
    PERSON_NOTE,
    PERSON_BIRTHDAY,
    PERSON_JOBTITLE,
    PERSON_NICKNAME,

    LAST_COL
};

const SQLiteSyncSource::Mapping *SQLiteContactSource::getConstMapping()
{
    static const Mapping mapping[LAST_COL] = {
        { "Last", "ABPerson" },
        { "Middle", "ABPerson" },
        { "First", "ABPerson" },
        { "Prefix", "ABPerson" },
        { "Suffix", "ABPerson" },
        { "FirstSort", "ABPerson" },
        { "LastSort", "ABPerson" },
        { "Organization", "ABPerson" },
        { "Department", "ABPerson" },
        { "Note", "ABPerson", "NOTE" },
        { "Birthday", "ABPerson", "BIRTHDAY" },
        { "JobTitle", "ABPerson", "ROLE" },
        { "Nickname", "ABPerson", "NICKNAME" }
    };

    return mapping;
}

void SQLiteContactSource::open()
{
    SQLiteSyncSource::open();

    // query database for certain constant indices
    m_addrCountryCode = findKey("ABMultiValueEntryKey", "value", "CountryCode");
    m_addrCity = findKey("ABMultiValueEntryKey", "value", "City");
    m_addrStreet = findKey("ABMultiValueEntryKey", "value", "Street");
    m_addrState = findKey("ABMultiValueEntryKey", "value", "State");
    m_addrZIP = findKey("ABMultiValueEntryKey", "value", "ZIP");
    m_typeMobile = findKey("ABMultiValueLabel", "value", "_$!<Mobile>!$_");
    m_typeHome = findKey("ABMultiValueLabel", "value", "_$!<Home>!$_");
    m_typeWork = findKey("ABMultiValueLabel", "value", "_$!<Work>!$_");
}


void SQLiteContactSource::beginSyncThrow(bool needAll,
                                         bool needPartial,
                                         bool deleteLocal)
{
    syncml_time_t lastSyncTime = anchorToTimestamp(getLastAnchor());

    eptr<sqlite3_stmt> all(prepareSQL("SELECT ROWID, CreationDate, ModificationDate FROM ABPerson;"));
    while (checkSQL(sqlite3_step(all)) == SQLITE_ROW) {
        string uid = toString(SQLITE3_COLUMN_KEY(all, 0));
        m_allItems.addItem(uid);

        // find new and updated items by comparing their creation resp. modification time stamp
        // against the end of the last sync
        if (needPartial) {
            syncml_time_t creationTime = getTimeColumn(all, 1);
            syncml_time_t modTime = getTimeColumn(all, 2);

            if (creationTime > lastSyncTime) {
                m_newItems.addItem(uid);
            } else if (modTime > lastSyncTime) {
                m_updatedItems.addItem(uid);
            }
        }
    }

    // TODO: find deleted items

    // all modifications from now on will be rolled back on an error:
    // if the server does the same, theoretically client and server
    // could restart with a two-way sync
    //
    // TODO: currently syncevolution resets the last anchor in case of
    // a failure and thus forces a slow sync - avoid that for SQLite
    // database sources
    eptr<sqlite3_stmt> start(prepareSQL("BEGIN TRANSACTION;"));
    checkSQL(sqlite3_step(start));


    if (deleteLocal) {
        for (itemList::const_iterator it = m_allItems.begin();
             it != m_allItems.end();
             it++) {
            deleteItemThrow(*it);
        }
        m_allItems.clear();
    }
}

void SQLiteContactSource::endSyncThrow()
{
    // complete the transaction started in beginSyncThrow()
    eptr<sqlite3_stmt> end(prepareSQL("COMMIT;"));
    checkSQL(sqlite3_step(end));
}

void SQLiteContactSource::exportData(ostream &out)
{
    eptr<sqlite3_stmt> all(prepareSQL("SELECT ROWID FROM ABPerson;"));
    while (checkSQL(sqlite3_step(all)) == SQLITE_ROW) {
        string uid = toString(SQLITE3_COLUMN_KEY(all, 1));
        auto_ptr<SyncItem> item(createItem(uid, SYNC_STATE_NONE));

        out << item->getData();
        out << "\n";
    }
}

SyncItem *SQLiteContactSource::createItem( const string &uid, SyncState state )
{
    logItem(uid, "extracting from database");

    eptr<sqlite3_stmt> contact(prepareSQL("SELECT * FROM ABPerson WHERE ROWID = '%s';", uid.c_str()));
    if (checkSQL(sqlite3_step(contact)) != SQLITE_ROW) {
        throw runtime_error(string(getName()) + ": contact not found: " + uid);
    }

    VObject vobj;
    string tmp;
    const unsigned char *text;

    vobj.addProperty("BEGIN", "VCARD");
    vobj.addProperty("VERSION", "2.1");
    vobj.setVersion("2.1");

    tmp = getTextColumn(contact, m_mapping[PERSON_LAST].colindex);
    tmp += VObject::SEMICOLON_REPLACEMENT;
    tmp += getTextColumn(contact, m_mapping[PERSON_MIDDLE].colindex);
    tmp += VObject::SEMICOLON_REPLACEMENT;
    tmp += getTextColumn(contact, m_mapping[PERSON_FIRST].colindex);
    if (tmp.size() > 2) {
        vobj.addProperty("N", tmp.c_str());
    }

    tmp = getTextColumn(contact, m_mapping[PERSON_ORGANIZATION].colindex);
    tmp += VObject::SEMICOLON_REPLACEMENT;
    tmp += getTextColumn(contact, m_mapping[PERSON_DEPARTMENT].colindex);
    if (tmp.size() > 1) {
        vobj.addProperty("ORG", tmp.c_str());
    }
 
    rowToVObject(contact, vobj);

    vobj.addProperty("END", "VCARD");

    vobj.fromNativeEncoding();

    arrayptr<char> finalstr(vobj.toString(), "VOCL string");
    LOG.debug("%s", (char *)finalstr);

    auto_ptr<SyncItem> item( new SyncItem( uid.c_str() ) );
    item->setData( (char *)finalstr, strlen(finalstr) );
    item->setDataType( getMimeType() );
    item->setModificationTime( 0 );
    item->setState( state );

    return item.release();
}

int SQLiteContactSource::addItemThrow(SyncItem& item)
{
    return insertItemThrow(item, NULL, "");
}

int SQLiteContactSource::updateItemThrow(SyncItem& item)
{
    // Make sure that there is no contact with this uid,
    // then insert the new data. If there was no such uid,
    // then this behaves like an add.
    string creationTime = findColumn("ABPerson", "ROWID", item.getKey(), "CreationDate", "");
    deleteItemThrow(item.getKey());
    return insertItemThrow(item, item.getKey(), creationTime);
}

int SQLiteContactSource::insertItemThrow(SyncItem &item, const char *uid, const string &creationTime)
{
    string data(getData(item));
    std::auto_ptr<VObject> vobj(VConverter::parse((char *)data.c_str()));
    if (vobj.get() == 0) {
        throwError(string("parsing contact ") + item.getKey());
    }
    vobj->toNativeEncoding();

    stringstream cols;
    stringstream values;
    VProperty *prop;

    // always set the name, even if not in the vcard
    prop = vobj->getProperty("N");
    string first, middle, last, prefix, suffix, firstsort, lastsort;
    if (prop && prop->getValue()) {
        string fn = prop->getValue();
        size_t sep1 = fn.find(VObject::SEMICOLON_REPLACEMENT);
        size_t sep2 = sep1 == fn.npos ? fn.npos : fn.find(VObject::SEMICOLON_REPLACEMENT, sep1 + 1);
        size_t sep3 = sep2 == fn.npos ? fn.npos : fn.find(VObject::SEMICOLON_REPLACEMENT, sep2 + 1);
        size_t sep4 = sep3 == fn.npos ? fn.npos : fn.find(VObject::SEMICOLON_REPLACEMENT, sep3 + 1);

        last = fn.substr(0, sep1);
        if (sep1 != fn.npos) {
            first = fn.substr(sep1 + 1, (sep2 == fn.npos) ? fn.npos : sep2 - sep1 - 1);
        }
        if (sep2 != fn.npos) {
            middle = fn.substr(sep2 + 1, (sep3 == fn.npos) ? fn.npos : sep3 - sep2 - 1);
        }
        if (sep3 != fn.npos) {
            prefix = fn.substr(sep3 + 1, (sep4 == fn.npos) ? fn.npos : sep4 - sep3 - 1);
        }
        if (sep4 != fn.npos) {
            suffix = fn.substr(sep4 + 1);
        }
    }
    cols << m_mapping[PERSON_FIRST].colname << ", " <<
        m_mapping[PERSON_MIDDLE].colname << ", " <<
        m_mapping[PERSON_LAST].colname << ", " <<
        m_mapping[PERSON_LASTSORT].colname << ", " <<
        m_mapping[PERSON_FIRSTSORT].colname;
    values << "?, ?, ?, ?, ?";

    // synthesize sort keys: upper case with specific order of first/last name
    firstsort = first + " " + last;
    transform(firstsort.begin(), firstsort.end(), firstsort.begin(), ::toupper);
    lastsort = last + " " + first;
    transform(lastsort.begin(), lastsort.end(), lastsort.begin(), ::toupper);

    // optional fixed UID, potentially fixed creation time
    if (uid) {
        cols << ", ROWID";
        values << ", ?";
    }
    cols << ", CreationDate, ModificationDate";
    values << ", ?, ?";

    eptr<sqlite3_stmt> insert(prepareSQL("INSERT INTO ABPerson( %s ) "
                                         "VALUES( %s );",
                                         cols.str().c_str(),
                                         values.str().c_str()));

    // now bind parameter values in the same order as the columns specification above
    int param = 1;
    checkSQL(sqlite3_bind_text(insert, param++, first.c_str(), -1, SQLITE_TRANSIENT));
    checkSQL(sqlite3_bind_text(insert, param++, middle.c_str(), -1, SQLITE_TRANSIENT));
    checkSQL(sqlite3_bind_text(insert, param++, last.c_str(), -1, SQLITE_TRANSIENT));
    checkSQL(sqlite3_bind_text(insert, param++, lastsort.c_str(), -1, SQLITE_TRANSIENT));
    checkSQL(sqlite3_bind_text(insert, param++, firstsort.c_str(), -1, SQLITE_TRANSIENT));
    if (uid) {
        checkSQL(sqlite3_bind_text(insert, param++, uid, -1, SQLITE_TRANSIENT));
        checkSQL(sqlite3_bind_text(insert, param++, creationTime.c_str(), -1, SQLITE_TRANSIENT));
    } else {
        checkSQL(sqlite3_bind_int64(insert, param++, (long long)time(NULL)));
    }
    checkSQL(sqlite3_bind_int64(insert, param++, (long long)time(NULL)));

    checkSQL(sqlite3_step(insert));

    if (!uid) {
        // figure out which UID was assigned to the new contact
        string newuid = findColumn("SQLITE_SEQUENCE", "NAME", "ABPerson", "SEQ", "");
        item.setKey(newuid.c_str());
    }

    return STC_OK;
}


int SQLiteContactSource::deleteItemThrow(const string &uid)
{
    int status = STC_OK;

    // delete address field members of contact
    eptr<sqlite3_stmt> del(prepareSQL("DELETE FROM ABMultiValueEntry "
                                      "WHERE ABMultiValueEntry.parent_id IN "
                                      "(SELECT ABMultiValue.uid FROM ABMultiValue WHERE "
                                      " ABMultiValue.record_id = ?);"));
    checkSQL(sqlite3_bind_text(del, 1, uid.c_str(), -1, SQLITE_TRANSIENT));
    checkSQL(sqlite3_step(del));

    // delete addresses and emails of contact
    del.set(prepareSQL("DELETE FROM ABMultiValue WHERE "
                       "ABMultiValue.record_id = ?;"));
    checkSQL(sqlite3_bind_text(del, 1, uid.c_str(), -1, SQLITE_TRANSIENT));
    checkSQL(sqlite3_step(del));

    // now delete the contact itself
    del.set(prepareSQL("DELETE FROM ABPerson WHERE "
                       "ABPerson.ROWID = ?;"));
    checkSQL(sqlite3_bind_text(del, 1, uid.c_str(), -1, SQLITE_TRANSIENT));
    checkSQL(sqlite3_step(del));

    return status;
}

int SQLiteContactSource::deleteItemThrow(SyncItem& item)
{
    return deleteItemThrow(item.getKey());
}

void SQLiteContactSource::logItem(const string &uid, const string &info, bool debug)
{
    if (LOG.getLevel() >= (debug ? LOG_LEVEL_DEBUG : LOG_LEVEL_INFO)) {
        (LOG.*(debug ? &Log::debug : &Log::info))("%s: %s %s",
                                                  getName(),
                                                  findColumn("ABPerson",
                                                             "ROWID",
                                                             uid.c_str(),
                                                             "FirstSort",
                                                             uid.c_str()).c_str(),
                                                  info.c_str());
    }
}

void SQLiteContactSource::logItem(SyncItem &item, const string &info, bool debug)
{
    if (LOG.getLevel() >= (debug ? LOG_LEVEL_DEBUG : LOG_LEVEL_INFO)) {
        string data(getData(item));

        // avoid pulling in a full vcard parser by just searching for a specific property,
        // FN in this case
        string name = "???";
        string prop = "\nFN:";
        size_t start = data.find(prop);
        if (start != data.npos) {
            start += prop.size();
            size_t end = data.find("\n", start);
            if (end != data.npos) {
                name = data.substr(start, end - start);
            }
        }

        (LOG.*(debug ? &Log::debug : &Log::info))("%s: %s %s",
                                                  getName(),
                                                  name.c_str(),
                                                  info.c_str());
    }
}


#ifdef ENABLE_MODULES

extern "C" EvolutionSyncSource *SyncEvolutionCreateSource(const string &name,
                                                          SyncSourceConfig *sc,
                                                          const string &changeId,
                                                          const string &id,
                                                          const string &mimeType)
{
    return new SQLiteContactSource(name, sc, changeId, id);
}

#endif /* ENABLE_MODULES */

#endif /* ENABLE_SQLITE */
