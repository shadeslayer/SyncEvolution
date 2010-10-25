/*
 * Copyright (C) 2010 Intel Corporation
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

#include <syncevo/MapSyncSource.h>
#include <syncevo/PrefixConfigNode.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

/**
 * This special ConfigNode intercepts luid/revision manipulation by
 * SyncSourceRevisions. It ensures that the revision string of all sub
 * items in a merged item have the same revision string by only
 * storing the revision string once in a special "rev-" property.
 *
 * The original "item-" properties just indicate that a certain
 * luid exists by having a "1" value.
 *
 * This "rev-" property is reference counted using "<counter>-<revision>"
 * as value. This is necessary so that removeProperty() can decide
 * quickly whether it also has to remove the "rev-" property in
 * addition to the "item-" property itself.
 */
class MapConfigNode : public PrefixConfigNode
{
    /** same underlying storage as PrefixConfigNode, but with "rev-" instead of "item-" prefix */
    boost::shared_ptr<ConfigNode> m_revisions;

public:
    MapConfigNode(const boost::shared_ptr<ConfigNode> &storage) :
        PrefixConfigNode("item-", storage),
        m_revisions(new PrefixConfigNode("rev-", storage))
    {}

    static boost::shared_ptr<MapConfigNode> createNode(const SyncSourceParams &params)
    {
        boost::shared_ptr<ConfigNode> storage(new SafeConfigNode(params.m_nodes.getTrackingNode()));
        boost::shared_ptr<MapConfigNode> res(new MapConfigNode(storage));
        return res;
    }

    virtual string readProperty(const string &luid) const
    {
        StringPair ids = MapSyncSource::splitLUID(luid);
        if (PrefixConfigNode::readProperty(luid).empty()) {
            // item does not exist, does not have revision
            return "";
        } else {
            // item exists, return shared revision
            std::string rev = m_revisions->readProperty(ids.first);
            size_t offset = rev.find('-');
            rev.erase(0, offset + 1);
            return rev;
        }
    }

    virtual void setProperty(const string &luid,
                             const string &revision,
                             const string &comment = string(""),
                             const string *defValue = NULL)
    {
        StringPair ids = MapSyncSource::splitLUID(luid);
        std::string value = m_revisions->readProperty(ids.first);
        int refcount = atoi(value.c_str());
        if (PrefixConfigNode::readProperty(luid).empty()) {
            // new item
            PrefixConfigNode::setProperty(luid, "1");
            refcount++;
        }
        m_revisions->setProperty(ids.first,
                                 StringPrintf("%d-%s", refcount, revision.c_str()));
    }

    virtual void readProperties(ConfigProps &props) const
    {
        PrefixConfigNode::readProperties(props);
        BOOST_FOREACH(ConfigProps::value_type &entry, props) {
            StringPair ids = MapSyncSource::splitLUID(entry.first);
            entry.second = m_revisions->readProperty(ids.first);
            size_t offset = entry.second.find('-');
            entry.second.erase(0, offset + 1);
        }
    }

    virtual void removeProperty(const string &luid)
    {
        if (!PrefixConfigNode::readProperty(luid).empty()) {
            StringPair ids = MapSyncSource::splitLUID(luid);
            std::string value = m_revisions->readProperty(ids.first);
            int refcount = atoi(value.c_str());
            size_t offset = value.find('-');
            refcount--;
            if (!refcount) {
                m_revisions->removeProperty(ids.first);
            } else {
                value = StringPrintf("%d-%s", refcount, value.c_str() + offset + 1);
                m_revisions->setProperty(ids.first, value);
            }
            PrefixConfigNode::removeProperty(luid);
        }
    }

    /**
     * remove luid (if it exists), set new revision string
     */
    void remove(const string &luid, const string &rev)
    {
        StringPair ids = MapSyncSource::splitLUID(luid);
        std::string value = m_revisions->readProperty(ids.first);
        int refcount = atoi(value.c_str());
        if (!PrefixConfigNode::readProperty(luid).empty()) {
            refcount--;
            PrefixConfigNode::removeProperty(luid);
        }
        if (!refcount) {
            m_revisions->removeProperty(ids.first);
        } else {
            value = StringPrintf("%d-%s", refcount, rev.c_str());
            m_revisions->setProperty(ids.first, value);
        }
    }
};

SDKInterface *SubSyncSource::getSynthesisAPI() const
{
    return m_parent ?
        m_parent->getSynthesisAPI() :
        NULL;
}

MapSyncSource::MapSyncSource(const SyncSourceParams &params,
                             int granularitySeconds,
                             const boost::shared_ptr<SubSyncSource> &sub) :
    TrackingSyncSource(params, granularitySeconds,
                       MapConfigNode::createNode(params)),
    m_sub(sub)
{
    // parameters don't matter because actual implementation is in sub source
    SyncSourceLogging::init(std::list<std::string>(),
                            ", ",
                            m_operations);
    m_sub->setParent(this);
    // Redirect backup/restore into sub source, if it defines a backup
    // operation. Otherwise continue to use our own,
    // SyncSourceRevision based implementation. The expectation is
    // that a custom backup operation implies a custom restore,
    // because of custom data formats in the data dump. Therefore if()
    // only checks the m_backupData pointer.
    if (m_sub->getOperations().m_backupData) {
        m_operations.m_backupData = m_sub->getOperations().m_backupData;
        m_operations.m_restoreData = m_sub->getOperations().m_restoreData;
    }
}

void MapSyncSource::listAllItems(SyncSourceRevisions::RevisionMap_t &revisions)
{
    SubSyncSource::SubRevisionMap_t subrevisions;
    m_sub->listAllSubItems(subrevisions);
    BOOST_FOREACH(const SubSyncSource::SubRevisionMap_t::value_type &subentry,
                  subrevisions) {
        const std::string &uid = subentry.first;
        const std::string &rev = subentry.second.first;
        BOOST_FOREACH(const std::string &subid, subentry.second.second) {
            std::string luid = createLUID(uid, subid);
            revisions[luid] = rev;
        }
    }
}

SyncSourceRaw::InsertItemResult MapSyncSource::insertItem(const std::string &luid, const std::string &item, bool raw)
{
    StringPair ids = splitLUID(luid);
    checkFlush(ids.first);
    SubSyncSource::SubItemResult res = m_sub->insertSubItem(ids.first, ids.second, item);
    return SyncSourceRaw::InsertItemResult(createLUID(res.m_uid, res.m_subid),
                                           res.m_revision, res.m_merged);
}

void MapSyncSource::readItem(const std::string &luid, std::string &item, bool raw)
{
    StringPair ids = splitLUID(luid);
    checkFlush(ids.first);
    m_sub->readSubItem(ids.first, ids.second, item);
}

void MapSyncSource::removeItem(const string &luid)
{
    StringPair ids = splitLUID(luid);
    checkFlush(ids.first);
    std::string rev = m_sub->removeSubItem(ids.first, ids.second);
    // Removal and updating is special, SyncSourceRevisions
    // cannot do it for us because the semantic of removeItem()
    // is different. The real work is in the MapConfigNode
    // which we pass to SyncSourceRevisions.
    static_cast<MapConfigNode &>(getTrackingNode()).remove(luid, rev);
}

std::string MapSyncSource::getDescription(const string &luid)
{
    StringPair ids = splitLUID(luid);
    checkFlush(ids.first);
    return m_sub->getSubDescription(ids.first, ids.second);
}

std::string MapSyncSource::createLUID(const std::string &uid, const std::string &subid)
{
    std::string luid = m_escape.escape(uid);
    if (!subid.empty()) {
        luid += '/';
        luid += m_escape.escape(subid);
    }
    return luid;
}

std::pair<std::string, std::string> MapSyncSource::splitLUID(const std::string &luid)
{
    size_t index = luid.find('/');
    if (index != luid.npos) {
        return make_pair(m_escape.unescape(luid.substr(0, index)),
                         m_escape.unescape(luid.substr(index + 1)));
    } else {
        return make_pair(m_escape.unescape(luid), "");
    }
}

void MapSyncSource::checkFlush(const std::string &uid)
{
    if (uid != m_oldIDs &&
        !m_oldIDs.empty()) {
        m_sub->flushItem(m_oldIDs);
        m_oldIDs = uid;
    }
}

StringEscape MapSyncSource::m_escape('\\', "/");

SE_END_CXX
