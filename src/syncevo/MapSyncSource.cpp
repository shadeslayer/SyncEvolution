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

SDKInterface *SubSyncSource::getSynthesisAPI() const
{
    return m_parent ?
        m_parent->getSynthesisAPI() :
        NULL;
}



MapSyncSource::MapSyncSource(const SyncSourceParams &params,
                             const boost::shared_ptr<SubSyncSource> &sub) :
    TestingSyncSource(params),
    m_sub(sub)
{
    boost::shared_ptr<ConfigNode> safeNode(new SafeConfigNode(params.m_nodes.getTrackingNode()));
    m_trackingNode.reset(new PrefixConfigNode("item-", safeNode));
    m_metaNode = safeNode;

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

void MapSyncSource::enableServerMode()
{
    SyncSourceAdmin::init(m_operations, this);
    SyncSourceBlob::init(m_operations, getCacheDir());
}

bool MapSyncSource::serverModeEnabled() const
{
    return m_operations.m_loadAdminData;
}

void MapSyncSource::detectChanges(SyncSourceRevisions::ChangeMode mode)
{
    // erase content which might have been set in a previous call
    reset();

    // read old list from node (matches endSync() code)
    m_revisions.clear();
    ConfigProps props;
    m_trackingNode->readProperties(props);
    BOOST_FOREACH(const StringPair &prop, props) {
        const std::string &mainid = prop.first;
        const std::string &value = prop.second;
        size_t pos = value.find('/');
        bool okay = false;
        if (pos == 0) {
            pos = value.find('/', 1);
            if (pos != value.npos) {
                std::string revision = m_escape.unescape(value.substr(1, pos - 1));
                size_t nextpos = value.find('/', pos + 1);
                if (nextpos != value.npos) {
                    std::string uid = m_escape.unescape(value.substr(pos + 1, nextpos - pos - 1));
                    SubRevisionEntry &ids = m_revisions[mainid];
                    ids.m_revision = revision;
                    ids.m_uid = uid;
                    pos = nextpos;
                    while ((nextpos = value.find('/', pos + 1)) != value.npos) {
                        std::string subid = m_escape.unescape(value.substr(pos + 1, nextpos - pos - 1));
                        ids.m_subids.insert(subid);
                        pos = nextpos;
                    }
                    okay = true;
                }
            }
        }
        if (!okay) {
            SE_LOG_DEBUG(this, NULL, "unsupported or corrupt revision entry: %s = %s",
                         mainid.c_str(),
                         value.c_str());
        }
    }

    // determine how to update that list and find changes
    switch (mode) {
    case SyncSourceRevisions::CHANGES_NONE:
        // nothing to do, just tell sub source
        m_sub->setAllSubItems(m_revisions);
        break;
    case SyncSourceRevisions::CHANGES_FULL: {
        // update the list and compare to find changes
        SubRevisionMap_t newRevisions;
        if (m_revisions.empty()) {
            // nothing to reuse, just ask for current items
            m_sub->listAllSubItems(newRevisions);
        } else {
            // update old information
            newRevisions = m_revisions;
            m_sub->updateAllSubItems(newRevisions);
        }
        // deleted items...
        BOOST_FOREACH(const SubRevisionMap_t::value_type &entry,
                      m_revisions) {
            const std::string &mainid = entry.first;
            const SubRevisionEntry &ids = entry.second;
            if (newRevisions.find(entry.first) == newRevisions.end()) {
                BOOST_FOREACH(const std::string &subid, ids.m_subids) {
                    addItem(createLUID(mainid, subid), DELETED);
                }
            }
        }
        // added or updated items...
        BOOST_FOREACH(const SubRevisionMap_t::value_type &entry,
                      newRevisions) {
            const std::string &mainid = entry.first;
            const SubRevisionEntry &ids = entry.second;
            SubRevisionMap_t::iterator it = m_revisions.find(entry.first);
            if (it == m_revisions.end()) {
                // all sub-items are added
                BOOST_FOREACH(const std::string &subid, ids.m_subids) {
                    addItem(createLUID(mainid, subid), NEW);
                }
            } else if (it->second.m_revision != ids.m_revision) {
                // merged item was modified, some of its sub-items
                // might have been removed...
                BOOST_FOREACH(const std::string &subid, it->second.m_subids) {
                    if (ids.m_subids.find(subid) == ids.m_subids.end()) {
                        addItem(createLUID(mainid, subid), DELETED);
                    }
                }
                // ... or added/modified
                BOOST_FOREACH(const std::string &subid, ids.m_subids) {
                    if (it->second.m_subids.find(subid) == it->second.m_subids.end()){ 
                        addItem(createLUID(mainid, subid), NEW);
                    } else {
                        addItem(createLUID(mainid, subid), UPDATED);
                    }
                }
            }
        }
        // continue with up-to-date list
        m_revisions = newRevisions;
        break;
    }
    case SyncSourceRevisions::CHANGES_SLOW:
        // replace with current list, don't bother about finding changes
        m_revisions.clear();
        m_sub->listAllSubItems(m_revisions);
        break;
    default:
        // ?!
        throwError("unknown change mode");
        break;
    }

    // always set the full list of luids in SyncSourceChanges
    BOOST_FOREACH(const SubRevisionMap_t::value_type &entry,
                  m_revisions) {
        const std::string &mainid = entry.first;
        const SubRevisionEntry &ids = entry.second;
        BOOST_FOREACH(const std::string &subid, ids.m_subids) {
            addItem(createLUID(mainid, subid));
        }
    }
}

void MapSyncSource::beginSync(const std::string &lastToken, const std::string &resumeToken)
{
    m_sub->begin();

    // TODO: copy-and-pasted from TrackingSyncSource::beginSync() - refactor!
    // vvvvvvvvvvvvvvvvv

    // use the most reliable (and most expensive) method by default
    SyncSourceRevisions::ChangeMode mode = SyncSourceRevisions::CHANGES_FULL;

    // resume token overrides the normal token; safe to ignore in most
    // cases and this detectChanges() is done independently of the
    // token, but let's do it right here anyway
    string token;
    if (!resumeToken.empty()) {
        token = resumeToken;
    } else {
        token = lastToken;
    }
    // slow sync if token is empty
    if (token.empty()) {
        SE_LOG_DEBUG(this, NULL, "slow sync or testing, do full item scan to detect changes");
        mode = SyncSourceRevisions::CHANGES_SLOW;
    } else {
        string oldRevision = m_metaNode->readProperty("databaseRevision");
        if (!oldRevision.empty()) {
            string newRevision = m_sub->subDatabaseRevision();
            SE_LOG_DEBUG(this, NULL, "old database revision '%s', new revision '%s'",
                         oldRevision.c_str(),
                         newRevision.c_str());
            if (newRevision == oldRevision) {
                SE_LOG_DEBUG(this, NULL, "revisions match, no item changes");
                mode = SyncSourceRevisions::CHANGES_NONE;
            }

            // Reset old revision. If anything goes wrong, then we
            // don't want to rely on a possibly incorrect optimization.
            m_metaNode->setProperty("databaseRevision", "");
            m_metaNode->flush();
        }
    }
    if (mode == SyncSourceRevisions::CHANGES_FULL) {
        SE_LOG_DEBUG(this, NULL, "using full item scan to detect changes");
    }

    detectChanges(mode);
    // TODO: refactor ^^^^^^^^^^^^^^^^^^^^^^^^
}

std::string MapSyncSource::endSync(bool success)
{
    m_sub->endSubSync(success);

    // TODO: refactor vvvvvvvvvvvvvvvv

    if (success) {
        string updatedRevision = m_sub->subDatabaseRevision();
        m_metaNode->setProperty("databaseRevision", updatedRevision);

        // This part is different from TrackingSyncSource: our luid/rev information
        // is in m_revisions and only gets dumped into m_trackingNode at the very end here.
        m_trackingNode->clear();
        BOOST_FOREACH(const SubRevisionMap_t::value_type &entry,
                      m_revisions) {
            const std::string &mainid = entry.first;
            const SubRevisionEntry &ids = entry.second;
            std::stringstream buffer;
            buffer << '/' << m_escape.escape(ids.m_revision) << '/';
            buffer << m_escape.escape(ids.m_uid) << '/';
            BOOST_FOREACH(const std::string &subid, ids.m_subids) {
                buffer << m_escape.escape(subid) << '/';
            }
            m_trackingNode->setProperty(mainid, buffer.str());
        }

        // flush both nodes, just in case; in practice, the properties
        // end up in the same file and only get flushed once
        m_trackingNode->flush();
        m_metaNode->flush();
    } else {
        // The Synthesis docs say that we should rollback in case of
        // failure. Cannot do that for data, so lets at least keep
        // the revision map unchanged.
    }

    // no token handling at the moment (not needed for clients):
    // return a non-empty token to distinguish an incremental
    // sync from a slow sync in beginSync()
    return "1";

    // TODO: refactor ^^^^^^^^^^^^^^^^^^^
}


SyncSourceRaw::InsertItemResult MapSyncSource::insertItem(const std::string &luid, const std::string &item)
{
    StringPair ids = splitLUID(luid);
    SubSyncSource::SubItemResult res = m_sub->insertSubItem(ids.first, ids.second, item);
    // anything changed?
    if (res.m_state != ITEM_NEEDS_MERGE) {
        SubRevisionEntry &entry = m_revisions[res.m_mainid];
        entry.m_uid = res.m_uid;
        entry.m_revision = res.m_revision;
        entry.m_subids.insert(res.m_subid);
    }
    return SyncSourceRaw::InsertItemResult(createLUID(res.m_mainid, res.m_subid),
                                           res.m_revision, res.m_state);
}

void MapSyncSource::readItem(const std::string &luid, std::string &item)
{
    StringPair ids = splitLUID(luid);
    m_sub->readSubItem(ids.first, ids.second, item);
}

void MapSyncSource::deleteItem(const string &luid)
{
    StringPair ids = splitLUID(luid);
    std::string rev = m_sub->removeSubItem(ids.first, ids.second);
    SubRevisionMap_t::iterator it = m_revisions.find(ids.first);
    if (it != m_revisions.end()) {
        it->second.m_subids.erase(ids.second);
        if (it->second.m_subids.empty()) {
            // last sub item removed, remove merged item
            m_revisions.erase(it);
        } else {
            // still some sub items, update revision in merged item
            it->second.m_revision = rev;
        }
    }
}

std::string MapSyncSource::getDescription(const string &luid)
{
    StringPair ids = splitLUID(luid);
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

StringEscape MapSyncSource::m_escape('%', "/");

void MapSyncSource::removeAllItems()
{
    BOOST_FOREACH(const SubRevisionMap_t::value_type &entry,
                  m_revisions) {
        m_sub->removeMergedItem(entry.first);
    }
    m_revisions.clear();
}

SE_END_CXX
