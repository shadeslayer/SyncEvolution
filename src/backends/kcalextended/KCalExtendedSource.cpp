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

#include <QApplication>

#include <kcal/event.h>
#include <kcal/journal.h>
#include <kcal/extendedcalendar.h>
#include <kcal/extendedstorage.h>
#include <kcal/icalformat.h>

#include <syncevo/SmartPtr.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

static const QString eventType("Event");
static const QString todoType("Todo");

class KCalExtendedData
{
    KCalExtendedSource *m_parent;
    QString m_notebook;
    QString m_notebookUID;
    QString m_type;

    // needed when using Qt code
    static QApplication *m_app;

    cxxptr<KCal::ExtendedCalendar> m_calendar;
    cxxptr<KCal::ExtendedStorage> m_storage;

public:
    KCalExtendedData(KCalExtendedSource *parent,
                     const QString &notebook,
                     const QString &type) :
        m_parent(parent),
        m_notebook(notebook),
        m_type(type)
    {
        if (!m_app) {
            static const char *argv[] = { "SyncEvolution" };
            static int argc = 1;
            m_app = new QApplication(argc, (char **)argv);
        }
    }

    void extractIncidences(KCal::Incidence::List &incidences,
                           SyncSourceChanges::State state,
                           SyncSourceChanges &changes)
    {
        foreach (KCal::Incidence *incidence, incidences) {
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

        QString getIDString() const { return QString(m_uid.c_str()); }
        KDateTime getDateTime() const { return KDateTime::fromString(QString(m_rid.c_str())); }

        string getLUID() const;
        static string getLUID(const string &uid, const string &rid);
    };

    ItemID getItemID(KCal::Incidence *incidence);
    KCal::Incidence *findIncidence(const string &luid);

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

KCal::Incidence *KCalExtendedData::findIncidence(const string &luid)
{
    ItemID id(luid);
    QString uid = id.getIDString();
    KDateTime rid = id.getDateTime();
    if (!m_storage->load(uid, rid)) {
        m_parent->throwError("failed to load incidence");
    }
    KCal::Incidence *incidence = m_calendar->incidence(uid, rid);
    return incidence;
}

KCalExtendedData::ItemID KCalExtendedData::getItemID(KCal::Incidence *incidence)
{
    QString uid = incidence->uid();
    KDateTime rid = incidence->recurrenceID();
    string ridStr;
    if (rid.isValid()) {
        ridStr = rid.toString().toLocal8Bit().constData();
    }
    return ItemID(uid.toLocal8Bit().constData(),
                  ridStr);
}

QApplication *KCalExtendedData::m_app;

KCalExtendedSource::KCalExtendedSource(const SyncSourceParams &params) :
    TestingSyncSource(params)
{
    m_data = NULL;
}

KCalExtendedSource::~KCalExtendedSource()
{
    delete m_data;
}

void KCalExtendedSource::open()
{
    // TODO: also support todoType
    m_data = new KCalExtendedData(this, getDatabaseID(), eventType);
    m_data->m_calendar.set(new KCal::ExtendedCalendar(KDateTime::Spec::LocalZone()), "KCalExtended Calendar");
    m_data->m_storage.set(m_data->m_calendar->defaultStorage(), "KCalExtended Default Storage");
    if (!m_data->m_storage->open()) {
        throwError("failed to open storage");
    }
    KCal::Notebook *defaultNotebook = m_data->m_storage->defaultNotebook();
    if (!defaultNotebook) {
        throwError("no default Notebook");
    }
    m_data->m_notebookUID = defaultNotebook->uid();
}

bool KCalExtendedSource::isEmpty()
{
    return false;
}

void KCalExtendedSource::close()
{
    if (m_data->m_storage) {
        if (!m_data->m_storage->save()) {
            throwError("could not save calendar");
        }
        m_data->m_storage->close();
        m_data->m_storage.set(NULL);
    }
    if (m_data->m_calendar) {
        m_data->m_calendar->close();
        m_data->m_calendar.set(NULL);
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

    result.push_back(Database("select database via Notebook name",
                              ""));
    return result;
}

void KCalExtendedSource::beginSync(const std::string &lastToken, const std::string &resumeToken)
{
    const char *anchor = resumeToken.empty() ? lastToken.c_str() : resumeToken.c_str();
    KCal::Incidence::List incidences;
    // return all items
    if (!m_data->m_storage->allIncidences(&incidences, m_data->m_notebookUID)) {
        throwError("allIncidences() failed");
    }
    m_data->extractIncidences(incidences, SyncSourceChanges::ANY, *this);
    if (*anchor) {
        KDateTime endSyncTime(QDateTime::fromString(QString(anchor), Qt::ISODate));
        KCal::Incidence::List added, modified, deleted;
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
    if (!m_data->m_storage->save()) {
        throwError("could not save calendar");
    }

    // sleep at least a second to ensure that time stamps increment
    // for change tracking
    time_t start = time(NULL);
    do {
        sleep(1);
    } while ((long)(time(NULL) - start) <= 0);

    QDateTime now = QDateTime::currentDateTime().toUTC();
    const char *anchor = now.toString(Qt::ISODate).toLocal8Bit().constData();

    // sleep again because change tracking does not say whether
    // comparisons are < or <=
    start = time(NULL);
    do {
        sleep(1);
    } while ((long)(time(NULL) - start) <= 0);

    return anchor;
}

void KCalExtendedSource::readItem(const string &uid, std::string &item)
{
    cxxptr<KCal::Incidence> incidence(m_data->findIncidence(uid));
    if (!incidence) {
        throwError(string("failure extracting ") + uid);
    }
    KCal::ExtendedCalendar calendar(KDateTime::Spec::LocalZone());
    calendar.addIncidence(incidence.release());
    KCal::ICalFormat formatter;
    item = formatter.toString(&calendar).toLocal8Bit().constData();
}

TestingSyncSource::InsertItemResult KCalExtendedSource::insertItem(const string &uid, const std::string &item)
{
    KCal::ExtendedCalendar calendar(KDateTime::Spec::LocalZone());
    KCal::ICalFormat parser;
    parser.fromString(&calendar, QString(item.c_str()));
    KCal::Incidence::List incidences = calendar.rawIncidences();
    if (incidences.empty()) {
        throwError("error parsing iCalendar 2.0 item");
    }
    bool updated;
    string newUID;
    if (uid.empty()) {
        // addInstance transfers ownership, need a copy
        cxxptr<KCal::Incidence> tmp(incidences[0]->clone(), "incidence clone");
        KCal::Incidence *incidence = tmp;

        updated = false;
        if (!m_data->m_calendar->addIncidence(tmp.release())) {
            throwError("could not add incidence");
        }
        m_data->m_calendar->setNotebook(incidence, m_data->m_notebookUID);
        newUID = m_data->getItemID(incidence).getLUID();
    } else {
        KCal::Incidence *incidence = incidences[0];
        updated = true;
        newUID = uid;
        KCal::Incidence *original = m_data->findIncidence(uid);
        if (!original) {
            throwError("incidence to be updated not found");
        }
        if (original->type() != incidence->type()) {
            throwError("cannot update incidence, wrong type?!");
        }

        // preserve UID and RECURRENCE-ID, because this must not change
        // and some peers don't preserve it
        incidence->setUid(original->uid());
        if (original->hasRecurrenceID()) {
            incidence->setRecurrenceID(original->recurrenceID());
        }

        if (original->type() == eventType) {
            *static_cast<KCal::Event *>(original) =
                *static_cast<KCal::Event *>(incidence);
        } else if (original->type() == todoType) {
            *static_cast<KCal::Todo *>(original) =
                *static_cast<KCal::Todo *>(incidence);
        } else {
            throwError("unknown type");
        }
        m_data->m_calendar->setNotebook(original, m_data->m_notebookUID);
        // no need to save
    }

    return InsertItemResult(newUID,
                            "",
                            updated);
}


void KCalExtendedSource::deleteItem(const string &uid)
{
    KCal::Incidence *incidence = m_data->findIncidence(uid);
    if (!incidence) {
        throwError("incidence not found");
    }
    if (!m_data->m_calendar->deleteIncidence(incidence)) {
        throwError("could not delete incidence");
    }
}

SE_END_CXX

#endif /* ENABLE_KCALEXTENDED */

#ifdef ENABLE_MODULES
# include "KCalExtendedSourceRegister.cpp"
#endif
