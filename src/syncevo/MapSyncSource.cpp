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
 * The original "item-" properties indicate that a certain
 * luid exists. They are also used to store the optional UID string
 * for the luid. That UID string must be set before setProperty()
 * is called by SyncSourceRevisions, in other words, before returning
 * luid/rev information to it.
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

    /** temporary storage for mainid -> uid mapping */
    StringMap m_uids;

    /** escape / in uid with %2F, so that splitMainIDValue() can use / as separator */
    StringEscape m_escape;

    /** compose rev-<mainid> value */
    std::string composeMainIDValue(int refcount,
                                   const std::string &uid,
                                   const std::string &revision)
    {
        return StringPrintf("%d/%s/%s",
                            refcount,
                            m_escape.escape(uid).c_str(),
                            revision.c_str());
    }

    /** split rev-<mainid> value again */
    void splitMainIDValue(const std::string &value,
                          int refcount,
                          std::string &uid,
                          std::string &revision) const
    {
        refcount = atoi(value.c_str());
        size_t offset1 = value.find('/');
        size_t offset2 = offset1 != std::string::npos ?
            value.find('/', offset1 + 1) :
            std::string::npos;
        if (offset2 != std::string::npos) {
            uid = m_escape.unescape(value.substr(offset1 + 1,
                                                 offset2 - offset1 - 1));
            revision = value.substr(offset2 + 1);
        }
    }

public:
    MapConfigNode(const boost::shared_ptr<ConfigNode> &storage) :
        PrefixConfigNode("item-", storage),
        m_revisions(new PrefixConfigNode("rev-", storage)),
        m_escape('%', "/")
    {}

    /**
     * Can be used to store a uid string for each mainid.
     * Must be called before setProperty() ends up being called.
     */
    void rememberUID(const std::string &mainid, const std::string &uid)
    {
        m_uids[mainid] = uid;
    }

    /**
     * Retrieves uid for a certain luid from the underlying key/value store.
     * This is not the complementary method for rememberUID()!
     */
    std::string getUID(const std::string &mainid)
    {
        std::string value = m_revisions->readProperty(mainid);
        int refcount;
        std::string uid, revision;
        splitMainIDValue(value, refcount, uid, revision);
        return uid;
    }

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
            std::string value = m_revisions->readProperty(ids.first);
            int refcount;
            std::string uid, revision;
            splitMainIDValue(value, refcount, uid, revision);
            return revision;
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
            // new item, create it together with its UID (passed to us
            // out-of-band)
            PrefixConfigNode::setProperty(luid, "1");
            refcount++;
        }
        m_revisions->setProperty(ids.first,
                                 composeMainIDValue(refcount, m_uids[ids.first], revision));
    }

    virtual void readProperties(ConfigProps &props) const
    {
        PrefixConfigNode::readProperties(props);
        BOOST_FOREACH(ConfigProps::value_type &entry, props) {
            StringPair ids = MapSyncSource::splitLUID(entry.first);
            std::string value = m_revisions->readProperty(ids.first);
            int refcount;
            std::string mainid, revision;
            splitMainIDValue(value,
                             refcount,
                             mainid,
                             revision);
            entry.second = revision;
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
        const std::string &mainid = subentry.first;
        const SubSyncSource::SubRevisionEntry &entry = subentry.second;
        BOOST_FOREACH(const std::string &subid, entry.m_subids) {
            std::string luid = createLUID(mainid, subid);
            revisions[luid] = entry.m_revision;
        }
    }
}

void MapSyncSource::setAllItems(const SyncSourceRevisions::RevisionMap_t &revisions)
{
    SubSyncSource::SubRevisionMap_t subrevisions;
    BOOST_FOREACH(const SyncSourceRevisions::RevisionMap_t::value_type &entry,
                  revisions) {
        const std::string &luid = entry.first;
        const std::string &rev = entry.second;
        StringPair ids = splitLUID(luid);
        SubSyncSource::SubRevisionEntry &subentry = subrevisions[ids.first];
        if (subentry.m_revision.empty()) {
            // must be new, store shared properties
            subentry.m_revision = rev;
            subentry.m_uid = static_cast<MapConfigNode &>(getTrackingNode()).getUID(ids.first);
        }
        subentry.m_subids.insert(ids.second);
    }
    m_sub->setAllSubItems(subrevisions);
}


SyncSourceRaw::InsertItemResult MapSyncSource::insertItem(const std::string &luid, const std::string &item, bool raw)
{
    StringPair ids = splitLUID(luid);
    checkFlush(ids.first);
    SubSyncSource::SubItemResult res = m_sub->insertSubItem(ids.first, ids.second, item);
    static_cast<MapConfigNode &>(getTrackingNode()).rememberUID(res.m_mainid, res.m_uid);
    return SyncSourceRaw::InsertItemResult(createLUID(res.m_mainid, res.m_subid),
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

void MapSyncSource::checkFlush(const std::string &luid)
{
    if (luid != m_oldLUID &&
        !m_oldLUID.empty()) {
        m_sub->flushItem(m_oldLUID);
        m_oldLUID = luid;
    }
}

StringEscape MapSyncSource::m_escape('\\', "/");

SE_END_CXX
