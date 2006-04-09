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
            if ( !id.compare(e_source_peek_name(source)) ||
                 !id.compare(e_source_get_uri(source)) )
                return source;
        }
    }
    return NULL;
}

void EvolutionSyncSource::throwError( const string &action, GError *gerror )
{
    string gerrorstr;
    if (gerror) {
        gerrorstr += " ";
        gerrorstr += gerror->message;
        g_clear_error(&gerror);
    } else {
        gerrorstr = ": failed";
    }

    string error = string(getName()) + ": " + action + gerrorstr;
    LOG.error( error.c_str() );       
    throw error;
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

EvolutionSyncSource *EvolutionSyncSource::createSource(
    const string &name,
    const string &changeId,
    const string &id,
    const string &mimeType
    )
{
    // remove special characters from change ID
    string strippedChangeId = changeId;
    int offset = 0;
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
        return new EvolutionContactSource(name, strippedChangeId, id, EVC_FORMAT_VCARD_30);
    } else if (mimeType == "text/x-todo") {
        return new EvolutionCalendarSource(E_CAL_SOURCE_TYPE_TODO, name, strippedChangeId, id);
    } else if (mimeType == "text/calendar" ||
               mimeType == "text/x-vcalendar") {
        return new EvolutionCalendarSource(E_CAL_SOURCE_TYPE_EVENT, name, strippedChangeId, id);
    }

    // TODO: other mime types?
    return NULL;
}

int EvolutionSyncSource::beginSync()
{
    string buffer = "sync mode is: ";
    SyncMode mode = getSyncMode();
    buffer += mode == SYNC_SLOW ? "slow" :
        mode == SYNC_TWO_WAY ? "two-way" :
        mode == SYNC_ONE_WAY_FROM_SERVER ? "one-way" :
        mode == SYNC_REFRESH_FROM_SERVER ? "refresh from server" :
        mode == SYNC_REFRESH_FROM_CLIENT ? "refresh from client" :
        mode == SYNC_NONE ? "none" :
        "???";
    LOG.info( buffer.c_str() );

    try {
        const char *error = getenv("SYNCEVOLUTION_BEGIN_SYNC_ERROR");
        if (error && strstr(error, getName())) {
            LOG.error("simulate error");
            throw "artificial error in beginSync()";
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
            throw "unsupported sync mode, valid are only: slow, two-way, refresh";
            break;
        }

        beginSyncThrow(needAll, needPartial, deleteLocal);
    } catch( ... ) {
        m_hasFailed = true;
        // TODO: properly set error
        return 1;
    }
    return 0;
}

int EvolutionSyncSource::endSync()
{
    try {
        endSyncThrow();
    } catch ( ... ) {
        m_hasFailed = true;
        return 1;
    }
    // TODO: sync engine ignores return code, work around this?
    return m_hasFailed ? 1 : 0;
}

void EvolutionSyncSource::setItemStatus(const char *key, int status)
{
    try {
        // TODO: logging
        setItemStatusThrow(key, status);
    } catch (...) {
        // TODO: error handling
    }
}

int EvolutionSyncSource::addItem(SyncItem& item)
{
    return processItem("add", &EvolutionSyncSource::addItemThrow, item);
}

int EvolutionSyncSource::updateItem(SyncItem& item)
{
    return processItem("update", &EvolutionSyncSource::updateItemThrow, item);
}

int EvolutionSyncSource::deleteItem(SyncItem& item)
{
    return processItem("delete", &EvolutionSyncSource::deleteItemThrow, item);
}

int EvolutionSyncSource::processItem(const char *action,
                                     void (EvolutionSyncSource::*func)(SyncItem& item),
                                     SyncItem& item)
{
    try {
        logItem(item, action);
        (this->*func)(item);
        m_isModified = true;
    } catch (...) {
        m_hasFailed = true;
        return STC_COMMAND_FAILED;
    }
    return STC_OK;
}

void EvolutionSyncSource::setItemStatusThrow(const char *key, int status)
{
    if (status < 200 || status > 300) {
        LOG.error("unexpected SyncML status response %d for item %.80s\n",
                  status, key);
        m_hasFailed = true;
    }
}
