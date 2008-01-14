/*
 * Copyright (C) 2008 Patrick Ohly
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

#ifndef INCL_TRACKINGSYNCSOURCE
#define INCL_TRACKINGSYNCSOURCE

#include "EvolutionSyncSource.h"
#include "DeviceManagementNode.h"

#include <string>
#include <map>
using namespace std;

/**
 * This class implements change tracking. Data sources which want to use
 * this functionality have to provide the following functionality
 * by implementing the pure virtual functions below:
 * - open() the data
 * - enumerate all existing items
 * - provide UID and "revision string"; both have to be simple
 *   strings (printable ASCII, no white spaces, no equal sign);
 *   the UID must remain *constant* when the user edits an item (it
 *   may change when SyncEvolution changes an item), whereas the
 *   revision string must *change* each time the item is changed
 *   by anyone
 * - import/export/update single items
 * - persistently store all changes in flush()
 * - clean up in close()
 *
 * A derived class may (but doesn't have to) override additional
 * functions to modify or replace the default implementations, e.g.:
 * - dumping the complete database (export())
 *
 * Potential implementations of the revision string are:
 * - a modification time stamp
 * - a hash value of a textual representation of the item
 *   (beware, such a hash might change as the textual representation
 *    changes even though the item is unchanged)
 */
class TrackingSyncSource : public EvolutionSyncSource
{
  public:
    /**
     * Creates a new tracking sync source.
     *
     * @param name           the named needed by SyncSource
     * @param sc             obligatory config for this source, must remain valid throughout the lifetime of the source;
     *                       may be NULL for unit testing
     * @param changeId       is used to track changes in the Evolution backend
     * @param id             identifies the backend; not specifying it makes this instance
     *                       unusable for anything but listing backend databases
     * @param trackingNode   the management node which this instance shall use to store its state:
     *                       ownership over the pointer has to be transferred to the source
     */
    TrackingSyncSource(const string &name, SyncSourceConfig *sc, const string &changeId, const string &id,
                       eptr<spdm::DeviceManagementNode> trackingNode);

    /**
     * returns a list of all know sources for the kind of items
     * supported by this sync source
     */
    virtual sources getSyncBackends() = 0;

    /**
     * Actually opens the data source specified in the constructor,
     * will throw the normal exceptions if that fails. Should
     * not modify the state of the sync source: that can be deferred
     * until the server is also ready and beginSync() is called.
     */
    virtual void open() = 0;

    /**
     * exports all items one after the other, separated by blank line;
     * if that format is not suitable, then the derived class must
     * override this call
     */
    virtual void exportData(ostream &out);

    typedef map<string, string> RevisionMap_t;

    /**
     * fills the complete mapping from UID to revision string of all
     * currently existing items
     */
    virtual void listAllItems(RevisionMap_t &revisions) = 0;

    /**
     * Create or modify an item.
     *
     * The sync source should be flexible: if the UID is non-empty,
     * it should try to modify the item. If the item is not found,
     * a new one should be created. The UID may be changed both when
     * creating as well as when modifying an item.
     *
     * Errors are signalled by throwing an exception.
     *
     * @param uid      in: identifies the item to be modified, empty for creating;
     *                 out: UID after the operation
     * @param item     contains the new content of the item and its MIME type
     * @return the new revision string
     */
    virtual string insertItem(string &uid, const SyncItem &item) = 0;

    /**
     * Extract information for the item identified by UID
     * and store it in a new SyncItem. The caller must
     * free that item. May throw exceptions.
     *
     * @param uid      identifies the item
     */
    virtual SyncItem *createItem(const string &uid) = 0;

    /**
     * removes and item
     */
    virtual void deleteItem(const string &uid) = 0;

    /**
     * write all changes, throw error if that fails
     */
    virtual void flush() = 0;
    
    /**
     * closes the data source so that it can be reopened
     *
     * Just as open() it should not affect the state of
     * the database unless some previous action requires
     * it.
     */
    virtual void close() = 0;

    /**
     * file suffix for database files
     */
    virtual string fileSuffix() = 0;

    /**
     * the actual type used by the source for items
     */
    virtual const char *getMimeType() = 0;

    /**
     * the actual version of the mime specification
     */
    virtual const char *getMimeVersion() = 0;

    /**
     * supported data types for send and receive,
     * in the format "type1:version1,type2:version2,..."
     */
    virtual const char *getSupportedTypes() = 0;

 protected:
    /** log a one-line info about an item */
    virtual void logItem(const string &uid, const string &info, bool debug = false) = 0;
    virtual void logItem(SyncItem &item, const string &info, bool debug = false) = 0;

  private:
    /* implementations of EvolutionSyncSource callbacks */
    virtual void beginSyncThrow(bool needAll,
                                bool needPartial,
                                bool deleteLocal);
    virtual void endSyncThrow();
    virtual void setItemStatusThrow(const char *key, int status);
    virtual int addItemThrow(SyncItem& item);
    virtual int updateItemThrow(SyncItem& item);
    virtual int deleteItemThrow(SyncItem& item);

    /** cannot be cloned because clones would have to coordinate access to change tracking */
    ArrayElement *clone() { return NULL; }

    eptr<spdm::DeviceManagementNode> m_trackingNode;
};

#endif // INCL_TRACKINGSYNCSOURCE
