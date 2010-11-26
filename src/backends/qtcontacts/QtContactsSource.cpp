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

#ifdef ENABLE_QTCONTACTS

#include "QtContactsSource.h"

#include <QCoreApplication>

#include <QContact>
#include <QContactManager>
#include <QContactFetchRequest>
#include <QContactRemoveRequest>
#include <QContactSaveRequest>
#include <QContactTimestamp>
#include <QContactLocalIdFilter>

#include <QVersitContactExporter>
#include <QVersitContactImporter>
#include <QVersitDocument>
#include <QVersitWriter>
#include <QVersitReader>

#include <syncevo/SmartPtr.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

using namespace QtMobility;

class QtContactsData
{
    QtContactsSource *m_parent;
    QString m_managerURI;
    cxxptr<QContactManager> m_manager;

public:
    QtContactsData(QtContactsSource *parent,
                   const QString &managerURI) :
        m_parent(parent),
        m_managerURI(managerURI)
    {
        if (!qApp) {
            static const char *argv[] = { "SyncEvolution" };
            static int argc = 1;
            new QCoreApplication(argc, (char **)argv);
        }
    }

    static QList<QContactLocalId> createContactList(const string &uid)
    {
        QList<QContactLocalId> list;
        list.append(atoi(uid.c_str()));
        return list;
    }

    static QContactLocalIdFilter createFilter(const string &uid)
    {
        QContactLocalIdFilter filter;
        filter.setIds(createContactList(uid));
        return filter;
    }

    static string getLUID(const QContact &contact)
    {
        QContactLocalId id = contact.localId();
        return StringPrintf("%u", id);
    }

    static string getRev(const QContact &contact)
    {
        QContactTimestamp rev = contact.detail<QContactTimestamp>();
        QDateTime stamp = rev.lastModified();
        if (!stamp.isValid()) {
            stamp = rev.created();
        }
        return stamp.toString().toLocal8Bit().constData();
    }

    template<class T>
    void checkError(const char *op, T &req)
    {
        if (req.error()) {
            m_parent->throwError(StringPrintf("%s: failed with error %d", op, req.error()));
        }
    }
    template<class T>
    void checkError(const char *op,
                    T &req,
                    const QMap<int, QContactManager::Error> &errors)
    {
        if (errors.isEmpty()) {
            checkError(op, req);
        } else {
            list<string> res;
            foreach (int index, errors.keys()) {
                res.push_back(StringPrintf("entry #%d failed with error %d", index, errors[index]));
            }
            m_parent->throwError(StringPrintf("%s: failed with error %d, ", op, req.error()) +
                                 boost::join(res, ", "));
        }
    }

    friend class QtContactsSource;
};

QtContactsSource::QtContactsSource(const SyncSourceParams &params) :
    TrackingSyncSource(params)
{
    m_data = NULL;
    SyncSourceLogging::init(InitList<std::string>("N_FIRST") + "N_MIDDLE" + "N_LAST",
                            " ",
                            m_operations);
}

QtContactsSource::~QtContactsSource()
{
    delete m_data;
}

void QtContactsSource::open()
{
    m_data = new QtContactsData(this, NULL);
    cxxptr<QContactManager> manager(QContactManager::fromUri(m_data->m_managerURI),
                                    "QTContactManager");
    if (manager->error()) {
        throwError(StringPrintf("failed to open QtContact database %s, error code %d",
                                m_data->m_managerURI.toLocal8Bit().constData(),
                                manager->error()));
    }
    m_data->m_manager = manager;
}

bool QtContactsSource::isEmpty()
{
    return false;
}

void QtContactsSource::close()
{
    m_data->m_manager.set(0);
}

QtContactsSource::Databases QtContactsSource::getDatabases()
{
    Databases result;
    QStringList availableManagers = QContactManager::availableManagers();

    result.push_back(Database("select database via QtContacts Manager URL",
                              "qtcontacts:tracker:"));
    return result;
}

void QtContactsSource::listAllItems(RevisionMap_t &revisions)
{
    QContactFetchRequest fetch;
    fetch.setManager(m_data->m_manager.get());

    // only need ID and time stamps
    QContactFetchHint hint;
    hint.setOptimizationHints(QContactFetchHint::OptimizationHints(QContactFetchHint::NoRelationships|QContactFetchHint::NoBinaryBlobs));
    hint.setDetailDefinitionsHint(QStringList() << QContactTimestamp::DefinitionName);
    fetch.setFetchHint(hint);

    fetch.start();
    fetch.waitForFinished();
    m_data->checkError("read all items", fetch);
    foreach (const QContact &contact, fetch.contacts()) {
        string revision = QtContactsData::getRev(contact);
        string luid = QtContactsData::getLUID(contact);
        if (luid == "2147483647" &&
            revision == "") {
            // Seems to be a special, artifical contact that always
            // exists. Ignore it.
            //
            // Also note that qtcontacts-tracker and/or QtContacts
            // spew out a warning on stdout about it (?):
            // skipping contact with unsupported IRI: "http://www.semanticdesktop.org/ontologies/2007/03/22/nco#default-contact-emergency"
            continue;
        }
        revisions[luid] = revision;
    }
}

void QtContactsSource::readItem(const string &uid, std::string &item, bool raw)
{
    QContactFetchRequest fetch;
    fetch.setManager(m_data->m_manager.get());
    fetch.setFilter(QtContactsData::createFilter(uid));
    fetch.start();
    fetch.waitForFinished();
    QContact contact = fetch.contacts().first();
    QVersitContactExporter exporter;
    if (!exporter.exportContacts(fetch.contacts(), QVersitDocument::VCard30Type)) {
        throwError(uid + ": encoding as vCard 3.0 failed");
    }
    QByteArray vcard;
    QVersitWriter writer(&vcard);
    if (!writer.startWriting(exporter.documents())) {
        throwError(uid + ": writing as vCard 3.0 failed");
    }
    writer.waitForFinished();
    item = vcard.constData();
    m_data->checkError("encoding as vCard 3.0", writer);
}

TrackingSyncSource::InsertItemResult QtContactsSource::insertItem(const string &uid, const std::string &item, bool raw)
{
    QVersitReader reader(QByteArray(item.c_str()));
    if (!reader.startReading()) {
        throwError("reading vCard failed");
    }
    reader.waitForFinished();
    m_data->checkError("decoding vCard", reader);

    QVersitContactImporter importer;
    if (!importer.importDocuments(reader.results())) {
        throwError("importing vCard failed");
    }

    QList<QContact> contacts = importer.contacts();
    QContact &contact = contacts.first();

    if (!uid.empty()) {
        QContactId id;
        id.setManagerUri(m_data->m_managerURI);
        id.setLocalId(atoi(uid.c_str()));
        contact.setId(id);
    }

    QContactSaveRequest save;
    save.setManager(m_data->m_manager.get());
    save.setContacts(QList<QContact>() << contact);
    save.start();
    save.waitForFinished();
    m_data->checkError("saving contact", save, save.errorMap());

    QList<QContact> savedContacts = save.contacts();
    QContact &savedContact = savedContacts.first();

    // Saving is not guaranteed to update the time stamp (BMC #5710).
    // Need to read again.
    QContactFetchRequest fetch;
    fetch.setManager(m_data->m_manager.get());
    fetch.setFilter(QtContactsData::createFilter(QtContactsData::getLUID(savedContact)));
    QContactFetchHint hint;
    hint.setOptimizationHints(QContactFetchHint::OptimizationHints(QContactFetchHint::NoRelationships|QContactFetchHint::NoBinaryBlobs));
    hint.setDetailDefinitionsHint(QStringList() << QContactTimestamp::DefinitionName);
    fetch.setFetchHint(hint);
    fetch.start();
    fetch.waitForFinished();
    QContact &finalContact = fetch.contacts().first();

    return InsertItemResult(QtContactsData::getLUID(savedContact),
                            QtContactsData::getRev(finalContact),
                            false);
}


void QtContactsSource::removeItem(const string &uid)
{
#if 1
    QContactRemoveRequest remove;
    remove.setManager(m_data->m_manager.get());
    remove.setContactIds(QtContactsData::createContactList(uid));
    remove.start();
    remove.waitForFinished();
    m_data->checkError("remove contact", remove, remove.errorMap());
#else
    m_data->m_manager->removeContact(atoi(uid.c_str()));
#endif
}

std::string QtContactsSource::getDescription(const string &luid)
{
    try {
        QContactFetchRequest fetch;
        fetch.setManager(m_data->m_manager.get());
        fetch.setFilter(QtContactsData::createFilter(luid));
        fetch.start();
        fetch.waitForFinished();
        if (fetch.contacts().isEmpty()) {
            return "";
        }
        QContact contact = fetch.contacts().first();
        string descr = contact.displayLabel().toLocal8Bit().constData();
        return descr;
    } catch (...) {
        // Instead of failing we log the error and ask
        // the caller to log the UID. That way transient
        // errors or errors in the logging code don't
        // prevent syncs.
        handleException();
        return "";
    }
}

SE_END_CXX

#endif /* ENABLE_QTCONTACTS */

#ifdef ENABLE_MODULES
# include "QtContactsSourceRegister.cpp"
#endif
