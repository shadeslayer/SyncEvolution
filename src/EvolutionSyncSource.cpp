/*
 * Copyright (C) 2005 Patrick Ohly
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

#include "EvolutionSyncSource.h"
#include "EvolutionContactSource.h"
#include "EvolutionCalendarSource.h"

#include <common/base/Log.h>

ESource *EvolutionSyncSource::findSource( ESourceList *list, const string &id )
{
    for (GSList *g = e_source_list_peek_groups (list); g; g = g->next) {
        ESourceGroup *group = E_SOURCE_GROUP (g->data);
        GSList *s;
        for (s = e_source_group_peek_sources (group); s; s = s->next) {
            ESource *source = E_SOURCE (s->data);
            char *uri = e_source_get_uri(source);
            bool found = !id.compare(e_source_peek_name(source)) ||
                (uri && !id.compare(uri));
            g_free(uri);
            if (found) {
                return source;
            }
        }
    }
    return NULL;
}

void EvolutionSyncSource::throwError( const string &action, GError *gerror )
{
    string gerrorstr;
    if (gerror) {
        gerrorstr += ": ";
        gerrorstr += gerror->message;
        g_clear_error(&gerror);
    } else {
        gerrorstr = ": failed";
    }

    throw runtime_error(string(getName()) + ": " + action + gerrorstr);
}

void EvolutionSyncSource::resetItems()
{
    m_allItems.clear();
    m_newItems.clear();
    m_updatedItems.clear();
    m_deletedItems.clear();
}

string EvolutionSyncSource::getData(SyncItem& item)
{
    char *mem = (char *)malloc(item.getDataSize() + 1);
    memcpy(mem, item.getData(), item.getDataSize());
    mem[item.getDataSize()] = 0;

    string res(mem);
    free(mem);
    return res;
}

string EvolutionSyncSource::getPropertyValue(ManagementNode &node, const string &property)
{
    char *value = node.getPropertyValue(property.c_str());
    string res;

    if (value) {
        res = value;
        delete [] value;
    }

    return res;
}

void EvolutionSyncSource::handleException()
{
    try {
        throw;
    } catch (std::exception &ex) {
        if (lastErrorCode == ERR_NONE) {
            lastErrorCode = ERR_UNSPECIFIED;
            strcpy(lastErrorMsg, ex.what());
        }
        LOG.error("%s", ex.what());
    }
}

EvolutionSyncSource *EvolutionSyncSource::createSource(
    const string &name,
    const string &changeId,
    const string &id,
    const string &mimeType
    )
{
    // remove special characters from change ID
    string strippedChangeId = changeId;
    size_t offset = 0;
    while (offset < strippedChangeId.size()) {
        switch (strippedChangeId[offset]) {
         case ':':
         case '/':
         case '\\':
            strippedChangeId.erase(offset, 1);
            break;
         default:
            offset++;
        }
    }

    if (mimeType == "text/x-vcard") {
#ifdef ENABLE_EBOOK
        return new EvolutionContactSource(name, strippedChangeId, id, EVC_FORMAT_VCARD_21);
#else
        LOG.error("access to addressbooks not compiled into this binary, text/x-vcard not supported");
#endif
    } else if (mimeType == "text/vcard") {
#ifdef ENABLE_EBOOK
        return new EvolutionContactSource(name, strippedChangeId, id, EVC_FORMAT_VCARD_30);
#else
        LOG.error("access to addressbooks not compiled into this binary, text/vcard not supported");
#endif
    } else if (mimeType == "text/x-todo") {
#ifdef ENABLE_ECAL
        return new EvolutionCalendarSource(E_CAL_SOURCE_TYPE_TODO, name, strippedChangeId, id);
#else
        LOG.error("access to calendars not compiled into this binary, text/x-todo not supported");
#endif
    } else if (mimeType == "text/calendar" ||
               mimeType == "text/x-vcalendar") {
#ifdef ENABLE_ECAL
        return new EvolutionCalendarSource(E_CAL_SOURCE_TYPE_EVENT, name, strippedChangeId, id);
#else
        LOG.error("access to calendars not compiled into this binary, %s not supported", mimeType.c_str());
#endif
    }

    return NULL;
}

int EvolutionSyncSource::beginSync()
{
    string buffer;
    buffer += getName();
    buffer += ": sync mode is ";
    SyncMode mode = getSyncMode();
    buffer += mode == SYNC_SLOW ? "'slow'" :
        mode == SYNC_TWO_WAY ? "'two-way'" :
        mode == SYNC_ONE_WAY_FROM_SERVER ? "'one-way'" :
        mode == SYNC_REFRESH_FROM_SERVER ? "'refresh from server'" :
        mode == SYNC_REFRESH_FROM_CLIENT ? "'refresh from client'" :
        mode == SYNC_NONE ? "'none' (for debugging)" :
        "???";
    LOG.info( buffer.c_str() );

    try {
        const char *error = getenv("SYNCEVOLUTION_BEGIN_SYNC_ERROR");
        if (error && strstr(error, getName())) {
            throw runtime_error("artificial error in beginSync()");
        }

        // reset state
        m_isModified = false;
        m_allItems.clear();
        m_newItems.clear();
        m_updatedItems.clear();
        m_deletedItems.clear();

        // determine what to do
        bool needAll = false;
        bool needPartial = false;
        bool deleteLocal = false;
        switch (mode) {
         case SYNC_SLOW:
            needAll = true;
            m_isModified = true;
            break;
         case SYNC_TWO_WAY:
            needPartial = true;
            break;
         case SYNC_REFRESH_FROM_SERVER:
            deleteLocal = true;
            m_isModified = true;
            break;
         case SYNC_REFRESH_FROM_CLIENT:
            needAll = true;
            break;
         case SYNC_NONE:
            // special mode for testing: prepare both all and partial lists
            needAll = needPartial = true;
            break;
         default:
            throw runtime_error("unsupported sync mode, valid are only: slow, two-way, refresh");
            break;
        }

        beginSyncThrow(needAll, needPartial, deleteLocal);
    } catch( ... ) {
        handleException();
        m_hasFailed = true;
        return 1;
    }
    return 0;
}

int EvolutionSyncSource::endSync()
{
    try {
        endSyncThrow();
    } catch ( ... ) {
        handleException();
        m_hasFailed = true;
    }

    // Do _not_ tell the caller (SyncManager) if an error occurred
    // because that causes Sync4jClient to abort processing for all
    // sync sources. Instead deal with failed sync sources in
    // EvolutionSyncClient::sync().
    return 0;
}

void EvolutionSyncSource::setItemStatus(const char *key, int status)
{
    try {
        // TODO: logging
        setItemStatusThrow(key, status);
    } catch (...) {
        handleException();
        m_hasFailed = true;
    }
}

int EvolutionSyncSource::addItem(SyncItem& item)
{
    return processItem("add", &EvolutionSyncSource::addItemThrow, item, true);
}

int EvolutionSyncSource::updateItem(SyncItem& item)
{
    return processItem("update", &EvolutionSyncSource::updateItemThrow, item, true);
}

int EvolutionSyncSource::deleteItem(SyncItem& item)
{
    return processItem("delete", &EvolutionSyncSource::deleteItemThrow, item, false);
}

int EvolutionSyncSource::processItem(const char *action,
                                     int (EvolutionSyncSource::*func)(SyncItem& item),
                                     SyncItem& item,
                                     bool needData)
{
    int status = STC_COMMAND_FAILED;
    
    try {
        logItem(item, action);
        if (needData && (item.getDataSize() < 0 || !item.getData())) {
            // Something went wrong in the server: update or add without data.
            // Shouldn't happen, but it did with one server and thus this
            // security check was added to prevent segfaults.
            logItem(item, "ignored due to missing data");
            status = STC_OK;
        } else {
            status = (this->*func)(item);
        }
        m_isModified = true;
    } catch (...) {
        handleException();
        m_hasFailed = true;
    }
    return status;
}

void EvolutionSyncSource::setItemStatusThrow(const char *key, int status)
{
    switch (status) {
     case STC_ALREADY_EXISTS:
        // found pair during slow sync, that's okay
        break;
     default:
        if (status < 200 || status > 300) {
            LOG.error("%s: unexpected SyncML status response %d for item %.80s\n",
                      getName(), status, key);
            m_hasFailed = true;
        }
    }
}
