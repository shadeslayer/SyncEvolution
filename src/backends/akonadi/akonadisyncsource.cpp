/*
    Copyright (c) 2009 Sascha Peilicke <sasch.pe@gmx.de>

    This application is free software; you can redistribute it and/or modify it
    under the terms of the GNU Library General Public License as published by
    the Free Software Foundation; either version 2 of the License, or (at your
    option) any later version.

    This application is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Library General Public
    License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this application; see the file COPYING.LIB.  If not, write to the
    Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
    02110-1301, USA.
*/

#include "akonadisyncsource.h"

#ifdef ENABLE_AKONADI

#include <Akonadi/ItemCreateJob>
#include <Akonadi/ItemDeleteJob>
#include <Akonadi/ItemFetchJob>
#include <Akonadi/ItemFetchScope>
#include <Akonadi/ItemModifyJob>
#include <Akonadi/CollectionFetchJob>
#include <Akonadi/Control>
#include <kurl.h>

#include <QtCore/QCoreApplication>

SE_BEGIN_CXX
using namespace Akonadi;

AkonadiSyncSource::AkonadiSyncSource(const char *submime,
                                     const SyncSourceParams &params) :
    TrackingSyncSource(params),
    m_subMime(submime)
{
}

AkonadiSyncSource::~AkonadiSyncSource()
{
}

bool AkonadiSyncSource::isEmpty(){return false;}
void AkonadiSyncSource::start()
{
    int argc = 1;
    static const char *prog = "syncevolution";
    static char *argv[] = { (char *)&prog, NULL };
    if (!qApp) {
        new QCoreApplication(argc, argv);
    }
}

SyncSource::Databases AkonadiSyncSource::getDatabases()
{
    start();

    Databases res;
    // TODO: insert databases which match the "type"
    // of the source, including a user-visible description
    // and a database IDs. Exactly one of the databases
    // should be marked as the default one used by the
    // source.
    // res.push_back("Contacts", "some-KDE-specific-ID", isDefault);

    CollectionFetchJob *fetchJob = new CollectionFetchJob(Collection::root(),
                                                          CollectionFetchJob::Recursive);
    // fetchJob->setMimeTypeFilter(m_subMime.c_str());
    if (!fetchJob->exec()) {
        throwError("cannot list collections");
    }

    // the first collection of the right type is the default
    // TODO: is there a better way to choose the default?
    bool isFirst = true;
    Collection::List collections = fetchJob->collections();
    foreach(const Collection &collection, collections) {
        // TODO: filter out collections which contain no items
        // of the type we sync (m_subMime)
        if (true) {
            res.push_back(Database(collection.name().toUtf8().constData(),
                                   collection.url().url().toUtf8().constData(),
                                   isFirst));
            isFirst = false;
        }
    }
    return res;
}

void AkonadiSyncSource::open()
{
    start();

    // the "evolutionsource" property, empty for default,
    // otherwise the collection URL or a name
    string id = getDatabaseID();

    // TODO: support selection by name and empty ID for default

    // TODO: check for invalid URL?!
    m_collection = Collection::fromUrl(KUrl(id.c_str()));
}

void AkonadiSyncSource::listAllItems(SyncSourceRevisions::RevisionMap_t &revisions)
{
    // copy all local IDs and the corresponding revision
    ItemFetchJob *fetchJob = new ItemFetchJob(m_collection);
    if (!fetchJob->exec()) {
        throwError("listing items");
    }
    BOOST_FOREACH(const Item &item, fetchJob->items()) {
        // TODO: filter out items which don't have the right type
        // (for example, VTODO when syncing events)
        // if (... == m_subMime)
        revisions[QByteArray::number(item.id()).constData()] =
                  QByteArray::number(item.revision()).constData();
    }
}

void AkonadiSyncSource::close()
{
    // TODO: close collection!?
}

TrackingSyncSource::InsertItemResult
AkonadiSyncSource::insertItem(const std::string &luid, const std::string &data, bool raw)
{
    Item item;

    if (luid.empty()) {
        item.setMimeType(m_subMime.c_str());
        item.setPayloadFromData(QByteArray(data.c_str()));
        ItemCreateJob *createJob = new ItemCreateJob(item, m_collection);
        if (!createJob->exec()) {
            throwError(string("storing new item ") + luid);
        }
        item = createJob->item();
    } else {
        Entity::Id syncItemId = QByteArray(luid.c_str()).toLongLong();
        ItemFetchJob *fetchJob = new ItemFetchJob(Item(syncItemId));
        if (!fetchJob->exec()) {
            throwError(string("checking item ") + luid);
        }            
        ItemModifyJob *modifyJob = new ItemModifyJob(item);
        // TODO: SyncEvolution must pass the known revision that
        // we are updating.
        // TODO: check that the item has not been updated in the meantime
        if (!modifyJob->exec()) {
            throwError(string("updating item ") + luid);
        }
        item = modifyJob->item();
    }

    // TODO: Read-only datastores may not have actually added something here!
    return InsertItemResult(QByteArray::number(item.id()).constData(),
                            QByteArray::number(item.revision()).constData(),
                            false);
}

void AkonadiSyncSource::removeItem(const string &luid)
{
    Entity::Id syncItemId = QByteArray(luid.c_str()).toLongLong();

    // Delete the item from our collection
    // TODO: check that the revision is right (need revision from SyncEvolution)
    ItemDeleteJob *deleteJob = new ItemDeleteJob(Item(syncItemId));
    if (!deleteJob->exec()) {
        throwError(string("deleting item " ) + luid);
    }
}

void AkonadiSyncSource::readItem(const std::string &luid, std::string &data, bool raw)
{
    Entity::Id syncItemId = QByteArray(luid.c_str()).toLongLong();

    ItemFetchJob *fetchJob = new ItemFetchJob(Item(syncItemId));
    fetchJob->fetchScope().fetchFullPayload();
    if (fetchJob->exec()) {
        QByteArray payload = fetchJob->items().first().payloadData();
        data.assign(payload.constData(),
                    payload.size());
    } else {
        throwError(string("extracting item " ) + luid);
    }
}

SE_END_CXX
#endif // ENABLE_AKONADI
