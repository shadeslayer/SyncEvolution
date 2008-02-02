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

void SQLiteContactSource::open()
{
    static const SQLiteUtil::Mapping mapping[LAST_COL + 1] = {
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
        { "Nickname", "ABPerson", "NICKNAME" },
        { NULL }
    };
    static const char *schema = 
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
        "COMMIT;";

    m_sqlite.open(getName(),
                  m_id,
                  mapping,
                  schema);

    // query database for certain constant indices
    m_addrCountryCode = m_sqlite.findKey("ABMultiValueEntryKey", "value", "CountryCode");
    m_addrCity = m_sqlite.findKey("ABMultiValueEntryKey", "value", "City");
    m_addrStreet = m_sqlite.findKey("ABMultiValueEntryKey", "value", "Street");
    m_addrState = m_sqlite.findKey("ABMultiValueEntryKey", "value", "State");
    m_addrZIP = m_sqlite.findKey("ABMultiValueEntryKey", "value", "ZIP");
    m_typeMobile = m_sqlite.findKey("ABMultiValueLabel", "value", "_$!<Mobile>!$_");
    m_typeHome = m_sqlite.findKey("ABMultiValueLabel", "value", "_$!<Home>!$_");
    m_typeWork = m_sqlite.findKey("ABMultiValueLabel", "value", "_$!<Work>!$_");
}

void SQLiteContactSource::close()
{
    m_sqlite.close();
}

void SQLiteContactSource::listAllItems(RevisionMap_t &revisions)
{
    eptr<sqlite3_stmt> all(m_sqlite.prepareSQL("SELECT ROWID, CreationDate, ModificationDate FROM ABPerson;"));
    while (m_sqlite.checkSQL(sqlite3_step(all)) == SQLITE_ROW) {
        string uid = m_sqlite.toString(SQLITE3_COLUMN_KEY(all, 0));
        string modTime = m_sqlite.time2str(m_sqlite.getTimeColumn(all, 2));
        revisions.insert(RevisionMap_t::value_type(uid, modTime));
    }
}

SyncItem *SQLiteContactSource::createItem(const string &uid)
{
    logItem(uid, "extracting from database", true);

    eptr<sqlite3_stmt> contact(m_sqlite.prepareSQL("SELECT * FROM ABPerson WHERE ROWID = '%s';", uid.c_str()));
    if (m_sqlite.checkSQL(sqlite3_step(contact)) != SQLITE_ROW) {
        throw runtime_error(string(getName()) + ": contact not found: " + uid);
    }

    VObject vobj;
    string tmp;
    const unsigned char *text;

    vobj.addProperty("BEGIN", "VCARD");
    vobj.addProperty("VERSION", "2.1");
    vobj.setVersion("2.1");

    tmp = m_sqlite.getTextColumn(contact, m_sqlite.getMapping(PERSON_LAST).colindex);
    tmp += VObject::SEMICOLON_REPLACEMENT;
    tmp += m_sqlite.getTextColumn(contact, m_sqlite.getMapping(PERSON_MIDDLE).colindex);
    tmp += VObject::SEMICOLON_REPLACEMENT;
    tmp += m_sqlite.getTextColumn(contact, m_sqlite.getMapping(PERSON_FIRST).colindex);
    if (tmp.size() > 2) {
        vobj.addProperty("N", tmp.c_str());
    }

    tmp = m_sqlite.getTextColumn(contact, m_sqlite.getMapping(PERSON_ORGANIZATION).colindex);
    tmp += VObject::SEMICOLON_REPLACEMENT;
    tmp += m_sqlite.getTextColumn(contact, m_sqlite.getMapping(PERSON_DEPARTMENT).colindex);
    if (tmp.size() > 1) {
        vobj.addProperty("ORG", tmp.c_str());
    }
 
    m_sqlite.rowToVObject(contact, vobj);
    vobj.addProperty("END", "VCARD");
    vobj.fromNativeEncoding();

    arrayptr<char> finalstr(vobj.toString(), "VOCL string");
    LOG.debug("%s", (char *)finalstr);

    auto_ptr<SyncItem> item( new SyncItem( uid.c_str() ) );
    item->setData( (char *)finalstr, strlen(finalstr) );
    item->setDataType( getMimeType() );
    item->setModificationTime( 0 );

    return item.release();
}

string SQLiteContactSource::insertItem(string &uid, const SyncItem &item)
{
    string creationTime;
    std::auto_ptr<VObject> vobj(VConverter::parse((char *)((SyncItem &)item).getData()));
    if (vobj.get() == 0) {
        throwError(string("parsing contact ") + ((SyncItem &)item).getKey());
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
            middle = fn.substr(sep1 + 1, (sep2 == fn.npos) ? fn.npos : sep2 - sep1 - 1);
        }
        if (sep2 != fn.npos) {
            first = fn.substr(sep2 + 1, (sep3 == fn.npos) ? fn.npos : sep3 - sep2 - 1);
        }
        if (sep3 != fn.npos) {
            prefix = fn.substr(sep3 + 1, (sep4 == fn.npos) ? fn.npos : sep4 - sep3 - 1);
        }
        if (sep4 != fn.npos) {
            suffix = fn.substr(sep4 + 1);
        }
    }
    cols << m_sqlite.getMapping(PERSON_FIRST).colname << ", " <<
        m_sqlite.getMapping(PERSON_MIDDLE).colname << ", " <<
        m_sqlite.getMapping(PERSON_LAST).colname << ", " <<
        m_sqlite.getMapping(PERSON_LASTSORT).colname << ", " <<
        m_sqlite.getMapping(PERSON_FIRSTSORT).colname;
    values << "?, ?, ?, ?, ?";

    // synthesize sort keys: upper case with specific order of first/last name
    firstsort = first + " " + last;
    transform(firstsort.begin(), firstsort.end(), firstsort.begin(), ::toupper);
    lastsort = last + " " + first;
    transform(lastsort.begin(), lastsort.end(), lastsort.begin(), ::toupper);

    // optional fixed UID, potentially fixed creation time
    if (uid.size()) {
        creationTime = m_sqlite.findColumn("ABPerson", "ROWID", uid.c_str(), "CreationDate", "");
        cols << ", ROWID";
        values << ", ?";
    }
    cols << ", CreationDate, ModificationDate";
    values << ", ?, ?";

    // delete complete row so that we can recreate it
    if (uid.size()) {
        eptr<sqlite3_stmt> remove(m_sqlite.prepareSQL("DELETE FROM ABPerson WHERE ROWID == ?;"));
        m_sqlite.checkSQL(sqlite3_bind_text(remove, 1, uid.c_str(), -1, SQLITE_TRANSIENT));
        m_sqlite.checkSQL(sqlite3_step(remove));
    }

    string cols_str = cols.str();
    string values_str = values.str();
    eptr<sqlite3_stmt> insert(m_sqlite.prepareSQL("INSERT INTO ABPerson( %s ) "
                                                  "VALUES( %s );",
                                                  cols_str.c_str(),
                                                  values_str.c_str()));

    // now bind parameter values in the same order as the columns specification above
    int param = 1;
    m_sqlite.checkSQL(sqlite3_bind_text(insert, param++, first.c_str(), -1, SQLITE_TRANSIENT));
    m_sqlite.checkSQL(sqlite3_bind_text(insert, param++, middle.c_str(), -1, SQLITE_TRANSIENT));
    m_sqlite.checkSQL(sqlite3_bind_text(insert, param++, last.c_str(), -1, SQLITE_TRANSIENT));
    m_sqlite.checkSQL(sqlite3_bind_text(insert, param++, lastsort.c_str(), -1, SQLITE_TRANSIENT));
    m_sqlite.checkSQL(sqlite3_bind_text(insert, param++, firstsort.c_str(), -1, SQLITE_TRANSIENT));
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
        uid = m_sqlite.findColumn("SQLITE_SEQUENCE", "NAME", "ABPerson", "SEQ", "");
    }
    return m_sqlite.time2str(modificationTime);
}


void SQLiteContactSource::deleteItem(const string &uid)
{
    // delete address field members of contact
    eptr<sqlite3_stmt> del(m_sqlite.prepareSQL("DELETE FROM ABMultiValueEntry "
                                               "WHERE ABMultiValueEntry.parent_id IN "
                                               "(SELECT ABMultiValue.uid FROM ABMultiValue WHERE "
                                               " ABMultiValue.record_id = ?);"));
    m_sqlite.checkSQL(sqlite3_bind_text(del, 1, uid.c_str(), -1, SQLITE_TRANSIENT));
    m_sqlite.checkSQL(sqlite3_step(del));

    // delete addresses and emails of contact
    del.set(m_sqlite.prepareSQL("DELETE FROM ABMultiValue WHERE "
                                "ABMultiValue.record_id = ?;"));
    m_sqlite.checkSQL(sqlite3_bind_text(del, 1, uid.c_str(), -1, SQLITE_TRANSIENT));
    m_sqlite.checkSQL(sqlite3_step(del));

    // now delete the contact itself
    del.set(m_sqlite.prepareSQL("DELETE FROM ABPerson WHERE "
                                "ABPerson.ROWID = ?;"));
    m_sqlite.checkSQL(sqlite3_bind_text(del, 1, uid.c_str(), -1, SQLITE_TRANSIENT));
    m_sqlite.checkSQL(sqlite3_step(del));
}

void SQLiteContactSource::flush()
{
    // Our change tracking is time based.
    // Don't let caller proceed without waiting for
    // one second to prevent being called again before
    // the modification time stamp is larger than it
    // is now.
    sleep(1);
}

void SQLiteContactSource::logItem(const string &uid, const string &info, bool debug)
{
    if (LOG.getLevel() >= (debug ? LOG_LEVEL_DEBUG : LOG_LEVEL_INFO)) {
        (LOG.*(debug ? &Log::debug : &Log::info))("%s: %s %s",
                                                  getName(),
                                                  m_sqlite.findColumn("ABPerson",
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
