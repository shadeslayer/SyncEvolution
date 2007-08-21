/*
 * Copyright (C) 2005-2006 Patrick Ohly
 * Copyright (C) 2007 Funambol
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

#ifndef INCL_EVOLUTIONSYNCSOURCE
#define INCL_EVOLUTIONSYNCSOURCE

#include <string>
#include <vector>
#include <set>
#include <ostream>
using namespace std;

#ifdef HAVE_EDS
#include <libedataserver/e-source.h>
#include <libedataserver/e-source-list.h>
#endif

#include <spds/SyncSource.h>
#include <spdm/ManagementNode.h>
#include <base/Log.h>

/**
 * SyncEvolution accesses all sources through this interface.  This
 * class also implements common functionality for all SyncSources:
 * - handling of change IDs and URI
 * - finding the calender/contact backend
 * - default implementation of SyncSource interface
 *
 * The default interface assumes that the backend's
 * open() already finds all items as well as new/modified/deleted
 * ones and stores their UIDs in the respective lists.
 * Then the SyncItem iterators just walk through these lists,
 * creating new items via createItem().
 *
 * Error reporting is done via the Log class and this instance
 * then just tracks whether any error has occurred. If that is
 * the case, then the caller has to assume that syncing somehow
 * failed and a full sync is needed the next time.
 *
 * It also adds an Evolution specific interface:
 * - listing backend storages: getSyncBackends()
 */
class EvolutionSyncSource : public SyncSource
{
  public:
    /**
     * Creates a new Evolution sync source.
     *
     * @param    name        the named needed by SyncSource
     * @param    sc          obligatory config for this source, must remain valid throughout the lifetime of the source
     * @param    changeId    is used to track changes in the Evolution backend
     * @param    id          identifies the backend; not specifying it makes this instance
     *                       unusable for anything but listing backend databases
     */
    EvolutionSyncSource( const string name, SyncSourceConfig *sc, const string &changeId, const string &id ) :
        SyncSource( name.c_str(), sc ),
        m_changeId( changeId ),
        m_id( id ),
        m_allItems( *this, "existing", SYNC_STATE_NONE ),
        m_newItems( *this, "new", SYNC_STATE_NEW ),
        m_updatedItems( *this, "updated", SYNC_STATE_UPDATED ),
        m_deletedItems( *this, "deleted", SYNC_STATE_DELETED ),
        m_hasFailed( false ),
        m_isModified( false )
        {}
    virtual ~EvolutionSyncSource() {}

    struct source {
        source( const string &name, const string &uri ) :
            m_name( name ), m_uri( uri ) {}
        string m_name;
        string m_uri;
    };
    typedef vector<source> sources;
    
    /**
     * returns a list of all know sources for the kind of items
     * supported by this sync source
     */
    virtual sources getSyncBackends() = 0;

    /**
     * Set credentials to be used during open.
     */
    virtual void setAuthentication(const string &user, const string &passwd) {
        m_user = user;
        m_passwd = passwd;
    }
          

    /**
     * Get credentials.
     */
    virtual void getAuthentication(string &user, string &passwd) {
        user = m_user;
        passwd = m_passwd;
    }
    
    /**
     * Actually opens the data source specified in the constructor,
     * will throw the normal exceptions if that fails. Should
     * not modify the state of the sync source: that can be deferred
     * until the server is also ready and beginSync() is called.
     */
    virtual void open() = 0;

    /**
     * Extract information for the item identified by UID
     * and store it in a new SyncItem. The caller must
     * free that item. May throw exceptions.
     *
     * @param uid      identifies the item
     * @param state    the state of the item
     */
    virtual SyncItem *createItem( const string &uid, SyncState state ) = 0;

    /**
     * closes the data source so that it can be reopened
     *
     * Just as open() it should not affect the state of
     * the database unless some previous action requires
     * it.
     */
    virtual void close() = 0;

    /**
     * Dump all data from source unmodified into the given stream.
     */
    virtual void exportData(ostream &out) = 0;

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
    
    /**
     * resets the lists of all/new/updated/deleted items
     */
    void resetItems();

    /**
     * returns true iff some failure occured
     */
    bool hasFailed() { return m_hasFailed; }
    void setFailed(bool failed) { m_hasFailed = failed; }

    /** convenience function: copies item's data into string */
    static string getData(SyncItem& item);

    /**
     * convenience function: gets property as string class
     *
     * @return empty string if property not found, otherwise its value
     */
    static string getPropertyValue(ManagementNode &node, const string &property);

    /**
     * convenience function, to be called inside a catch() block:
     * rethrows the exception to determine what it is, then logs it
     * as an error
     */
    static void handleException();

    /**
     * factory function for a EvolutionSyncSources that provides the
     * given mime type; for the other parameters see constructor
     *
     * @param error    throw a runtime error describing what the problem is if no matching source is found
     * @return NULL if no source can handle the given type
     */
    static EvolutionSyncSource *createSource(
        const string &name,
        ManagementNode *node,
        SyncSourceConfig *sc,
        const string &changeId,
        const string &id,
        const string &mimeType,
        bool error = true);

    /**
     * SyncSource backend modules have to provide this function under
     * the "SyncEvolutionCreateSource" name (plan C name); it will then
     * be called by the createSource() method.
     *
     * @return new sync source or NULL if the mime type is not supported
     */
    typedef EvolutionSyncSource *(*CreateSource_t)(
        const string &name,
        SyncSourceConfig *sc,
        const string &changeId,
        const string &id,
        const string &mimeType);

    //
    // default implementation of SyncSource iterators
    //
    // getFirst/NextItemKey() are only required to return an item
    // with its key set and nothing else, but this optimization
    // does not really matter for Evolution, so they just iterate
    // over all items normally. Strictly speaking they should use
    // their own position marker, but as they are never called in
    // parallel that's okay.
    //
    virtual SyncItem* getFirstItem() { return m_allItems.start(); }
    virtual SyncItem* getNextItem() { return m_allItems.iterate(); }
    virtual SyncItem* getFirstNewItem() { return m_newItems.start(); }
    virtual SyncItem* getNextNewItem() { return m_newItems.iterate(); }
    virtual SyncItem* getFirstUpdatedItem() { return m_updatedItems.start(); }
    virtual SyncItem* getNextUpdatedItem() { return m_updatedItems.iterate(); }
    virtual SyncItem* getFirstDeletedItem() { return m_deletedItems.start(); }
    virtual SyncItem* getNextDeletedItem() { return m_deletedItems.iterate(); }
    virtual SyncItem* getFirstItemKey() { return getFirstItem(); }
    virtual SyncItem* getNextItemKey() { return getNextItem(); }

    virtual int beginSync();
    virtual int endSync();
    virtual void setItemStatus(const char *key, int status);
    virtual int addItem(SyncItem& item);
    virtual int updateItem(SyncItem& item);
    virtual int deleteItem(SyncItem& item);


  protected:
#ifdef HAVE_EDS
    /**
     * searches the list for a source with the given uri or name
     *
     * @param list      a list previously obtained from Gnome
     * @param id        a string identifying the data source: either its name or uri
     * @return   pointer to source or NULL if not found
     */
    ESource *findSource( ESourceList *list, const string &id );
#endif

    /**
     * throw an exception after a Gnome action failed and
     * remember that this instance has failed
     *
     * @param action     a string describing what was attempted
     * @param gerror     if not NULL: a more detailed description of the failure,
     *                                will be freed
     */
    void throwError( const string &action
#ifdef HAVE_EDS
                     , GError *gerror = NULL
#endif
                     );

    /**
     * source specific part of beginSync() - throws exceptions in case of error
     *
     * @param needAll           fill m_allItems
     * @param needPartial       fill m_new/deleted/modifiedItems
     * @param deleteLocal       erase all items
     */
    virtual void beginSyncThrow(bool needAll,
                                bool needPartial,
                                bool deleteLocal) = 0;

    /**
     * source specific part of endSync/setItemStatus/addItem/updateItem/deleteItem:
     * throw exception in case of error
     */
    virtual void endSyncThrow() = 0;
    virtual void setItemStatusThrow(const char *key, int status);
    virtual int addItemThrow(SyncItem& item) = 0;
    virtual int updateItemThrow(SyncItem& item) = 0;
    virtual int deleteItemThrow(SyncItem& item) = 0;


    /** log a one-line info about an item */
    virtual void logItem(const string &uid, const string &info, bool debug = false) = 0;
    virtual void logItem(SyncItem &item, const string &info, bool debug = false) = 0;

    const string m_changeId;
    const string m_id;

    class itemList : public set<string> {
        const_iterator m_it;
        EvolutionSyncSource &m_source;
        const string m_type;
        const SyncState m_state;

      public:
        itemList( EvolutionSyncSource &source, const string &type, SyncState state ) :
            m_source( source ),
            m_type( type ),
            m_state( state )
        {}
        /** start iterating, return first item if available */
        SyncItem *start() {
            m_it = begin();
            string buffer = string( "start scanning " ) + m_type + " items";
            LOG.debug( buffer.c_str() );
            return iterate();
        }
        /** return current item if available, step to next one */
        SyncItem *iterate() {
            if (m_it != end()) {
                const string &uid( *m_it );
                string buffer = string( "next " ) + m_type + " item: " + uid;
                LOG.debug( buffer.c_str() );
                ++m_it;
                if (&m_source.m_deletedItems == this) {
                    // just tell caller the uid of the deleted item
                    // and the type that it probably had
                    SyncItem *item = new SyncItem( uid.c_str() );
                    item->setDataType(m_source.getMimeType());
                    return item;
                } else {
                    // retrieve item with all its data
                    try {
                        return m_source.createItem( uid, m_state );
                    } catch(...) {
                        EvolutionSyncSource::handleException();
                        return NULL;
                    }
                }
            } else {
                return NULL;
            }
        }

        /**
         * add to list, with logging
         *
         * @return true if the item had not been added before
         */
        bool addItem(const string &uid) {
            pair<iterator, bool> res = insert(uid);
            if (res.second) {
                m_source.logItem(uid, m_type, true);
            }
            return res.second;
        }
    };
    
    /** UIDs of all/all new/all updated/all deleted items */
    itemList m_allItems,
        m_newItems,
        m_updatedItems,
        m_deletedItems;

    /**
     * remembers whether items have been modified during the sync:
     * if it is, then the destructor has to advance the change marker
     * or these modifications will be picked up during the next
     * two-way sync
     */
    bool m_isModified;

  private:
    /**
     * private wrapper function for add/delete/updateItemThrow()
     */
    int processItem(const char *action,
                    int (EvolutionSyncSource::*func)(SyncItem& item),
                    SyncItem& item,
                    bool needData);

    /** keeps track of failure state */
    bool m_hasFailed;

    /** user name/password set for this source, empty if none */
    string m_user, m_passwd;
};

#endif // INCL_EVOLUTIONSYNCSOURCE
