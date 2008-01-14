/*
 * Copyright (C) 2008 Patrick Ohly
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

#include "TrackingSyncSource.h"

TrackingSyncSource::TrackingSyncSource(const string &name, SyncSourceConfig *sc, const string &changeId, const string &id,
                                       eptr<spdm::DeviceManagementNode> trackingNode) :
    EvolutionSyncSource(name, sc, changeId, id),
    m_trackingNode(trackingNode)
{
    m_trackingNode->setAutosave(false);
}

void TrackingSyncSource::beginSyncThrow(bool needAll,
                                       bool needPartial,
                                       bool deleteLocal)
{
    RevisionMap_t revisions;
    listAllItems(revisions);

    for (RevisionMap_t::const_iterator it = revisions.begin();
         it != revisions.end();
         it++) {
        const string &uid = it->first;
        const string &revision = it->second;

        if (deleteLocal) {
            deleteItem(uid);
            m_trackingNode->removeProperty(uid.c_str());
        } else {
            // always remember the item, need full list to find deleted items
            m_allItems.addItem(uid);

            if (needPartial) {
                arrayptr<char> serverRevision(m_trackingNode->readPropertyValue(uid.c_str()));

                if (!serverRevision || !serverRevision[0]) {
                    m_newItems.addItem(uid);
                    m_trackingNode->setPropertyValue(uid.c_str(), revision.c_str());
                } else {
                    if (revision !=  serverRevision.get()) {
                        m_updatedItems.addItem(uid);
                        m_trackingNode->setPropertyValue(uid.c_str(), revision.c_str());
                    }
                }
            }
        }
    }

    if (needPartial) {
        ArrayList uids;
        ArrayList modTimes;
        m_trackingNode->readProperties(&uids, &modTimes);
        for (int i = 0; i < uids.size(); i++ ) {
            const StringBuffer *uid = (StringBuffer *)uids[i];
            if (m_allItems.find(uid->c_str()) == m_allItems.end()) {
                m_deletedItems.addItem(uid->c_str());
                m_trackingNode->removeProperty(uid->c_str());
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
        m_trackingNode->update(false);
    } else {
        // SyncEvolution's error handling for failed sources
        // forces a slow sync the next time. Therefore the
        // content of the tracking node is irrelevant in
        // case of a failure.
    }
}

void TrackingSyncSource::exportData(ostream &out)
{
    RevisionMap_t revisions;
    listAllItems(revisions);

    for (RevisionMap_t::const_iterator it = revisions.begin();
         it != revisions.end();
         it++) {
        const string &uid = it->first;
        eptr<SyncItem> item(createItem(uid), "sync item");

        out << (char *)item->getData() << "\n";
    }
}

int TrackingSyncSource::addItemThrow(SyncItem& item)
{
    string uid;
    string revision = insertItem(uid, item);
    item.setKey(uid.c_str());
    m_trackingNode->setPropertyValue(uid.c_str(), revision.c_str());
    return STC_OK;
}

int TrackingSyncSource::updateItemThrow(SyncItem& item)
{
    const string olduid = item.getKey();
    string newuid = olduid;
    string revision = insertItem(newuid, item);
    if (olduid != newuid) {
        m_trackingNode->removeProperty(olduid.c_str());
    }
    item.setKey(newuid.c_str());
    m_trackingNode->setPropertyValue(newuid.c_str(), revision.c_str());
    return STC_OK;
}

int TrackingSyncSource::deleteItemThrow(SyncItem& item)
{
    const string uid = item.getKey();
    deleteItem(uid);
    m_trackingNode->removeProperty(uid.c_str());
    return STC_OK;
}

void TrackingSyncSource::setItemStatusThrow(const char *uid, int status)
{
}
