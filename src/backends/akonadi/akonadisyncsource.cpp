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
#include "timetrackingobserver.h"

#include <Akonadi/ItemCreateJob>
#include <Akonadi/ItemDeleteJob>
#include <Akonadi/ItemFetchJob>
#include <Akonadi/ItemFetchScope>
#include <Akonadi/ItemModifyJob>

using namespace Akonadi;

AkonadiSyncSource::AkonadiSyncSource(TimeTrackingObserver *observer,
                                     AkonadiSyncSourceConfig *config,
                                     SyncManagerConfig *managerConfig)
    : SyncSource(config->getName(), config)
    , m_observer(observer)
{
    managerConfig->setSyncSourceConfig(*config);
}

AkonadiSyncSource::~AkonadiSyncSource()
{
}

int AkonadiSyncSource::beginSync()
{
    // Fetch all item sets from the time-tracking observer that
    // correspond to this SyncSource's corresponding Akonadi collection
    m_allItems = m_observer->allItems(m_lastSyncTime, m_collectionId);
    m_newItems = m_observer->addedItems(m_lastSyncTime, m_collectionId);
    m_updatedItems = m_observer->changedItems(m_lastSyncTime, m_collectionId);
    m_deletedItems = m_observer->removedItems(m_lastSyncTime, m_collectionId);

    m_currentTime = QDateTime::currentDateTime().toUTC();
    kDebug() << "Begin sync at" << m_currentTime;
    return 0;
}

int AkonadiSyncSource::endSync()
{
    m_lastSyncTime = m_currentTime;
    kDebug() << "End sync at" << m_lastSyncTime;
    return 0;
}

int AkonadiSyncSource::addItem(SyncItem& syncItem)
{
    kDebug() << "Remote wants us to add" << syncItemToString(syncItem);

    Item item;
    item.setMimeType(syncItem.getDataType());
    item.setPayloadFromData(QByteArray((char *)syncItem.getData()));

    ItemCreateJob *createJob = new ItemCreateJob(item, Collection(m_collectionId));
    if (createJob->exec()) {
        item = createJob->item();
        kDebug() << "Created new item" << item.id() << "with mimetype" << item.mimeType()
                 << "and added it to collection" << m_collectionId;
        syncItem.setKey(QByteArray::number(item.id()));
        //TODO: Read-only datastores may not have actually added something here!
        return 200; // Ok, the SyncML command completed successfully
    } else {
        kDebug() << "Unable to create item" << item.id() << "in Akonadi datastore";
        return 211; // Failed, the recipient encountered an error
    }
}

int AkonadiSyncSource::updateItem(SyncItem& syncItem)
{
    kDebug() << "Remote wants us to update" << syncItemToString(syncItem);

    Entity::Id syncItemId = QByteArray(syncItem.getKey()).toLongLong();

    // Fetch item which shall be modified
    ItemFetchJob *fetchJob = new ItemFetchJob(Item(syncItemId));
    if (fetchJob->exec()) {
        Item item = fetchJob->items().first();

        // Modify item, e.g. set new payload data
        QByteArray data((char *)syncItem.getData());
        item.setPayloadFromData(data);

        // Store back modified item
        ItemModifyJob *modifyJob = new Akonadi::ItemModifyJob(item);
        if (modifyJob->exec()) {
            kDebug() << "Item" << item.id() << "modified successfully";
            return 200; // Ok, the SyncML command completed successfully
        } else {
            return 211; // Failed, the recipient encountered an error
        }
    } else {
        kDebug() << "Unable to find item with id" << syncItemId;
        return 211; // Failed, the recipient encountered an error
    }
}

int AkonadiSyncSource::deleteItem(SyncItem& syncItem)
{
    kDebug() << "Remote wants us to delete" << syncItemToString(syncItem);

    Entity::Id syncItemId = QByteArray(syncItem.getKey()).toLongLong();

    // Delete the item from our collection
    ItemDeleteJob *deleteJob = new ItemDeleteJob(Item(syncItemId));
    if (deleteJob->exec()) {
        return 200; // Ok, the SyncML command completed successfully
    } else {
        return 211; // Failed, the recipient encountered an error
    }
}

int AkonadiSyncSource::removeAllItems()
{
    kDebug() << "Remote wants us to remove all items";

    // Remove all items from our collection
    ItemDeleteJob *deleteJob = new ItemDeleteJob(Collection(m_collectionId));
    if (deleteJob->exec()) {
        return 200; // Ok, the SyncML command completed successfully
    } else {
        return 211; // Failed, the recipient encountered an error
    }
}

SyncItem *AkonadiSyncSource::first(ItemSet set, bool withData)
{
    SyncState state = SYNC_STATE_NONE;
    Akonadi::Item item;

    switch (set) {
        case AllItems:
            m_allItemsIndex = 0;
            if (m_allItemsIndex < m_allItems.size()) {
                kDebug() << "Fetch first item from 'all items' set";
                item = m_allItems[m_allItemsIndex];
            }
            break;
        case NewItems:
            m_newItemsIndex = 0;
            if (m_newItemsIndex < m_newItems.size()) {
                kDebug() << "Fetch first item from 'new items' set";
                state = SYNC_STATE_NEW;
                item = m_newItems[m_newItemsIndex];
            }
            break;
        case UpdatedItems:
            m_updatedItemsIndex = 0;
            if (m_updatedItemsIndex < m_updatedItems.size()) {
                kDebug() << "Fetch first item from 'updated items' set";
                state = SYNC_STATE_UPDATED;
                item = m_updatedItems[m_updatedItemsIndex];
            }
            break;
        case DeletedItems:
            m_deletedItemsIndex = 0;
            if (m_deletedItemsIndex < m_deletedItems.size()) {
                kDebug() << "Fetch first item from 'next items' set";
                state = SYNC_STATE_DELETED;
                item = m_deletedItems[m_deletedItemsIndex];
            }
            break;
    }

    if (item.isValid()) {
        return syncItem(item, withData, state);
    } else {
        kDebug() << "Fetched invalid item";
        return 0;
    }
}

SyncItem *AkonadiSyncSource::next(ItemSet set, bool withData)
{
    SyncState state = SYNC_STATE_NONE;
    Akonadi::Item item;

    switch (set) {
        case AllItems:
            m_allItemsIndex++;
            if (m_allItemsIndex < m_allItems.size()) {
                kDebug() << "Fetch item" << m_allItemsIndex << "from 'all items' set";
                item = m_allItems[m_allItemsIndex];
            }
            break;
        case NewItems:
            m_newItemsIndex++;
            if (m_newItemsIndex < m_newItems.size()) {
                kDebug() << "Fetch item" << m_newItemsIndex << "from 'new items' set";
                state = SYNC_STATE_NEW;
                item = m_newItems[m_newItemsIndex];
            }
            break;
        case UpdatedItems:
            m_updatedItemsIndex++;
            if (m_updatedItemsIndex < m_updatedItems.size()) {
                kDebug() << "Fetch item" << m_updatedItemsIndex << "from 'updated items' set";
                state = SYNC_STATE_UPDATED;
                item = m_updatedItems[m_updatedItemsIndex];
            }
            break;
        case DeletedItems:
            m_deletedItemsIndex++;
            if (m_deletedItemsIndex < m_deletedItems.size()) {
                kDebug() << "Fetch item" << m_deletedItemsIndex << "from 'deleted items' set";
                state = SYNC_STATE_DELETED;
                item = m_deletedItems[m_deletedItemsIndex];
            }
            break;
    }

    if (item.isValid()) {
        return syncItem(item, withData, state);
    } else {
        kDebug() << "Fetched invalid item";
        return 0;
    }
}

SyncItem *AkonadiSyncSource::syncItem(const Item &item, bool withData, SyncState state) const
{
    SyncItem *syncItem = new SyncItem();

    kDebug() << "Return SyncItem for item" << item.id();

    syncItem->setKey(QByteArray::number(item.id()));
    syncItem->setModificationTime(m_lastSyncTime.toTime_t());
    syncItem->setState(state);

    if (withData) {
        ItemFetchJob *fetchJob = new ItemFetchJob(item);
        fetchJob->fetchScope().fetchFullPayload();
        if (fetchJob->exec()) {
            kDebug() << "Add payload data";
            QByteArray data = fetchJob->items().first().payloadData().toBase64();

            syncItem->setData(data, data.size());
            syncItem->setDataEncoding(SyncItem::encodings::escaped);
            syncItem->setDataType(getConfig().getType());
        } else {
            kDebug() << "Unable to add payload data";
        }
    }
    //kDebug() << "Created SyncItem:" << syncItemToString(*syncItem);
    return syncItem;
}

QString AkonadiSyncSource::syncItemToString(SyncItem& syncItem) const
{
    QByteArray data((char *)syncItem.getData());
    QString ret("Key: ");
    ret += syncItem.getKey();
    ret += " Mod.Time: ";
    ret += QString::number(syncItem.getModificationTime());
    ret += " Encoding: ";
    ret += syncItem.getDataEncoding();
    ret += " Size: ";
    ret += QString::number(syncItem.getDataSize());
    ret += " Type: ";
    ret += syncItem.getDataType();
    ret += " State: ";
    ret += QString::number(syncItem.getState());
    ret += " Data:\n";
    ret += data;
    return ret;
}

#include "moc_akonadisyncsource.cpp"
