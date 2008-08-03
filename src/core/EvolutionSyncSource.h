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

#include "config.h"
#include "SyncEvolutionConfig.h"
#include "EvolutionSmartPtr.h"

#include <boost/shared_ptr.hpp>
#include <string>
#include <vector>
#include <set>
#include <ostream>
#include <stdexcept>
using namespace std;

#ifdef HAVE_EDS
#include <libedataserver/e-source.h>
#include <libedataserver/e-source-list.h>
#endif

#include <spds/SyncSource.h>
#include <spdm/ManagementNode.h>
#include <base/Log.h>

class EvolutionSyncSource;

/**
 * This set of parameters always has to be passed when constructing
 * EvolutionSyncSource instances.
 */
struct EvolutionSyncSourceParams {
    /**
     * @param    name        the name needed by SyncSource
     * @param    nodes       a set of config nodes to be used by this source
     * @param    changeId    is used to track changes in the Evolution backend:
     *                       a unique string constructed from an ID for SyncEvolution
     *                       and the URL/database we synchronize against
     */
    EvolutionSyncSourceParams(const string &name,
                              const SyncSourceNodes &nodes,
                              const string &changeId) :
    m_name(name),
        m_nodes(nodes),
        m_changeId(stripChangeId(changeId))
    {}

    const string m_name;
    const SyncSourceNodes m_nodes;
    const string m_changeId;

    /** remove special characters from change ID */
    static string stripChangeId(const string changeId) {
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
        return strippedChangeId;
    }
};

/**
 * The SyncEvolution core has no knowledge of existing SyncSource
 * implementations. Implementations have to register themselves
 * by instantiating this class exactly once with information
 * about themselves.
 *
 * It is also possible to add configuration options. For that define a
 * derived class. In its constructor use
 * EvolutionSyncSourceConfig::getRegistry()
 * resp. EvolutionSyncConfig::getRegistry() to define new
 * configuration properties. The advantage of registering them is that
 * the user interface will automatically handle them like the
 * predefined ones. The namespace of these configuration options
 * is shared by all sources and the core.
 *
 * For properties with arbitrary names use the
 * SyncSourceNodes::m_trackingNode.
 */
class RegisterSyncSource
{
 public:
    /**
     * Users select a SyncSource and its data format via the "type"
     * config property. Backends have to add this kind of function to
     * the SourceRegistry_t in order to be considered by the
     * SyncSource creation mechanism.
     *
     * The function will be called to check whether the backend was
     * meant by the user. It should return a new instance which will
     * be freed by the caller or NULL if it does not support the
     * selected type.
     * 
     * Inactive sources should return the special InactiveSource
     * pointer value if they recognize without a doubt that the user
     * wanted to instantiate them: for example, an inactive
     * EvolutionContactSource will return NULL for "addressbook" but
     * InactiveSource for "evolution-contacts".
     */
    typedef EvolutionSyncSource *(*Create_t)(const EvolutionSyncSourceParams &params);

    /** special return value of Create_t, not a real sync source! */
    static EvolutionSyncSource *const InactiveSource;

    /**
     * @param shortDescr     a few words identifying the data to be synchronized,
     *                       e.g. "Evolution Calendar"
     * @param enabled        true if the sync source can be instantiated,
     *                       false if it was not enabled during compilation or is
     *                       otherwise not functional
     * @param create         factory function for sync sources of this type
     * @param typeDescr      multiple lines separated by \n which get appended to
     *                       the the description of the type property, e.g.
     *                       "Evolution Memos = memo = evolution-memo\n"
     *                       "   plain text in UTF-8 (default) = text/plain\n"
     *                       "   iCalendar 2.0 = text/calendar\n"
     *                       "   The later format is not tested because none of the\n"
     *                       "   supported SyncML servers accepts it.\n"
     * @param typeValues     the config accepts multiple names for the same internal
     *                       type string; this list here is added to that list of
     *                       aliases. It should contain at least one unique string
     *                       the can be used to pick  this sync source among all
     *                       SyncEvolution sync sources (testing, listing backends, ...).
     *                       Example: Values() + (Aliases("Evolution Memos") + "evolution-memo")
     */
    RegisterSyncSource(const string &shortDescr,
                       bool enabled,
                       Create_t create,
                       const string &typeDescr,
                       const Values &typeValues);
 public:
    const string m_shortDescr;
    const bool m_enabled;
    const Create_t m_create;
    const string m_typeDescr;
    const Values m_typeValues;
};
    
typedef list<const RegisterSyncSource *> SourceRegistry;


#ifdef ENABLE_INTEGRATION_TESTS
#include <test/ClientTest.h>
typedef ClientTest::Config ClientTestConfig;
#else
/**
 * this class doesn't exist and cannot be referenced in code which is 
 * compiled without ENABLE_INTEGRATION_TEST, but we only need to
 * declare a reference to it, so that's okay
 */
class ClientTestConfig;
class ClientTest;
#endif

/**
 * In addition to registering the sync source itself by creating an
 * instance of RegisterSyncSource, configurations for testing it can
 * also be registered. A sync source which supports more than one data
 * exchange format can register one configuration for each format, but
 * not registering any configuration is also okay.
 *
 * This code depends on the C++ client library test framework and
 * therefore CPPUnit. To avoid a hard dependency on that in the normal
 * "syncevolution" binary, the actual usage of the test Config class
 * is limited to the *Register.cpp files when compiling them for
 * inclusion in the "client-test" binary, i.e., they are protected by
 * #ifdef ENABLE_UNIT_TESTS.
 *
 * Sync sources have to work stand-alone without a full SyncClient
 * configuration for all local tests. The minimal configuration prepared
 * for the source includes:
 * - a tracking node (as used f.i. by TrackingSyncSource) which
 *   points towards "~/.config/syncevolution/client-test-changes"
 * - a unique change ID (as used f.i. by EvolutionContactSource)
 * - a valid "evolutionsource" property in the config node, starting
 *   with the CLIENT_TEST_EVOLUTION_PREFIX env variable or (if that
 *   wasn't set) the "SyncEvolution_Test_" prefix
 *
 * No other properties are set, which implies that currently sync sources
 * which require further parameters cannot be tested.
 */
class RegisterSyncSourceTest
{
 public:
    /**
     * This call is invoked after setting up the config with default
     * values for the test cases selected via the constructor's
     * testCaseName parameter (one of vcard21, vcard30, ical20, itodo20;
     * see ClientTest in the Funambol client library for the current
     * list).
     *
     * This call can then override any of the values or (if there
     * are no predefined test cases) add them.
     *
     * The "type" property must select your sync source and the
     * data format for the test.
     *
     * @retval config        change any field whose default is not suitable
     */
    virtual void updateConfig(ClientTestConfig &config) const = 0;

    /**
     * @param configName     a unique string: the predefined names known by
     *                       ClientTest::getTestData() are already used for the initial
     *                       set of Evolution sync sources, for new sync sources
     *                       build a string by combining them with the sync source name
     *                       (e.g., "sqlite_vcard30")
     * @param testCaseName   a string recognized by ClientTest::getTestData() or an
     *                       empty string if there are no predefined test cases
     */
    RegisterSyncSourceTest(const string &configName,
                           const string &testCaseName);
    virtual ~RegisterSyncSourceTest() {}

    /**
     * Dump items in the native format, not the one currently selected
     * for exchange with the SyncML server. Useful for testing sync
     * sources which normally use one format internally, but also
     * support another one (EvolutionContactSource).
     */
    static int dump(ClientTest &client, SyncSource &source, const char *file);

    const string m_configName;
    const string m_testCaseName;
};

class TestRegistry : public vector<const RegisterSyncSourceTest *>
{
 public:
    // TODO: using const RegisterSyncSourceTest * operator [] (int);
    const RegisterSyncSourceTest * operator [] (const string &configName) const {
        BOOST_FOREACH(const RegisterSyncSourceTest *test, *this) {
            if (test->m_configName == configName) {
                return test;
            }
        }
        throw out_of_range(string("test config registry: ") + configName);
        return NULL;
    }
};


/**
 * SyncEvolution accesses all sources through this interface.  This
 * class also implements common functionality for all SyncSources:
 * - handling of change IDs and URI
 * - finding the calender/contact backend (only for Evolution)
 * - default implementation of SyncSource interface
 *
 * The default interface assumes that the backend's
 * beginSyncThrow() finds all items as well as new/modified/deleted
 * ones and stores their UIDs in the respective lists.
 * Then the Items iterators just walk through these lists,
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
class EvolutionSyncSource : public SyncSource, public EvolutionSyncSourceConfig
{
 public:
    /**
     * Creates a new Evolution sync source.
     */
    EvolutionSyncSource(const EvolutionSyncSourceParams &params) :
        SyncSource(params.m_name.c_str(), NULL),
        EvolutionSyncSourceConfig(params.m_name, params.m_nodes),
        m_changeId( params.m_changeId ),
        m_allItems( *this, "existing", SYNC_STATE_NONE ),
        m_newItems( *this, "new", SYNC_STATE_NEW ),
        m_updatedItems( *this, "updated", SYNC_STATE_UPDATED ),
        m_deletedItems( *this, "deleted", SYNC_STATE_DELETED ),
        m_isModified( false ),
        m_hasFailed( false )
        {
            setConfig(this);
        }
    virtual ~EvolutionSyncSource() {}

    /**
     * SyncSource implementations must register themselves here via
     * RegisterSyncSource
     */
    static SourceRegistry &getSourceRegistry();

    /**
     * SyncSource tests are registered here by the constructor of
     * RegisterSyncSourceTest
     */
    static TestRegistry &getTestRegistry();

    struct Database {
    Database(const string &name, const string &uri, bool isDefault = false) :
        m_name( name ), m_uri( uri ), m_isDefault(isDefault) {}
        string m_name;
        string m_uri;
        bool m_isDefault;
    };
    typedef vector<Database> Databases;
    
    /**
     * returns a list of all know data sources for the kind of items
     * supported by this sync source
     */
    virtual Databases getDatabases() = 0;

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
     * The information that has to be set in the new item is:
     * - content
     * - UID
     * - mime type
     *
     * @param uid      identifies the item
     */
    virtual SyncItem *createItem(const string &uid) = 0;

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
    virtual string fileSuffix() const = 0;

    /**
     * Returns the preferred mime type of the items handled by the sync source.
     * Example: "text/x-vcard"
     */
    virtual const char *getMimeType() const = 0;

    /**
     * Returns the version of the mime type used by client.
     * Example: "2.1"
     */
    virtual const char *getMimeVersion() const = 0;

    /**
     * A string representing the source types (with versions) supported by the SyncSource.
     * The string must be formatted as a sequence of "type:version" separated by commas ','.
     * For example: "text/x-vcard:2.1,text/vcard:3.0".
     * The version can be left empty, for example: "text/x-s4j-sifc:".
     * Supported types will be sent as part of the DevInf.
     */
    virtual const char* getSupportedTypes() const = 0;
    
    /**
     * resets the lists of all/new/updated/deleted items
     */
    void resetItems();

    /**
     * returns true iff some failure occured
     */
    bool hasFailed() { return m_hasFailed; }
    void setFailed(bool failed) { m_hasFailed = failed; }

    /**
     * Convenience function, to be called inside a catch() block of
     * the sync source.
     * Rethrows the exception to determine what it is, then logs it
     * as an error and sets the state of the sync source to failed.
     */
    void handleException();

    /**
     * factory function for a EvolutionSyncSource that provides the
     * source type specified in the params.m_nodes.m_configNode
     *
     * @param error    throw a runtime error describing what the problem is if no matching source is found
     * @return NULL if no source can handle the given type
     */
    static EvolutionSyncSource *createSource(const EvolutionSyncSourceParams &params,
                                             bool error = true);

    /**
     * Factory function for a EvolutionSyncSource with the given name
     * and handling the kind of data specified by "type" (e.g.
     * "Evolution Contacts:text/x-vcard").
     *
     * The source is instantiated with dummy configuration nodes under
     * the pseudo server name "testing". This function is used for
     * testing sync sources, not for real syncs. If the prefix is set,
     * then <prefix>_<name>_1 is used as database, just as in the
     * Client::Sync and Client::Source tests. Otherwise the default
     * database is used.
     *
     * @param error    throw a runtime error describing what the problem is if no matching source is found
     * @return NULL if no source can handle the given type
     */
    static EvolutionSyncSource *createTestingSource(const string &name, const string &type, bool error,
                                                    const char *prefix = getenv("CLIENT_TEST_EVOLUTION_PREFIX"));

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
    virtual SyncItem* getFirstItem() throw() { return m_allItems.start(); }
    virtual SyncItem* getNextItem() throw() { return m_allItems.iterate(); }
    virtual SyncItem* getFirstNewItem() throw() { return m_newItems.start(); }
    virtual SyncItem* getNextNewItem() throw() { return m_newItems.iterate(); }
    virtual SyncItem* getFirstUpdatedItem() throw() { return m_updatedItems.start(); }
    virtual SyncItem* getNextUpdatedItem() throw() { return m_updatedItems.iterate(); }
    virtual SyncItem* getFirstDeletedItem() throw() { return m_deletedItems.start(); }
    virtual SyncItem* getNextDeletedItem() throw() { return m_deletedItems.iterate(); }
    virtual SyncItem* getFirstItemKey() throw() { return getFirstItem(); }
    virtual SyncItem* getNextItemKey() throw() { return getNextItem(); }

    virtual int beginSync() throw();
    virtual int endSync() throw();
    virtual void setItemStatus(const char *key, int status) throw();
    virtual int addItem(SyncItem& item) throw();
    virtual int updateItem(SyncItem& item) throw();
    virtual int deleteItem(SyncItem& item) throw();

    /**
     * The client library invokes this call to delete all local
     * items. SyncSources derived from EvolutionSyncSource should
     * take care of that when beginSyncThrow() is called with
     * deleteLocal == true and thus do not need to implement
     * this method. If a derived source doesn't do that, then the
     * implementation of this call will simply iterate over all
     * stored LUIDs and remove them.
     *
     * @return 0 for success, non-zero for failure
     */
    virtual int removeAllItems() throw();

    /**
     * Disambiguate getName(): we have inherited it from both SyncSource and
     * AbstractSyncSourceConfig. Both must return the same string.
     */
    const char *getName()  throw() { return SyncSource::getName(); }

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

    /**
     * @name log a one-line info about an item
     *
     * The first two have to provided by derived classes because only
     * they know how to present the item to the user. When being passed
     * a SyncItem note that it may or may not contain data.
     *
     * The third version is an utility function which is provided
     * for derived classes. It does the right thing for vCard, vCalendar
     * and plain text (in a crude way, without really parsing them),
     * but needs access to the item data.
     */
    /**@{*/
    virtual void logItem(const string &uid, const string &info, bool debug = false) = 0;
    virtual void logItem(const SyncItem &item, const string &info, bool debug = false) = 0;
    virtual void logItemUtil(const string data, const string &mimeType, const string &mimeVersion,
                             const string &uid, const string &info, bool debug = false);
    /**@}*/

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

#ifdef HAVE_EDS
    /**
     * throw an exception after an operation failed and
     * remember that this instance has failed
     *
     * @param action     a string describing what was attempted
     * @param gerror     if not NULL: a more detailed description of the failure,
     *                                will be freed
     */
    void throwError(const string &action,
                    GError *gerror);
#endif

    /**
     * throw an exception after an operation failed and
     * remember that this instance has failed
     *
     * @param action     a string describing what was attempted *and* how it failed
     */
    void throwError(const string &failure);

    const string m_changeId;

    class Items : public set<string> {
        const_iterator m_it;
        EvolutionSyncSource &m_source;
        const string m_type;
        const SyncState m_state;

      public:
        Items( EvolutionSyncSource &source, const string &type, SyncState state ) :
            m_source( source ),
            m_type( type ),
            m_state( state )
        {}

        /**
         * start iterating, return first item if available
         *
         * Lists items in increasing lexical order. This is not required by
         * the SyncML standard, but it makes debugging easier. The
         * EvolutionCalendarSource relies on it: its uids are shorter
         * for parent items and thus they appear in the list before
         * their children.
         */
        SyncItem *start();

        /** return current item if available, step to next one */
        SyncItem *iterate();

        /**
         * add to list, with logging
         *
         * @return true if the item had not been added before
         */
        bool addItem(const string &uid);
    };
    
    /** UIDs of all/all new/all updated/all deleted items */
    Items m_allItems,
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
                    bool needData) throw();

    /** keeps track of failure state */
    bool m_hasFailed;
};

#endif // INCL_EVOLUTIONSYNCSOURCE
