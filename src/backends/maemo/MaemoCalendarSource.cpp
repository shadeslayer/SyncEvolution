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

#include "config.h"

#ifdef ENABLE_MAEMO_CALENDAR

#include "MaemoCalendarSource.h"

#include <CalendarErrors.h>

#include <boost/algorithm/string/case_conv.hpp>

#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <syncevo/util.h>

#include <sstream>
#include <fstream>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

MaemoCalendarSource::MaemoCalendarSource(int EntryType, int EntryFormat,
                                         const SyncSourceParams &params) :
    TrackingSyncSource(params),
    entry_type(EntryType), entry_format(EntryFormat)
{
    mc = CMulticalendar::MCInstance();
    cal = NULL;
    if (!mc) {
        throwError("Could not connect to Maemo Calendar backend");
    }
}

const char *MaemoCalendarSource::getMimeType() const
{
    switch (entry_format) {
    case -1: return "text/plain";
    case ICAL_TYPE: return "text/calendar";
    case VCAL_TYPE: return "text/x-calendar";
    default: return NULL;
    }
}

const char *MaemoCalendarSource::getMimeVersion() const
{
    switch (entry_format) {
    case -1: return "1.0";
    case ICAL_TYPE: return "2.0";
    case VCAL_TYPE: return "1.0";
    default: return NULL;
    }
}

void MaemoCalendarSource::open()
{
    string id = getDatabaseID();
    const string id_prefix("id:");
    int err;

    if (!id.size()) {
        CCalendar *def_cal = mc->getSynchronizedCalendar();
        // generate a new instance of default calendar,
        // which we can safely delete in the close() method
        cal = mc->getCalendarById(def_cal->getCalendarId(), err);
    }
    else if (boost::starts_with(id, id_prefix)) {
        istringstream uri(id.substr(id_prefix.size()));
        int cid;
        uri >> cid;
        cal = mc->getCalendarById(cid, err);
    }
    else {
        // try the calendar's name
        cal = mc->getCalendarByName(id, err);
    }

    if (!cal) {
        throwError(string("not found: ") + id);
    }
    conv = new ICalConverter;
    conv->setSyncing(true); // not sure what this does, but may as well tell the truth
}

bool MaemoCalendarSource::isEmpty()
{
    // TODO: provide a real implementation. Always returning false
    // here disables the "allow slow sync when no local data" heuristic
    // for preventSlowSync=1.
    return false;
}

void MaemoCalendarSource::close()
{
    delete conv;
    conv = NULL;
    delete cal;
    cal = NULL;
    // since timestamps are rounded down to nearest second,
    // sleep until next second, just in case
    sleep(1);
}

MaemoCalendarSource::Databases MaemoCalendarSource::getDatabases()
{
    // getDefaultCalendar returns the Private calendar,
    // getSynchronizedCalendar returns the Main calendar
    // (the same calendar Nokia PC Suite would sync with)
    CCalendar *def_cal = mc->getSynchronizedCalendar();
    int def_id = def_cal->getCalendarId();
    vector< CCalendar * > calendars = mc->getListCalFromMc();
    Databases result;

    BOOST_FOREACH(CCalendar * c, calendars) {
        int id = c->getCalendarId();
        ostringstream uri;
        uri << "id:" << id;
        result.push_back(Database(c->getCalendarName(),
                                  uri.str(),
                                  id == def_id));
    }

    mc->releaseListCalendars(calendars);

    return result;
}

void MaemoCalendarSource::listAllItems(RevisionMap_t &revisions)
{
#if 0 /* this code exposes a bug in calendar-backend, https://bugs.maemo.org/show_bug.cgi?id=8277 */
    // I've found no way to query the last modified time of a component
    // without getting the whole component.
    // This limit should hopefully reduce memory usage of that a bit
    static const int limit = 1024;
    int ofs = 0, err;
    vector< CComponent * > comps;

    for (;;) {
        comps = cal->getComponents(entry_type, -1, -1,
                                   limit, ofs, err);
        // Note that non-success value in "err" is not necessarily fatal,
        // I seem to get a nonspecific "application error" if there are no
        // components of the specified type, so just ignore it for now
        if (!comps.size())
            break;
        BOOST_FOREACH(CComponent * c, comps) {
            revisions[c->getId()] = get_revision(c);
            // Testing shows that the backend doesn't free the memory itself
            delete c;
            ofs++;
        }
    }
#else
    // this avoids the calendar-backend bug, but may use unlimited memory;
    // hopefully the users aren't saving their entire life here!
    int err;
    vector< CComponent * > comps;

    comps = cal->getComponents(entry_type, -1, -1, err);
    BOOST_FOREACH(CComponent * c, comps) {
        revisions[c->getId()] = get_revision(c);
        delete c;
    }
#endif
}

void MaemoCalendarSource::readItem(const string &uid, std::string &item, bool raw)
{
    int err;
    CComponent * c = cal->getEntry(uid, entry_type, err);
    if (!c) {
        throwError(string("retrieving item: ") + uid);
    }
    if (entry_format == -1) {
        item = c->getDescription();
        err = CALENDAR_OPERATION_SUCCESSFUL;
    } else {
        item = conv->localToIcalVcal(c, FileType(entry_format), err);
    }
    delete c;
    if (err != CALENDAR_OPERATION_SUCCESSFUL) {
        throwError(string("generating ical for item: ") + uid);
    }
}

TrackingSyncSource::InsertItemResult MaemoCalendarSource::insertItem(const string &uid, const std::string &item, bool raw)
{
    int err;
    CComponent *c;
    bool r, u = false;
    TrackingSyncSource::InsertItemResult result;

    if (cal->getCalendarType() == BIRTHDAY_CALENDAR) {
        // stubbornly refuse to try this
        throwError(string("can't sync smart calendar ") + cal->getCalendarName());
    }

    if (entry_format == -1) {
        c = new CJournal(item);
        err = CALENDAR_OPERATION_SUCCESSFUL;
    } else {
        vector< CComponent * > comps = conv->icalVcalToLocal(item, FileType(entry_format), err);
        // Note that a non-success value in "err" is not necessarily fatal,
        // I seem to get a nonspecific "application error" on certain types of
        // barely-legal input (mostly on todo entries), yet a component is returned
        if (!comps.size()) {
            if (err != CALENDAR_OPERATION_SUCCESSFUL) {
                throwError(string("parsing ical: ") + item);
            } else {
                throwError(string("no events in ical: ") + item);
            }
        }
        vector< CComponent * >::iterator it = comps.begin();
        if (comps.size() > 1) {
            for (; it != comps.end(); it++) {
                delete (*it);
            }
            throwError(string("too many events in ical: ") + item);
        }
        c = *it;
    }

    // I wish there were public modifyEntry and addEntry methods,
    // so I wouldn't need the switches
    // (using the batch-operation modifyComponents and addComponents methods on
    // individual items would probably be inefficient)
    if (uid.size()) {
        c->setId(uid);
        switch (entry_type) {
        case EVENT:   r = cal->modifyEvent  (static_cast< CEvent   * >(c), err); break;
        case TODO:    r = cal->modifyTodo   (static_cast< CTodo    * >(c), err); break;
        case JOURNAL: r = cal->modifyJournal(static_cast< CJournal * >(c), err); break;
        default: r = false; err = CALENDAR_SYSTEM_ERROR;
        }
        if (!r) {
            throwError(string("updating item ") + uid);
        }
    } else {
        switch (entry_type) {
        case EVENT:   r = cal->addEvent  (static_cast< CEvent   * >(c), err); break;
        case TODO:    r = cal->addTodo   (static_cast< CTodo    * >(c), err); break;
        case JOURNAL: r = cal->addJournal(static_cast< CJournal * >(c), err); break;
        default: r = false; err = CALENDAR_SYSTEM_ERROR;
        }
        if (!r) {
            throwError(string("creating item "));
        }
        if (err == CALENDAR_ENTRY_DUPLICATED) {
            u = true;
        }
    }

    result = InsertItemResult(c->getId(), get_revision(c), u);
    delete c;
    return result;
}


void MaemoCalendarSource::removeItem(const string &uid)
{
    int err;
    cal->deleteComponent(uid, err);
    if (err != CALENDAR_OPERATION_SUCCESSFUL) {
        throwError(string("deleting item: ") + uid);
    }
}

string MaemoCalendarSource::get_revision(CComponent * c)
{
    time_t mtime = c->getLastModified();

    ostringstream revision;
    revision << mtime;

    return revision.str();
}

SE_END_CXX

#endif /* ENABLE_MAEMO_CALENDAR */

#ifdef ENABLE_MODULES
# include "MaemoCalendarSourceRegister.cpp"
#endif
