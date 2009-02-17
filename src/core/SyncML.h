/*
 * Copyright (C) 2009 Patrick Ohly
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY, TITLE, NONINFRINGEMENT or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307  USA
 */

#ifndef INCL_SYNCML
#define INCL_SYNCML

#include <string>

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

#endif // INCL_SYNCML
