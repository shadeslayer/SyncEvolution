/*
 * Copyright (C) 2008-2009 Patrick Ohly <patrick.ohly@gmx.de>
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

#include <syncevo/TrackingSyncSource.h>
#include <syncevo/SafeConfigNode.h>
#include <syncevo/PrefixConfigNode.h>

#include <boost/bind.hpp>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

TrackingSyncSource::TrackingSyncSource(const SyncSourceParams &params,
                                       int granularitySeconds,
                                       const boost::shared_ptr<ConfigNode> &trackingNode) :
    TestingSyncSource(params),
    m_trackingNode(trackingNode)
{
    if (!m_trackingNode) {
        m_trackingNode.reset(new PrefixConfigNode("item-",
                                                  boost::shared_ptr<ConfigNode>(new SafeConfigNode(params.m_nodes.getTrackingNode()))));
    }
    m_operations.m_checkStatus = boost::bind(&TrackingSyncSource::checkStatus, this, _1);
    m_operations.m_isEmpty = boost::bind(&TrackingSyncSource::isEmpty, this);
    SyncSourceRevisions::init(this, this, granularitySeconds, m_operations);
}

void TrackingSyncSource::checkStatus(SyncSourceReport &changes)
{
    detectChanges(*m_trackingNode);
    // copy our item counts into the report
    changes.setItemStat(ITEM_LOCAL, ITEM_ADDED, ITEM_TOTAL, getNewItems().size());
    changes.setItemStat(ITEM_LOCAL, ITEM_UPDATED, ITEM_TOTAL, getUpdatedItems().size());
    changes.setItemStat(ITEM_LOCAL, ITEM_REMOVED, ITEM_TOTAL, getDeletedItems().size());
    changes.setItemStat(ITEM_LOCAL, ITEM_ANY, ITEM_TOTAL, getAllItems().size());
}

void TrackingSyncSource::beginSync(const std::string &lastToken, const std::string &resumeToken)
{
    detectChanges(*m_trackingNode);
}

std::string TrackingSyncSource::endSync(bool success)
{
    // store changes persistently
    flush();

    if (success) {
        m_trackingNode->flush();
    } else {
        // The Synthesis docs say that we should rollback in case of
        // failure. Cannot do that for data, so lets at least keep
        // the revision map unchanged.
    }

    // no token handling at the moment (not needed for clients)
    return "";
}

TrackingSyncSource::InsertItemResult TrackingSyncSource::insertItem(const std::string &luid, const std::string &item)
{
    InsertItemResult res = insertItem(luid, item, false);
    updateRevision(*m_trackingNode, luid, res.m_luid, res.m_revision);
    return res;
}

TrackingSyncSource::InsertItemResult TrackingSyncSource::insertItemRaw(const std::string &luid, const std::string &item)
{
    InsertItemResult res = insertItem(luid, item, true);
    updateRevision(*m_trackingNode, luid, res.m_luid, res.m_revision);
    return res;
}

void TrackingSyncSource::readItem(const std::string &luid, std::string &item)
{
    readItem(luid, item, false);
}

void TrackingSyncSource::readItemRaw(const std::string &luid, std::string &item)
{
    readItem(luid, item, true);
}

void TrackingSyncSource::deleteItem(const std::string &luid)
{
    removeItem(luid);
    deleteRevision(*m_trackingNode, luid);
}

void TrackingSyncSource::enableServerMode()
{
    SyncSourceAdmin::init(m_operations, this);
    SyncSourceBlob::init(m_operations, getCacheDir());
}

bool TrackingSyncSource::serverModeEnabled() const
{
    return m_operations.m_loadAdminData;
}

std::string TrackingSyncSource::getPeerMimeType() const
{
    return getMimeType();
}

SE_END_CXX
