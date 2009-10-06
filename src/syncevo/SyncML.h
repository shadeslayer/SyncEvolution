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

#include <syncevo/declarations.h>
SE_BEGIN_CXX

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
 * Parse user-visible mode names.
 */
SyncMode StringToSyncMode(const std::string &str);

/**
 * result of SyncML operations, same codes as in HTTP and the Synthesis engine
 */
enum SyncMLStatus {
    /** ok */
    STATUS_OK = 0,

    /** more explicit ok status in cases where 0 might mean "unknown" (SyncReport) */
    STATUS_HTTP_OK = 200,

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

/**
 * Information about a database dump.
 * Currently only records the number of items.
 * A negative number of items means no backup
 * available.
 */
class BackupReport {
 public:
    BackupReport() {
        clear();
    }

    bool isAvailable() const { return m_numItems >= 0; }
    long getNumItems() const { return m_numItems; }
    void setNumItems(long numItems) { m_numItems = numItems; }

    void clear() {
        m_numItems = -1;
    }

 private:
    long m_numItems;
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

    enum ItemLocation {
        ITEM_LOCAL,
        ITEM_REMOTE,
        ITEM_LOCATION_MAX
    };
    static std::string LocationToString(ItemLocation location);
    static ItemLocation StringToLocation(const std::string &location);
    enum ItemState {
        ITEM_ADDED,
        ITEM_UPDATED,
        ITEM_REMOVED,
        ITEM_ANY,
        ITEM_STATE_MAX
    };
    static std::string StateToString(ItemState state);
    static ItemState StringToState(const std::string &state);
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
    static std::string ResultToString(ItemResult result);
    static ItemResult StringToResult(const std::string &result);

    static std::string StatTupleToString(ItemLocation location, ItemState state, ItemResult result);
    static void StringToStatTuple(const std::string &str, ItemLocation &location, ItemState &state, ItemResult &result);

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
    void incrementItemStat(ItemLocation location,
                           ItemState state,
                           ItemResult success) {
        m_stat[location][state][success]++;
    }

    void recordFinalSyncMode(SyncMode mode) { m_mode = mode; }
    SyncMode getFinalSyncMode() const { return m_mode; }

    void recordFirstSync(bool isFirstSync) { m_first = isFirstSync; }
    bool isFirstSync() const { return m_first; }

    void recordResumeSync(bool isResumeSync) { m_resume = isResumeSync; }
    bool isResumeSync() const { return m_resume; }

    void recordStatus(SyncMLStatus status ) { m_status = status; }
    SyncMLStatus getStatus() const { return m_status; }

    /** information about database dump before and after session */
    BackupReport m_backupBefore, m_backupAfter;

 private:
    /** storage for getItemStat(): allow access with _MAX as index */
    int m_stat[ITEM_LOCATION_MAX + 1][ITEM_STATE_MAX + 1][ITEM_RESULT_MAX + 1];

    SyncMode m_mode;
    bool m_first;
    bool m_resume;
    SyncMLStatus m_status;
};

class SyncReport : public std::map<std::string, SyncSourceReport> {
    time_t m_start, m_end;
    SyncMLStatus m_status;

 public:
    SyncReport() :
        m_start(0),
        m_end(0),
        m_status(STATUS_OK)
        {}

    void addSyncSourceReport(const std::string &name,
                             const SyncSourceReport &report) {
        (*this)[name] = report;
    }
    SyncSourceReport &getSyncSourceReport(const std::string &name) {
        return (*this)[name];
    }

    /** start time of sync, 0 if unknown */
    time_t getStart() const { return m_start; }
    void setStart(time_t start) { m_start = start; }
    /** end time of sync, 0 if unknown (indicates a crash) */
    time_t getEnd() const { return m_end; }
    void setEnd(time_t end) { m_end = end; }

    /**
     * overall sync result
     *
     * STATUS_OK = 0 means unknown status (might have aborted prematurely),
     * STATUS_HTTP_OK = 200 means successful completion
     */
    SyncMLStatus getStatus() const { return m_status; }
    void setStatus(SyncMLStatus status) { m_status = status; }

    void clear() {
        std::map<std::string, SyncSourceReport>::clear();
        m_start = m_end = 0;
    }

    /** generate short string representing start and duration of sync */
    std::string formatSyncTimes() const;

    /** pretty-print with options */
    void prettyPrint(std::ostream &out, int flags) const;
    enum {
        WITHOUT_CLIENT = 1 << 1,
        WITHOUT_SERVER = 1 << 2,
        WITHOUT_CONFLICTS = 1 << 3,
        WITHOUT_REJECTS = 1 << 4,
        WITH_TOTAL = 1 << 5
    };
};

/** pretty-print the report as an ASCII table */
std::ostream &operator << (std::ostream &out, const SyncReport &report);

class ConfigNode;

/** write report into a ConfigNode */
ConfigNode &operator << (ConfigNode &node, const SyncReport &report);

/** read report from a ConfigNode */
ConfigNode &operator >> (ConfigNode &node, SyncReport &report);


SE_END_CXX
#endif // INCL_SYNCML
