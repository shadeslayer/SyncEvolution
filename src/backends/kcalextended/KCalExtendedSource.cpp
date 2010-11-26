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

#ifdef ENABLE_KCALEXTENDED

#include "KCalExtendedSource.h"

#include <QCoreApplication>

#include <event.h>
#include <journal.h>
#include <extendedcalendar.h>
#include <extendedstorage.h>
#include <sqlitestorage.h>
#include <icalformat.h>
#include <memorycalendar.h>

#include <syncevo/SmartPtr.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

/**
 * All std::string and plain C strings in SyncEvolution are in UTF-8.
 * QString must be told about that explicitly.
 */
static QString std2qstring(const std::string &str) { return QString::fromUtf8(str.c_str()); }
// static QString std2qstring(const char *str) { return QString::fromUtf8(str); }

/**
 * Convert back to UTF-8.
 */
static std::string qstring2std(const QString str) { return str.toUtf8().constData(); }

class KCalExtendedData
{
    KCalExtendedSource *m_parent;
    bool m_modified;
    QString m_notebook;
    QString m_notebookUID;
    KCalCore::IncidenceBase::IncidenceType m_type;

    mKCal::ExtendedCalendar::Ptr m_calendar;
    mKCal::ExtendedStorage::Ptr m_storage;

public:
    KCalExtendedData(KCalExtendedSource *parent,
                     const QString &notebook,
                     const KCalCore::IncidenceBase::IncidenceType &type) :
        m_parent(parent),
        m_modified(false),
        m_notebook(notebook),
        m_type(type)
    {
        if (!qApp) {
            static const char *argv[] = { "SyncEvolution" };
            static int argc = 1;
            new QCoreApplication(argc, (char **)argv);
        }
    }

    void extractIncidences(KCalCore::Incidence::List &incidences,
                           SyncSourceChanges::State state,
                           SyncSourceChanges &changes)
    {
        foreach (KCalCore::Incidence::Ptr incidence, incidences) {
            if (incidence->type() == m_type) {
                changes.addItem(getItemID(incidence).getLUID(),
                                state);
            }
        }
    }

    /**
     * An item is identified in the calendar by
     * its UID (unique ID) and RID (recurrence ID).
     * The RID may be empty.
     *
     * This is turned into a SyncML LUID by
     * concatenating them: <uid>-rid<rid>.
     */
    class ItemID {
    public:
    ItemID(const string &uid, const string &rid) :
        m_uid(uid),
            m_rid(rid)
            {}
    ItemID(const char *uid, const char *rid):
        m_uid(uid ? uid : ""),
            m_rid(rid ? rid : "")
                {}
    ItemID(const string &luid);

        const string m_uid, m_rid;

        QString getIDString() const { return std2qstring(m_uid); }
        KDateTime getDateTime() const { return KDateTime::fromString(std2qstring(m_rid)); }

        string getLUID() const;
        static string getLUID(const string &uid, const string &rid);
    };

    ItemID getItemID(const KCalCore::Incidence::Ptr &incidence);
    KCalCore::Incidence::Ptr findIncidence(const string &luid);

    friend class KCalExtendedSource;
};

string KCalExtendedData::ItemID::getLUID() const
{
    return getLUID(m_uid, m_rid);
}

string KCalExtendedData::ItemID::getLUID(const string &uid, const string &rid)
{
    return uid + "-rid" + rid;
}

KCalExtendedData::ItemID::ItemID(const string &luid)
{
    size_t ridoff = luid.rfind("-rid");
    if (ridoff != luid.npos) {
        const_cast<string &>(m_uid) = luid.substr(0, ridoff);
        const_cast<string &>(m_rid) = luid.substr(ridoff + strlen("-rid"));
    } else {
        const_cast<string &>(m_uid) = luid;
    }
}

KCalCore::Incidence::Ptr KCalExtendedData::findIncidence(const string &luid)
{
    ItemID id(luid);
    QString uid = id.getIDString();
    KDateTime rid = id.getDateTime();
    // if (!m_storage->load(uid, rid)) {
    // m_parent->throwError(string("failed to load incidence ") + luid);
    // }
    KCalCore::Incidence::Ptr incidence = m_calendar->incidence(uid, rid);
    return incidence;
}

KCalExtendedData::ItemID KCalExtendedData::getItemID(const KCalCore::Incidence::Ptr &incidence)
{
    QString uid = incidence->uid();
    KDateTime rid = incidence->recurrenceId();
    string ridStr;
    if (rid.isValid()) {
        ridStr = qstring2std(rid.toString());
    }
    return ItemID(qstring2std(uid), ridStr);
}

KCalExtendedSource::KCalExtendedSource(const SyncSourceParams &params) :
    TestingSyncSource(params)
{
    SyncSourceRevisions::init(this, this, 0, m_operations);
    SyncSourceLogging::init(InitList<std::string>("SUMMARY") + "LOCATION",
                            ", ",
                            m_operations);
#if 0
    // VTODO
    SyncSourceLogging::init(InitList<std::string>("SUMMARY"),
                            ", ",
                            m_operations);
    // VJOURNAL
    SyncSourceLogging::init(InitList<std::string>("SUBJECT"),
                            ", ",
                            m_operations);
#endif

    m_data = NULL;
}

KCalExtendedSource::~KCalExtendedSource()
{
    delete m_data;
}

void KCalExtendedSource::open()
{
    // TODO: also support todoType
    m_data = new KCalExtendedData(this, getDatabaseID(),
                                  KCalCore::IncidenceBase::TypeEvent);
    m_data->m_calendar = mKCal::ExtendedCalendar::Ptr(new mKCal::ExtendedCalendar(KDateTime::Spec::LocalZone()));

    // read specified database name from evolutionsource property
    const char *databaseID = getDatabaseID();

    if ( strcmp(databaseID, "" ) == 0 || boost::starts_with(databaseID, "file://") ) {
        // if databaseID is empty, create default storage at default location
        // else if databaseID has a "file://" prefix, create storage at the specified place
        // use default notebook in default storage
        if ( boost::starts_with(databaseID, "file://") ) {
            mKCal::SqliteStorage::Ptr ss(new mKCal::SqliteStorage(m_data->m_calendar, QString(databaseID + 7), false));
            m_data->m_storage = ss.staticCast<mKCal::ExtendedStorage>();
        } else {
            m_data->m_storage = mKCal::ExtendedCalendar::defaultStorage(m_data->m_calendar);
        }
        if (!m_data->m_storage->open()) {
            throwError("failed to open storage");
        }
        mKCal::Notebook::Ptr defaultNotebook = m_data->m_storage->defaultNotebook();
        if (!defaultNotebook) {
            throwError("no default Notebook");
        }
        m_data->m_notebookUID = defaultNotebook->uid();
    } else {
        // use databaseID as notebook name to search for an existing notebook
        // if found use it, otherwise:
        // 1) with "SyncEvolution_Test_" prefix, create a new notebook with given name and add it to default storage
        // 2) without a special prefix, throw an error
        m_data->m_storage = mKCal::ExtendedCalendar::defaultStorage(m_data->m_calendar);
        if (!m_data->m_storage->open()) {
            throwError("failed to open storage");
        }
        QString name = databaseID;
        mKCal::Notebook::Ptr notebook;
        mKCal::Notebook::List notebookList = m_data->m_storage->notebooks();
        mKCal::Notebook::List::Iterator it;

        for ( it = notebookList.begin(); it != notebookList.end(); ++it ) {
            if ( name == (*it)->name() ) {
                break;
            }
        }
        if ( it == notebookList.end() ) {
            if ( boost::starts_with(databaseID, "SyncEvolution_Test_") ) {
                notebook = mKCal::Notebook::Ptr ( new mKCal::Notebook(QString(), name, QString(), QString(), false, true, false, false,true) );
                if ( !notebook ) {
                    throwError("failed to create notebook");
                }
                m_data->m_storage->addNotebook(notebook, false);
            } else {
                throwError(string("no such notebook with name \"") + string(databaseID) + string("\" in default storage"));
            }
        } else {
            notebook = *it;
        }
        m_data->m_notebookUID = notebook->uid();
    }

    // we are not currently using partial loading because there were
    // issues with it (BMC #6061); the load() calls elsewhere in this
    // file are commented out
    if (!m_data->m_storage->load()) {
        throwError("failed to load calendar");
    }
}

bool KCalExtendedSource::isEmpty()
{
    return false;
}

void KCalExtendedSource::close()
{
    if (m_data->m_storage) {
        m_data->m_storage->close();
    }
    if (m_data->m_calendar) {
        m_data->m_calendar->close();
    }
}

void KCalExtendedSource::enableServerMode()
{
    SyncSourceAdmin::init(m_operations, this);
    SyncSourceBlob::init(m_operations, getCacheDir());
}

bool KCalExtendedSource::serverModeEnabled() const
{
    return m_operations.m_loadAdminData;
}

KCalExtendedSource::Databases KCalExtendedSource::getDatabases()
{
    Databases result;

    m_data = new KCalExtendedData(this, getDatabaseID(),
                                  KCalCore::IncidenceBase::TypeEvent);
    m_data->m_calendar = mKCal::ExtendedCalendar::Ptr(new mKCal::ExtendedCalendar(KDateTime::Spec::LocalZone()));
    m_data->m_storage = mKCal::ExtendedCalendar::defaultStorage(m_data->m_calendar);
    if (!m_data->m_storage->open()) {
        throwError("failed to open storage");
    }
    mKCal::Notebook::List notebookList = m_data->m_storage->notebooks();
    mKCal::Notebook::List::Iterator it;
    for ( it = notebookList.begin(); it != notebookList.end(); ++it ) {
        bool isDefault = (*it)->isDefault();
        result.push_back(Database( (*it)->name().toStdString(), 
                                   (m_data->m_storage).staticCast<mKCal::SqliteStorage>()->databaseName().toStdString(), 
                                   isDefault));
    }
    m_data->m_storage->close();
    m_data->m_calendar->close();
    return result;
}

void KCalExtendedSource::beginSync(const std::string &lastToken, const std::string &resumeToken)
{
    const char *anchor = resumeToken.empty() ? lastToken.c_str() : resumeToken.c_str();
    KCalCore::Incidence::List incidences;
    // return all items
    if (!m_data->m_storage->allIncidences(&incidences, m_data->m_notebookUID)) {
        throwError("allIncidences() failed");
    }
    m_data->extractIncidences(incidences, SyncSourceChanges::ANY, *this);
    if (*anchor) {
        KDateTime endSyncTime(QDateTime::fromString(QString(anchor), Qt::ISODate));
        KCalCore::Incidence::List added, modified, deleted;
        if (!m_data->m_storage->insertedIncidences(&added, endSyncTime, m_data->m_notebookUID)) {
            throwError("insertedIncidences() failed");
        }
        if (!m_data->m_storage->modifiedIncidences(&modified, endSyncTime, m_data->m_notebookUID)) {
            throwError("modifiedIncidences() failed");
        }
        if (!m_data->m_storage->deletedIncidences(&deleted, endSyncTime, m_data->m_notebookUID)) {
            throwError("deletedIncidences() failed");
        }
        // It is guaranteed that modified and inserted items are
        // returned as inserted, so no need to check that.
        m_data->extractIncidences(added, SyncSourceChanges::NEW, *this);
        m_data->extractIncidences(modified, SyncSourceChanges::UPDATED, *this);
        m_data->extractIncidences(deleted, SyncSourceChanges::DELETED, *this);
    }
}

std::string KCalExtendedSource::endSync(bool success)
{
    if (m_data->m_modified) {
        if (!m_data->m_storage->save()) {
            throwError("could not save calendar");
        }
        time_t modtime = time(NULL);
        // Saving set the modified time stamps of all items needed
        // saving, so ensure that we sleep for one second starting now.
        // Must sleep before taking the time stamp for the anchor,
        // because changes made after and including (>= instead of >) that time
        // stamp will be considered as "changes made after last sync".
        time_t current = modtime;
        do {
            sleep(1 - (current - modtime));
            current = time(NULL);
        } while (current - modtime < 1);
    }

    QDateTime now = QDateTime::currentDateTime().toUTC();
    string anchor(qstring2std(now.toString(Qt::ISODate)));
    return anchor;
}

void KCalExtendedSource::readItem(const string &uid, std::string &item)
{
    KCalCore::Incidence::Ptr incidence(m_data->findIncidence(uid));
    if (!incidence) {
        throwError(string("failure extracting ") + uid);
    }
    KCalCore::Calendar::Ptr calendar(new KCalCore::MemoryCalendar(KDateTime::Spec::LocalZone()));
    calendar->addIncidence(incidence);
    KCalCore::ICalFormat formatter;
    item = qstring2std(formatter.toString(calendar));
}

TestingSyncSource::InsertItemResult KCalExtendedSource::insertItem(const string &uid, const std::string &item)
{
    KCalCore::Calendar::Ptr calendar(new KCalCore::MemoryCalendar(KDateTime::Spec::LocalZone()));
    KCalCore::ICalFormat parser;
    if (!parser.fromString(calendar, std2qstring(item))) {
        throwError("error parsing iCalendar 2.0 item");
    }
    KCalCore::Incidence::List incidences = calendar->rawIncidences();
    if (incidences.empty()) {
        throwError("iCalendar 2.0 item empty?!");
    }
    bool updated;
    string newUID;
    string oldUID = uid;

    // check for existing incidence with this UID and RECURRENCE-ID first,
    // update when found even if caller didn't know about that existing
    // incidence
    if (uid.empty()) {
        QString id = incidences[0]->uid();
        KDateTime rid = incidences[0]->recurrenceId();
        if (!id.isEmpty()) {
            // m_data->m_storage->load(id, rid);
            KCalCore::Incidence::Ptr incidence = m_data->m_calendar->incidence(id, rid);
            if (incidence) {
                oldUID = m_data->getItemID(incidence).getLUID();
            }
        }
    }

    // Brute-force copying of all time zone definitions. Ignores name
    // conflicts, which is something better handled in a generic mKCal
    // API function (BMC #8604).
    KCalCore::ICalTimeZones *source = calendar->timeZones();
    if (source) {
        KCalCore::ICalTimeZones *target = m_data->m_calendar->timeZones();
        if (target) {
            BOOST_FOREACH(const KCalCore::ICalTimeZone &zone, source->zones().values()) {
                target->add(zone);
            }
        }
    }

    if (oldUID.empty()) {
        KCalCore::Incidence::Ptr incidence = incidences[0];

        updated = false;
        if (!m_data->m_calendar->addIncidence(incidence)) {
            throwError("could not add incidence");
        }
        m_data->m_calendar->setNotebook(incidence, m_data->m_notebookUID);
        newUID = m_data->getItemID(incidence).getLUID();
    } else {
        KCalCore::Incidence::Ptr incidence = incidences[0];
        updated = true;
        newUID = oldUID;
        KCalCore::Incidence::Ptr original = m_data->findIncidence(oldUID);
        if (!original) {
            throwError("incidence to be updated not found");
        }
        if (original->type() != incidence->type()) {
            throwError("cannot update incidence, wrong type?!");
        }

        // preserve UID and RECURRENCE-ID, because this must not change
        // and some peers don't preserve it
        incidence->setUid(original->uid());
        if (original->hasRecurrenceId()) {
            incidence->setRecurrenceId(original->recurrenceId());
        }

        // created() corresponds to the CREATED property (= time when
        // item was created in the local storage for the first time),
        // so it can never be modified by our peer and must be
        // preserved unconditionally in updates.
        incidence->setCreated(original->created());

        // now overwrite item in calendar
        (KCalCore::IncidenceBase &)*original = (KCalCore::IncidenceBase &)*incidence;
        m_data->m_calendar->setNotebook(original, m_data->m_notebookUID);
        // no need to save
    }

    m_data->m_modified = true;

    return InsertItemResult(newUID,
                            "",
                            updated);
}


void KCalExtendedSource::deleteItem(const string &uid)
{
    KCalCore::Incidence::Ptr incidence = m_data->findIncidence(uid);
    if (!incidence) {
        // throwError(string("incidence ") + uid + " not found");
        // don't treat this as error, it can happen, for example
        // when the master event was removed before (MBC #6061)
        return;
    }
    if (!m_data->m_calendar->deleteIncidence(incidence)) {
        throwError(string("could not delete incidence") + uid);
    }
    m_data->m_modified = true;
}

void KCalExtendedSource::listAllItems(RevisionMap_t &revisions)
{
    KCalCore::Incidence::List incidences;
    if (!m_data->m_storage->allIncidences(&incidences, m_data->m_notebookUID)) {
        throwError("allIncidences() failed");
    }
    foreach (KCalCore::Incidence::Ptr incidence, incidences) {
        if (incidence->type() == m_data->m_type) {
            revisions[m_data->getItemID(incidence).getLUID()] = "1";
        }
    }
}

std::string KCalExtendedSource::getDescription(const string &luid)
{
    try {
        KCalCore::Incidence::Ptr incidence = m_data->findIncidence(luid);
        if (incidence) {
            list<string> parts;
            QString str;
            // for VEVENT
            str = incidence->summary();
            if (!str.isEmpty()) {
                parts.push_back(qstring2std(str));
            }
            str = incidence->location();
            if (!str.isEmpty()) {
                parts.push_back(qstring2std(str));
            }
            return boost::join(parts, ", ");
        } else {
            return "";
        }
    } catch (...) {
        return "";
    }
}

SE_END_CXX

#endif /* ENABLE_KCALEXTENDED */

#ifdef ENABLE_MODULES
# include "KCalExtendedSourceRegister.cpp"
#endif
