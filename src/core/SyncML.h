/*
 * Copyright (C) 2009 Patrick Ohly <patrick.ohly@gmx.de>
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

#ifndef INCL_SYNCML
#define INCL_SYNCML

#include <string>
#include <map>
#include <ostream>
#include <string.h>

enum SyncMode {
    SYNC_NONE,
    SYNC_TWO_WAY,
    SYNC_SLOW,
    SYNC_ONE_WAY_FROM_CLIENT,
    SYNC_REFRESH_FROM_CLIENT,
    SYNC_ONE_WAY_FROM_SERVER,
    SYNC_REFRESH_FROM_SERVER,
    SYNC_MODE_MAX
};

/**
 * Return string for sync mode. User-visible strings are the ones used
 * in a sync source config ("two-way", "refresh-from-server", etc.).
 * Otherwise the constants above are returned ("SYNC_NONE").
 */
std::string PrettyPrintSyncMode(SyncMode mode, bool userVisible = true);

/**
 * simple container for SyncML items
 */
class SyncItem {
 private:
    /**
     * Data, might not be text. nul-byte not included in data size.
     */
    std::string m_data;
    /**
     * Local unique ID of the item.
     */
    std::string m_luid;
    /**
     * Empty string indicates the default format specified for a sync
     * source. Might be set to a mime type (e.g. "text/calendar") to
     * override the default format.
     */
    std::string m_datatype;


 public:
    std::string getKey() const { return m_luid; }
    void setKey(const std::string &key) { m_luid = key; }
    const char *getData() const { return m_data.c_str(); }
    size_t getDataSize() const { return m_data.size(); }
    void setData(const char *data, size_t size) { m_data.assign(data, size); }
    void setDataType(const std::string &datatype) { m_datatype = datatype; }
    std::string getDataType() const { return m_datatype; }

    /** result of change tracking and iteration over items */
    enum State {
        /** undefined state */
        NONE,
        /** not changed */
        UNCHANGED,
        /** item added */
        NEW,
        /** item updated */
        UPDATED,
        /** item deleted (only key, but no data available) */
        DELETED,
        /** end of iteration */
        NO_MORE_ITEMS,
        /** error reading item */
        ERROR,

        /** end of enumeration */
        STATE_MAX,
    };
};

/**
 * result of SyncML operations, same codes as in HTTP and the Synthesis engine
 */
enum SyncMLStatus {
    /** ok */
    STATUS_OK = 0,

    /** no content / end of file / end of iteration / empty/NULL value */
    STATUS_NO_CONTENT = 204,
    /** external data has been merged */
    STATUS_DATA_MERGED = 207,

    /** forbidden / access denied */
    STATUS_FORBIDDEN = 403,
    /** object not found / unassigned field */
    STATUS_NOT_FOUND = 404,
    /** command not allowed */
    STATUS_COMMAND_NOT_ALLOWED = 405,
    /** command failed / fatal DB error */
    STATUS_FATAL = 500,
    /** general DB error */
    STATUS_DATASTORE_FAILURE = 510,
    /** database / memory full error */
    STATUS_FULL = 420,

    STATUS_MAX = 0x7FFFFFF
};

class SyncSourceReport {
 public:
    SyncSourceReport() {
        memset(m_stat, 0, sizeof(m_stat));
        m_first =
            m_resume = false;
        m_mode = SYNC_NONE;
        m_status = STATUS_OK;
    }
    SyncSourceReport(const SyncSourceReport &other) {
        *this = other;
    }
    SyncSourceReport &operator = (const SyncSourceReport &other) {
        if (this != &other) {
            memcpy(m_stat, other.m_stat, sizeof(m_stat));
            m_first = other.m_first;
            m_resume = other.m_resume;
            m_mode = other.m_mode;
            m_status = other.m_status;
        }
        return *this;
    }

    enum ItemLocation {
        ITEM_LOCAL,
        ITEM_REMOTE,
        ITEM_LOCATION_MAX
    };
    enum ItemState {
        ITEM_ADDED,
        ITEM_UPDATED,
        ITEM_REMOVED,
        ITEM_ANY,
        ITEM_STATE_MAX
    };
    enum ItemResult {
        ITEM_TOTAL,               /**< total number ADDED/UPDATED/REMOVED */
        ITEM_REJECT,              /**< number of rejected items, ANY state */
        ITEM_MATCH,               /**< number of matched items, ANY state, REMOTE */
        ITEM_CONFLICT_SERVER_WON, /**< conflicts resolved by using server item, ANY state, REMOTE */
        ITEM_CONFLICT_CLIENT_WON, /**< conflicts resolved by using client item, ANY state, REMOTE */
        ITEM_CONFLICT_DUPLICATED, /**< conflicts resolved by duplicating item, ANY state, REMOTE */
        ITEM_SENT_BYTES,          /**< number of sent bytes, ANY, LOCAL */
        ITEM_RECEIVED_BYTES,      /**< number of received bytes, ANY, LOCAL */
        ITEM_RESULT_MAX
    };

    /**
     * get item statistics
     *
     * @param location   either local or remote
     * @param state      added, updated or removed
     * @param success    either okay or failed
     */
    int getItemStat(ItemLocation location,
                    ItemState state,
                    ItemResult success) const {
        return m_stat[location][state][success];
    }
    void setItemStat(ItemLocation location,
                     ItemState state,
                     ItemResult success,
                     int count) {
        m_stat[location][state][success] = count;
    }

    void recordFinalSyncMode(SyncMode mode) { m_mode = mode; }
    SyncMode getFinalSyncMode() const { return m_mode; }

    void recordFirstSync(bool isFirstSync) { m_first = isFirstSync; }
    bool isFirstSync() const { return m_first; }

    void recordResumeSync(bool isResumeSync) { m_resume = isResumeSync; }
    bool isResumeSync() const { return m_resume; }

    void recordStatus(SyncMLStatus status ) { m_status = status; }
    SyncMLStatus getStatus() const { return m_status; }

 private:
    /** storage for getItemStat() */
    int m_stat[ITEM_LOCATION_MAX][ITEM_STATE_MAX][ITEM_RESULT_MAX];

    SyncMode m_mode;
    bool m_first;
    bool m_resume;
    SyncMLStatus m_status;
};

class SyncReport : public std::map<std::string, SyncSourceReport> {
 public:
    void addSyncSourceReport(const std::string &name,
                             const SyncSourceReport &report) {
        (*this)[name] = report;
    }
    const SyncSourceReport &getSyncSourceReport(const std::string &name) {
        return (*this)[name];
    }
};

/** pretty-print the report as an ASCII table */
std::ostream &operator << (std::ostream &out, const SyncReport &report);


#endif // INCL_SYNCML
