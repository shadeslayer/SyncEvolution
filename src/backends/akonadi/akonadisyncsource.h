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

#ifndef AKONADISYNCSOURCE_H
#define AKONADISYNCSOURCE_H

#include "settings.h"

#include <Akonadi/Collection>
#include <Akonadi/Item>

#include <QDateTime>
#include <QObject>

#include <funambol/common/spds/SyncManagerConfig.h>
#include <funambol/common/spds/SyncSource.h>

class TimeTrackingObserver;

/**
 * Base config for all sync sources.
 */
class AkonadiSyncSourceConfig : public QObject,
                                public SyncSourceConfig
{
    Q_OBJECT

public:
    enum SyncMode {
        Slow = 0,
        TwoWay,
        OneWayFromServer,
        OneWayFromClient,
        RefreshFromServer,
        RefreshFromClient
    };

    AkonadiSyncSourceConfig(unsigned long lastSync = 0,
                            const char *uri = "default")
        : SyncSourceConfig()
    {
        setURI(uri);
        setLast(lastSync);                          // Set last sync time
        setVersion("");                             // Don't care for the SyncML version
        setEncoding(SyncItem::encodings::escaped);  // Means base64 in Funambol tongue

        setSyncModes("slow,two-way,one-way-from-server,one-way-from-client,refresh-from-server,refresh-from-client");
        //setSupportedTypes("");                    // This can be set by derived sync sources
        setEncryption("");

        // Determine how to sync
        switch(Settings::self()->syncMode()) {
            case 0: setSync("slow");
                    kDebug() << "Use 'Slow' sync mode"; break;
            case 1: setSync("two-way");
                    kDebug() << "Use 'TwoWay' sync mode"; break;
            case 2: setSync("one-way-from-server");
                    kDebug() << "Use 'OneWayFromServer' sync mode"; break;
            case 3: setSync("one-way-from-client");
                    kDebug() << "Use 'OneWayFromClient' sync mode"; break;
            case 4: setSync("refresh-from-server");
                    kDebug() << "Use 'RefreshFromServer' sync mode"; break;
            case 5: setSync("refresh-from-client");
                    kDebug() << "Use 'RefreshFromClient' sync mode"; break;
        }

        kDebug() << "Sync source config for" << getName() << "with URI" << getURI() << "set up";
    }
};

/**
 * Abstract base class for all sync sources.
 */
class AkonadiSyncSource : public QObject
                        , public SyncSource
{
    Q_OBJECT

public:
    virtual ~AkonadiSyncSource();

    // The following are Funambol API specific methods
    int beginSync();
    int endSync();

    int addItem(SyncItem& syncItem);
    int updateItem(SyncItem& syncItem);
    int deleteItem(SyncItem& syncItem);
    int removeAllItems();

    SyncItem *getFirstItem() { return first(AllItems); }
    SyncItem *getNextItem() { return next(AllItems); }
    SyncItem *getFirstNewItem() { return first(NewItems); }
    SyncItem *getNextNewItem() { return next(NewItems); }
    SyncItem *getFirstUpdatedItem() { return first(UpdatedItems); }
    SyncItem *getNextUpdatedItem() { return next(UpdatedItems); }
    SyncItem *getFirstDeletedItem() { return first(DeletedItems, false); }
    SyncItem *getNextDeletedItem() { return next(DeletedItems, false); }
    SyncItem *getFirstItemKey() { return first(AllItems, false); }
    SyncItem *getNextItemKey() { return next(AllItems, false); }
    // End of Funambol API specific methods

    QDateTime lastSyncTime() const { return m_lastSyncTime; }

protected:
    AkonadiSyncSource(TimeTrackingObserver *observer,
                      AkonadiSyncSourceConfig *config,
                      SyncManagerConfig *managerConfig);

    TimeTrackingObserver *m_observer;
    Akonadi::Entity::Id m_collectionId;
    QDateTime m_lastSyncTime;
    QDateTime m_currentTime;

private:
    enum ItemSet {
        AllItems = 0,
        NewItems,
        UpdatedItems,
        DeletedItems
    };

    SyncItem *first(ItemSet set, bool withData = true);
    SyncItem *next(ItemSet set, bool withData = true);
    SyncItem *syncItem(const Akonadi::Item &item, bool withData = true, SyncState state = SYNC_STATE_NONE) const;
    QString syncItemToString(SyncItem& syncItem) const;

    int m_allItemsIndex;
    int m_newItemsIndex;
    int m_updatedItemsIndex;
int m_deletedItemsIndex;

    Akonadi::Item::List m_allItems;
    Akonadi::Item::List m_newItems;
    Akonadi::Item::List m_updatedItems;
    Akonadi::Item::List m_deletedItems;
};

#endif
