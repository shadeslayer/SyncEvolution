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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef ENABLE_ACTIVESYNC

#include "ActiveSyncSource.h"

#include <eas-errors.h>

#include <stdlib.h>
#include <errno.h>

/* #include <eas-connection-errors.h> */
#include <syncevo/declarations.h>
SE_BEGIN_CXX

void EASItemUnref(EasItemInfo *info) { g_object_unref(&info->parent_instance); }
void GStringUnref(char *str) { g_free(str); }

void ActiveSyncSource::enableServerMode()
{
    SyncSourceAdmin::init(m_operations, this);
    SyncSourceBlob::init(m_operations, getCacheDir());
}
bool ActiveSyncSource::serverModeEnabled() const
{
    return m_operations.m_loadAdminData;
}

ActiveSyncSource::Databases ActiveSyncSource::getDatabases()
{
    Databases result;
    // empty string always selects the default database
    result.push_back(Database("", "", true));
    return result;
}


void ActiveSyncSource::open()
{
    // extract account ID and throw error if missing
    std::string username = m_context->getSyncUsername();

    m_account = username.c_str();
    m_folder = getDatabaseID();

    // create handler
    m_handler.set(eas_sync_handler_new(m_account), "EAS handler");
}

void ActiveSyncSource::close()
{
    // free handler if not done already
    m_handler.set(NULL);
}

void ActiveSyncSource::beginSync(const std::string &lastToken, const std::string &resumeToken)
{
    // erase content which might have been set in a previous call
    reset();

    // claim item node for ids, if not done yet
    if (m_itemNode && !m_ids) {
        m_ids.swap(m_itemNode);
    }

    // incremental sync (non-empty token) or start from scratch
    m_startSyncKey = lastToken;
    if (lastToken.empty()) {
        // slow sync: wipe out cached list of IDs, will be filled anew below
        SE_LOG_DEBUG(this, NULL, "sync key empty, starting slow sync");
        m_ids->clear();
    } else {
        SE_LOG_DEBUG(this, NULL, "sync key %s, starting incremental sync", lastToken.c_str());
    }

    GErrorCXX gerror;
    gboolean moreAvailable = TRUE;

    m_currentSyncKey = m_startSyncKey;

    // same logic as in ActiveSyncCalendarSource::beginSync()

    bool slowSync = false;
    for (bool firstIteration = true;
         moreAvailable;
         firstIteration = false) {
        gchar *buffer = NULL;
        EASItemsCXX created, updated;
        EASIdsCXX deleted;
        bool wasSlowSync = m_currentSyncKey.empty();

        if (!eas_sync_handler_get_items(m_handler,
                                        m_currentSyncKey.c_str(),
                                        &buffer,
                                        getEasType(),
                                        m_folder.c_str(),
                                        created, updated, deleted,
                                        &moreAvailable,
                                        gerror)) {
            if (gerror.m_gerror &&
                /*
                gerror.m_gerror->domain == EAS_TYPE_CONNECTION_ERROR &&
                gerror.m_gerror->code == EAS_CONNECTION_SYNC_ERROR_INVALIDSYNCKEY && */
                gerror.m_gerror->message &&
                !strcmp(gerror.m_gerror->message, "Sync error: Invalid synchronization key") &&
                firstIteration) {
                // fall back to slow sync
                slowSync = true;
                m_currentSyncKey = "";
                m_ids->clear();
                continue;
            }

            gerror.throwError("reading ActiveSync changes");
        }
        GStringPtr bufferOwner(buffer, "reading changes: empty sync key returned");

        // TODO: Test that we really get an empty token here for an unexpected slow
        // sync. If not, we'll start an incremental sync here and later the engine
        // will ask us for older, unmodified item content which we won't have.

        // populate ID lists and content cache
        BOOST_FOREACH(EasItemInfo *item, created) {
            if (!item->server_id) {
                throwError("no server ID for new eas item");
            }
            string luid(item->server_id);
            if (luid.empty()) {
                throwError("empty server ID for new eas item");
            }
            SE_LOG_DEBUG(this, NULL, "new item %s", luid.c_str());
            addItem(luid, NEW);
            m_ids->setProperty(luid, "1");
            if (!item->data) {
                throwError(StringPrintf("no body returned for new eas item %s", luid.c_str()));
            }
            m_items[luid] = item->data;
        }
        BOOST_FOREACH(EasItemInfo *item, updated) {
            if (!item->server_id) {
                throwError("no server ID for updated eas item");
            }
            string luid(item->server_id);
            if (luid.empty()) {
                throwError("empty server ID for updated eas item");
            }
            SE_LOG_DEBUG(this, NULL, "updated item %s", luid.c_str());
            addItem(luid, UPDATED);
            // m_ids.setProperty(luid, "1"); not necessary, should already exist (TODO: check?!)
            if (!item->data) {
                throwError(StringPrintf("no body returned for updated eas item %s", luid.c_str()));
            }
            m_items[luid] = item->data;
        }
        BOOST_FOREACH(const char *serverID, deleted) {
            if (!serverID) {
                throwError("no server ID for deleted eas item");
            }
            string luid(serverID);
            if (luid.empty()) {
                throwError("empty server ID for deleted eas item");
            }
            SE_LOG_DEBUG(this, NULL, "deleted item %s", luid.c_str());
            addItem(luid, DELETED);
            m_ids->removeProperty(luid);
        }

        // update key
        m_currentSyncKey = buffer;

        // Google  hack: if we started with an empty sync key (= slow sync)
        // and got no results (= existing items), then try one more time,
        // because Google only seems to report results when asked with
        // a valid sync key. As an additional sanity check make sure that
        // we have a valid sync key now.
        if (wasSlowSync &&
            created.empty() &&
            !m_currentSyncKey.empty()) {
            moreAvailable = true;
        }
    }

    // now also generate full list of all current items:
    // old items + new (added to m_ids above) - deleted (removed above)
    ConfigProps props;
    m_ids->readProperties(props);
    BOOST_FOREACH(const StringPair &entry, props) {
        const std::string &luid = entry.first;
        SE_LOG_DEBUG(this, NULL, "existing item %s", luid.c_str());
        addItem(luid, ANY);
    }

    if (slowSync) {
        // tell engine that we need a slow sync
        SE_THROW_EXCEPTION_STATUS(StatusException,
                                  "ActiveSync error: Invalid synchronization key",
                                  STATUS_SLOW_SYNC_508);
    }
}

std::string ActiveSyncSource::endSync(bool success)
{
    // store current set of items
    if (!success) {
        m_ids->clear();
    }
    m_ids->flush();

    // let engine do incremental sync next time or start from scratch
    // in case of failure
    std::string newSyncKey = success ? m_currentSyncKey : "";
    SE_LOG_DEBUG(this, NULL, "next sync key %s", newSyncKey.empty() ? "empty" : newSyncKey.c_str());
    return newSyncKey;
}

void ActiveSyncSource::deleteItem(const string &luid)
{
    // asking to delete a non-existent item via ActiveSync does not
    // trigger an error; this is expected by the caller, so detect
    // the problem by looking up the item in our list (and keep the
    // list up-to-date elsewhere)
    if (m_ids && m_ids->readProperty(luid).empty()) {
        throwError(STATUS_NOT_FOUND, "item not found: " + luid);
    }

    // send delete request
    // TODO (?): batch delete requests
    GListCXX<char, GSList> items;
    items.push_back((char *)luid.c_str());

    GErrorCXX gerror;
    char *buffer;
    if (!eas_sync_handler_delete_items(m_handler,
                                       m_currentSyncKey.c_str(),
                                       &buffer,
                                       getEasType(),
                                       m_folder.c_str(),
                                       items,
                                       gerror)) {
        gerror.throwError("deleting eas item");
    }
    GStringPtr bufferOwner(buffer, "delete items: empty sync key returned");

    // remove from item list
    if (m_ids) {
        m_items.erase(luid);
        m_ids->removeProperty(luid);
    }

    // update key
    m_currentSyncKey = buffer;
}

SyncSourceSerialize::InsertItemResult ActiveSyncSource::insertItem(const std::string &luid, const std::string &data)
{
    SyncSourceSerialize::InsertItemResult res;

    EASItemPtr tmp(eas_item_info_new(), "EasItem");
    EasItemInfo *item = tmp.get();
    if (!luid.empty()) {
        // update
        item->server_id = g_strdup(luid.c_str());
    } else {
        // add
        // TODO: is a local id needed? We don't have one.
    }
    item->data = g_strdup(data.c_str());
    EASItemsCXX items;
    items.push_front(tmp.release());

    GErrorCXX gerror;
    char *buffer;

    // distinguish between update (existing luid)
    // or creation (empty luid)
    if (luid.empty()) {
        // send item to server
        if (!eas_sync_handler_add_items(m_handler,
                                        m_currentSyncKey.c_str(),
                                        &buffer,
                                        getEasType(),
                                        m_folder.c_str(),
                                        items,
                                        gerror)) {
            gerror.throwError("adding eas item");
        }
        if (!item->server_id) {
            throwError("no server ID for new eas item");
        }
        // get new ID from updated item
        res.m_luid = item->server_id;
        if (res.m_luid.empty()) {
            throwError("empty server ID for new eas item");
        }

        // TODO: if someone else has inserted a new calendar item
        // with the same UID as the one we are trying to insert here,
        // what will happen? Does the ActiveSync server prevent
        // adding our own version of the item or does it merge?
        // res.m_merged = ???
    } else {
        // update item on server
        if (!eas_sync_handler_update_items(m_handler,
                                           m_currentSyncKey.c_str(),
                                           &buffer,
                                           getEasType(),
                                           m_folder.c_str(),
                                           items,
                                           gerror)) {
            gerror.throwError("updating eas item");
        }
        res.m_luid = luid;
    }
    GStringPtr bufferOwner(buffer, "insert item: empty sync key returned");

    // add/update in cache
    if (m_ids) {
        m_items[res.m_luid] = data;
        m_ids->setProperty(res.m_luid, "1");
    }

    // update key
    m_currentSyncKey = buffer;

    return res;
}

void ActiveSyncSource::readItem(const std::string &luid, std::string &item)
{
    // return straight from cache?
    std::map<std::string, std::string>::iterator it = m_items.find(luid);
    if (it == m_items.end()) {
        // no, must fetch
        EASItemPtr tmp(eas_item_info_new(), "EasItem");
        GErrorCXX gerror;
        if (!eas_sync_handler_fetch_item(m_handler,
                                         m_folder.c_str(),
                                         luid.c_str(),
                                         tmp,
                                         getEasType(),
                                         gerror)) {
            if (gerror.m_gerror->message &&
                strstr(gerror.m_gerror->message, "ObjectNotFound")
                /* gerror.matches(EAS_CONNECTION_ERROR, EAS_CONNECTION_ITEMOPERATIONS_ERROR_OBJECTNOTFOUND)
                   (gdb) p *m_gerror
                   $7 = {domain = 156, code = 36, 
                   message = 0xda2940 "GDBus.Error:org.meego.activesyncd.ItemOperationsError.ObjectNotFound: Document library - The object was not found or access denied."}

                */) {
                throwError(STATUS_NOT_FOUND, "item not found: " + luid);
            } else {
                gerror.throwError(StringPrintf("reading eas item %s", luid.c_str()));
            }
        }
        if (!tmp->data) {
            throwError(StringPrintf("no body returned for eas item %s", luid.c_str()));
        }
        item = tmp->data;
    } else {
        item = it->second;
    }
}

SE_END_CXX

#endif /* ENABLE_ACTIVESYNC */

#ifdef ENABLE_MODULES
# include "ActiveSyncSourceRegister.cpp"
#endif
