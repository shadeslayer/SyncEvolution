/*
 * Copyright (C) 2008-2009 Patrick Ohly <patrick.ohly@gmx.de>
 * Copyright (C) 2009 Intel Corporation
 */

#include "TrackingSyncSource.h"
#include "SafeConfigNode.h"
#include "PrefixConfigNode.h"

#include <ctype.h>
#include <errno.h>

#include <fstream>

TrackingSyncSource::TrackingSyncSource(const EvolutionSyncSourceParams &params) :
    EvolutionSyncSource(params),
    m_trackingNode(new PrefixConfigNode("item-",
                                        boost::shared_ptr<ConfigNode>(new SafeConfigNode(params.m_nodes.m_trackingNode))))
{
}

void TrackingSyncSource::beginSyncThrow(bool needAll,
                                       bool needPartial,
                                       bool deleteLocal)
{
    RevisionMap_t revisions;
    listAllItems(revisions);

    // slow sync or refresh-from-server/client: clear tracking node and
    // recreate it based on current content of database
    if (!needPartial) {
        map<string, string> props;
        m_trackingNode->readProperties(props);

        BOOST_FOREACH(const StringPair &prop, props) {
            const string &uid(prop.first);
            m_deletedItems.addItem(uid.c_str());
            m_trackingNode->removeProperty(uid);
        }
    }

    BOOST_FOREACH(const StringPair &mapping, revisions) {
        const string &uid = mapping.first;
        const string &revision = mapping.second;

        // uid must always be non-empty whereas
        // revision may be empty when doing refresh-from-client
        // syncs; refresh-from-client cannot be distinguished
        // from slow syncs, so allow slow syncs, too
        if (uid.empty()) {
            throwError("could not read UID for an item");
        }
        bool fromClient = needAll && !needPartial && !deleteLocal;
        if (!fromClient && revision.empty()) {
            throwError(string("could not read revision identifier for item ") + uid + ": only refresh-from-client synchronization is supported");
        }

        if (deleteLocal) {
            deleteItem(uid);
        } else {
            // always remember the item, need full list to find deleted items
            m_allItems.addItem(uid);

            if (needPartial) {
                string serverRevision(m_trackingNode->readProperty(uid));

                if (!serverRevision.size()) {
                    m_newItems.addItem(uid);
                    m_trackingNode->setProperty(uid, revision);
                } else {
                    if (revision != serverRevision) {
                        m_updatedItems.addItem(uid);
                        m_trackingNode->setProperty(uid, revision);
                    }
                }
            } else {
                // refresh-from-client: make sure that all items we are about
                // to send to server are also in our tracking node (otherwise
                // the next incremental sync will go wrong)
                m_trackingNode->setProperty(uid, revision);
            }
        }
    }

    // clear information about all items that we recognized as deleted
    if (needPartial) {
        map<string, string> props;
        m_trackingNode->readProperties(props);

        BOOST_FOREACH(const StringPair &mapping, props) {
            const string &uid(mapping.first);
            if (m_allItems.find(uid) == m_allItems.end()) {
                m_deletedItems.addItem(uid);
                m_trackingNode->removeProperty(uid);
            }
        }
    }
            
    if (!needAll) {
        // did not need full list after all...
        m_allItems.clear();
    }
}

void TrackingSyncSource::endSyncThrow()
{
    // store changes persistently
    flush();

    if (!hasFailed()) {
        m_trackingNode->flush();
    } else {
        // SyncEvolution's error handling for failed sources
        // forces a slow sync the next time. Therefore the
        // content of the tracking node is irrelevant in
        // case of a failure.
    }
}

void TrackingSyncSource::backupData(const string &dir, ConfigNode &node, BackupReport &report)
{
    RevisionMap_t revisions;
    listAllItems(revisions);

    unsigned long counter = 1;
    errno = 0;
    BOOST_FOREACH(const StringPair &mapping, revisions) {
        const string &uid = mapping.first;
        const string &rev = mapping.second;
        cxxptr<SyncItem> item(createItem(uid), "sync item");

        stringstream filename;
        filename << dir << "/" << counter;

        ofstream out(filename.str().c_str());
        out.write(item->getData(), item->getDataSize());
        out.close();
        if (out.fail()) {
            throwError(string("error writing ") + filename.str() + ": " + strerror(errno));
        }

        stringstream key;
        key << counter << "-uid";
        node.setProperty(key.str(), uid);
        key.clear();
        key << counter << "-rev";
        node.setProperty(key.str(), rev);

        counter++;
    }

    stringstream value;
    value << counter - 1;
    node.setProperty("numitems", value.str());
    node.flush();

    report.setNumItems(counter - 1);
}

void TrackingSyncSource::restoreData(const string &dir, const ConfigNode &node)
{
    RevisionMap_t revisions;
    listAllItems(revisions);

    // int counter = 1;
    // BOOST_FOREACH(const StringPair &mapping, revisions) {
    // }
}


SyncMLStatus TrackingSyncSource::addItemThrow(SyncItem& item)
{
    InsertItemResult res = insertItem("", item);
    item.setKey(res.m_uid.c_str());
    if (res.m_uid.empty() || res.m_revision.empty()) {
        throwError("could not add item");
    }
    m_trackingNode->setProperty(res.m_uid, res.m_revision);
    return res.m_merged ? STATUS_DATA_MERGED : STATUS_OK;
}

SyncMLStatus TrackingSyncSource::updateItemThrow(SyncItem& item)
{
    const string uid = item.getKey();
    InsertItemResult res = insertItem(uid, item);
    if (res.m_uid != uid) {
        m_trackingNode->removeProperty(uid);
    }
    item.setKey(res.m_uid.c_str());
    if (res.m_uid.empty() || res.m_revision.empty()) {
        throwError("could not update item");
    }
    m_trackingNode->setProperty(res.m_uid, res.m_revision);
    return res.m_merged ? STATUS_DATA_MERGED : STATUS_OK;
}

SyncMLStatus TrackingSyncSource::deleteItemThrow(SyncItem& item)
{
    const string uid = item.getKey();
    deleteItem(uid);
    m_trackingNode->removeProperty(uid);
    return STATUS_OK;
}
