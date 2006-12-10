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

#include <config.h>

#include <cppunit/AdditionalMessage.h>
#include <cppunit/extensions/HelperMacros.h>
#define EVOLUTION_ASSERT_NO_THROW( _source, _x ) \
{ \
    CPPUNIT_ASSERT_NO_THROW( _x ); \
    CPPUNIT_ASSERT( !(_source).hasFailed() ); \
}

#define EVOLUTION_ASSERT( _source, _x ) \
{ \
    CPPUNIT_ASSERT( _x ); \
    CPPUNIT_ASSERT( !(_source).hasFailed() ); \
}

#define EVOLUTION_ASSERT_MESSAGE( _message, _source, _x ) \
{ \
    CPPUNIT_ASSERT_MESSAGE( (_message), (_x) ); \
    CPPUNIT_ASSERT( !(_source).hasFailed() );\
}

#ifdef ENABLE_EBOOK
#include <EvolutionContactSource.h>
#endif
#ifdef ENABLE_ECAL
#include <EvolutionCalendarSource.h>
#endif
#include <EvolutionSyncClient.h>
#include <common/spds/SyncStatus.h>
#include <posix/base/posixlog.h>

#include <string.h>
#include <fcntl.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <list>
using namespace std;

#include "Test.h"

/** utility function to iterate over different kinds of items in a sync source */
static int countAnyItems(
    EvolutionSyncSource &source,
    SyncItem * (EvolutionSyncSource::*first)(),
    SyncItem * (EvolutionSyncSource::*next)() )
{
    SyncItem *item;
    int count = 0;
    CPPUNIT_ASSERT( !source.hasFailed() );
    EVOLUTION_ASSERT_NO_THROW( source, item = (source.*first)() );
    while ( item ) {
        count++;
        delete item;
        EVOLUTION_ASSERT_NO_THROW( source, item = (source.*next)() );
    }

    return count;
}
    
static int countNewItems( EvolutionSyncSource &source )
{
    int res = countAnyItems(
        source,
        &EvolutionSyncSource::getFirstNewItem,
        &EvolutionSyncSource::getNextNewItem );
    return res;
}

static int countUpdatedItems( EvolutionSyncSource &source )
{
    int res = countAnyItems(
        source,
        &EvolutionSyncSource::getFirstUpdatedItem,
        &EvolutionSyncSource::getNextUpdatedItem );
    return res;
}

static int countDeletedItems( EvolutionSyncSource &source )
{
    int res = countAnyItems(
        source,
        &EvolutionSyncSource::getFirstDeletedItem,
        &EvolutionSyncSource::getNextDeletedItem );
    return res;
}

static int countItems( EvolutionSyncSource &source )
{
    int res = countAnyItems(
        source,
        &EvolutionSyncSource::getFirstItem,
        &EvolutionSyncSource::getNextItem );
    return res;
}

/**
 * the base class for all kind of tests, using
 * a class derived from EvolutionSyncSource to
 * to access the backend
 */
template<class T> class TestEvolution : public CppUnit::TestFixture {
    /**
     * base name of the sync source, e.g. "addressbook"
     */
    const string m_syncSourceName;

    /**
     * properties which need to be updated when creating
     * artificial items
     */
    list<string> m_uniqueProperties;

    /**
     * property which can be filled with spaces to artificially increase the
     * size of the m_insertItem; must already be contained in item
     */
    const string m_sizeProperty;
    
    /**
     * initial item which gets inserted by testSimpleInsert(),
     * default item to be used for updating it,
     * updates of it for triggering a merge conflict,
     * 
     */
    const string m_insertItem,
        m_updateItem, m_complexUpdateItem,
        m_mergeItem1, m_mergeItem2;

    /**
     * file containing items to be copied and compared after copying
     */
    const string m_testItems;
    
    /**
     * duration to sleep after a synchronization -
     * needed by Sync4j 2.3 to operate correctly
     */
    int m_syncDelay;
    
    /** the name of the Evolution databases */
    string m_databases[2];

    /** two different sync configurations, referencing the databases in m_databases */
    string m_syncConfigs[2];

    /** different change ids */
    string m_changeIds[2];

    /** the source names */
    string m_source[2];

    /** filename of server log */
    string m_serverLog;

    /**
     * inserts the given item or m_insertItem,
     * using a source with config and change ID as specified
     */
    void insert(const string *data = NULL,
                int changeid = 0,
                int config = 0);

    /**
     * assumes that one element is currently inserted and updates it
     * with the given item or m_updateItem
     */
    void update( int config = 0, const char *data = NULL );

    /**
     * imports m_testItems (must be file with blank-line separated items)
     */
    void import();

    /** performs one sync operation */
    string doSync(const string &logfile, int config, SyncMode syncMode,
                  long maxMsgSize = 0,
                  long maxObjSize = 0,
                  bool loSupport = false,
                  const char *encoding = NULL);

    /** deletes all items locally via T sync source */
    void deleteAll(int config, bool insertFirst = false);

    enum DeleteAllMode {
        DELETE_ALL_SYNC,   /**< make sure client and server are in sync,
                              delete locally,
                              sync again */
        DELETE_ALL_REFRESH /**< delete locally, refresh server */
    };

    /** deletes all items locally and on server, using different methods */
    void deleteAll( const string &prefix, int config, DeleteAllMode mode = DELETE_ALL_SYNC );

    /** reset databases, create item in one database, then copy to the other */
    void doCopy( const string &prefix );

    /**
     * takes two databases, exports them,
     * then compares them using synccompare
     *
     * @param refData      existing file with source reference items (defaults to first config)
     * @param copyDatabase config of database with the copied items (defaults to second config)
     * @param raiseAssertion raise assertion if comparison yields differences (defaults to true)
     */
    void compareDatabases(const string &prefix, const char *refData = NULL, int copyDatabase = 1, bool raiseAssert = true);

    /**
     * insert artificial items, number of them determined by TEST_EVOLUTION_NUM_ITEMS
     * unless passed explicitly
     *
     * @param config          determines which client is modified
     * @param startIndex      IDs are generated starting with this value
     * @param numItems        number of items to be inserted if non-null, otherwise TEST_EVOLUTION_NUM_ITEMS is used
     * @param size            minimum size for new items
     * @return number of items inserted
     */
    int insertManyItems(int config, int startIndex = 1, int numItems = 0, int size = -1);

    /**
     * replicate server database locally: same as SYNC_REFRESH_FROM_SERVER,
     * but done with explicit local delete and then a SYNC_SLOW because some
     * servers do no support SYNC_REFRESH_FROM_SERVER
     */
    void refreshClient(const string &prefix, int config) {
        deleteAll(config);
        doSync(prefix, config, SYNC_SLOW);
    }

    /**
     * implements testMaxMsg(), testLargeObject(), testLargeObjectEncoded()
     * using a sequence of items with varying sizes
     */
    void doVarSizes(bool withMaxMsgSize,
                    bool withLargeObject,
                    const char *encoding);
    
public:
    TestEvolution(
        const char *syncSourceName,
        const char *uniqueProperties,
        const char *sizeProperty,
        const char *insertItem,
        const char *updateItem,
        const char *complexUpdateItem,
        const char *mergeItem1,
        const char *mergeItem2
        ) :
        m_syncSourceName(syncSourceName),
        m_sizeProperty(sizeProperty),
        m_insertItem(insertItem),
        m_updateItem(updateItem),
        m_complexUpdateItem(complexUpdateItem),
        m_mergeItem1(mergeItem1),
        m_mergeItem2(mergeItem2),
        m_testItems(string(syncSourceName) + ".tests")
        {
            // split into individual components
            const char *prop = uniqueProperties;
            const char *nextProp;

            while (*prop) {
                nextProp = strchr(prop, ':');
                if (!nextProp) {
                    m_uniqueProperties.push_back(string(prop));
                    break;
                }
                m_uniqueProperties.push_back(string(prop, 0, nextProp - prop));
                prop = nextProp + 1;
            }
        }

    void setUp() {
        m_databases[0] = "SyncEvolution test #1";
        m_databases[1] = "SyncEvolution test #2";
        const char *server = getenv("TEST_EVOLUTION_SERVER");
        if (!server) {
            server = "localhost";
        }
        m_syncConfigs[0] = string(server) + "_1";
        m_syncConfigs[1] = string(server) + "_2";
        m_changeIds[0] = "SyncEvolution Change ID #0";
        m_changeIds[1] = "SyncEvolution Change ID #1";
        m_source[0] = m_syncSourceName + "_1";
        m_source[1] = m_syncSourceName + "_2";

        const char *log = getenv( "TEST_EVOLUTION_LOG" );
        if (log) {
            m_serverLog = log;
        }
        const char *delay = getenv("TEST_EVOLUTION_DELAY");
        m_syncDelay = delay ? atoi(delay) : 0;
    }
    void tearDown() {
    }

    //
    // tests involving only T sync source:
    // - done on m_databases[0]
    // - change tracking is tested with two different
    //   change markers in m_changeIds[0/1]
    //

    // opening address book
    void testOpen();
    // insert one contact
    void testSimpleInsert();
    // delete all items
    void testLocalDeleteAll();
    // restart scanning of items
    void testIterateTwice();
    // clean database, then insert
    void testComplexInsert();
    // clean database, insert item, update it
    void testLocalUpdate();
    // complex sequence of address book changes
    void testChanges();
    // clean database, import file, then export again and compare
    void testImport();
    // same as testImport() with immediate delete
    void testImportDelete();
    // test change tracking with large number of items
    void testManyChanges();

    //
    // tests involving real synchronization:
    // - expects existing configurations called as in m_syncConfigs
    // - changes due to syncing are monitored via direct access through T sync source
    //

    // do a refresh from server sync without additional checks
    void testRefreshFromServerSync();
    // do a refresh from client sync without additional checks
    void testRefreshFromClientSync();
    // do a two-way sync without additional checks
    void testTwoWaySync();
    // do a slow sync without additional checks
    void testSlowSync();
    // delete all items, locally and on server using two-way sync
    void testDeleteAllSync();
    // delete all items, locally and on server using refresh-from-client sync
    void testDeleteAllRefresh();
    // test that a refresh sync of an empty server leads to an empty datatbase
    void testRefreshSemantic();
    // tests the following sequence of events:
    // - insert item
    // - delete all items
    // - insert one other item
    // - refresh from client
    // => no items should now be listed as new, updated or deleted for this client
    void testRefreshStatus();
    // test that a two-way sync copies an item from one address book into the other
    void testCopy();
    // test that a two-way sync copies updates from database to the other client,
    // using simple data commonly supported by servers
    void testUpdate();
    // test that a two-way sync copies updates from database to the other client,
    // using data that some, but not all servers support, like adding a second
    // phone number to a contact
    void testComplexUpdate();
    // test that a two-way sync deletes the copy of an item in the other database
    void testDelete();
    // test what the server does when it finds that different
    // fields of the same item have been modified
    void testMerge();
    // test what the server does when it has to execute a slow sync
    // with identical data on client and server:
    // expected behaviour is that nothing changes
    void testTwinning();
    // tests one-way sync from server:
    // - get both clients and server in sync with no items anywhere
    // - add one item on first client, copy to server
    // - add a different item on second client, one-way-from-server
    // - two-way sync with first client
    // => one item on first client, two on second
    // - delete on first client, sync that to second client
    //   via two-way sync + one-way-from-server
    // => one item left on second client (the one inserted locally)
    void testOneWayFromServer();
    // tests one-way sync from client:
    // - get both clients and server in sync with no items anywhere
    // - add one item on first client, copy to server
    // - add a different item on second client, one-way-from-client
    // - two-way sync with first client
    // => two items on first client, one on second
    // - delete on second client, sync that to first client
    //   via one-way-from-client, two-way
    // => one item left on first client (the one inserted locally)
    void testOneWayFromClient();
    // creates several items, transmits them back and forth and
    // then compares which of them have been preserved
    void testItems();
    // tests the following sequence of events:
    // - both clients in sync with server
    // - client 1 adds item
    // - client 1 updates the same item
    // - client 2 gets item (depending on server, might be flagged as update)
    // See http://forge.objectweb.org/tracker/?func=detail&atid=100096&aid=305018&group_id=96
    void testAddUpdate();
    // test copying with maxMsg and no large object support
    void testMaxMsg() {
        doVarSizes(true, false, NULL);
    }
    // test copying with maxMsg and large object support
    void testLargeObject() {
        doVarSizes(true, true, NULL);
    }
    // test copying with maxMsg and large object support using explicit "bin" encoding
    void testLargeObjectBin() {
        doVarSizes(true, true, "bin");
    }
    // test copying with maxMsg and large object support using B64 encoding
    void testLargeObjectEncoded() {
        doVarSizes(true, true, "b64");
    }
    
    //
    // stress tests: execute some of the normal operations,
    // but with large number of artificially generated items
    //

    // two-way sync with clean client/server,
    // followed by slow sync and comparison
    // via second client
    void testManyItems();
};

#ifdef ENABLE_EBOOK
/**
 * TestEvolution configured for use with contacts
 */
class TestContact : public TestEvolution<EvolutionContactSource>
{
public:
    TestContact() :
        TestEvolution<EvolutionContactSource>(
            "addressbook",

            "FN:N:X-EVOLUTION-FILE-AS",
            "NOTE",
            
            /* initial item */
            "BEGIN:VCARD\n"
            "VERSION:3.0\n"
            "TITLE:tester\n"
            "FN:John Doe\n"
            "N:Doe;John;;;\n"
            "TEL;TYPE=WORK;TYPE=VOICE:business 1\n"
            "X-EVOLUTION-FILE-AS:Doe\\, John\n"
            "X-MOZILLA-HTML:FALSE\n"
            "NOTE:\n"
            "END:VCARD\n",

            /* default update item which replaces the initial item */
            "BEGIN:VCARD\n"
            "VERSION:3.0\n"
            "TITLE:tester\n"
            "FN:Joan Doe\n"
            "N:Doe;Joan;;;\n"
            "X-EVOLUTION-FILE-AS:Doe\\, Joan\n"
            "TEL;TYPE=WORK;TYPE=VOICE:business 2\n"
            "BDAY:2006-01-08\n"
            "X-MOZILLA-HTML:TRUE\n"
            "END:VCARD\n",

            /*
             * complex update item which replaces the initial item in testComplexUpdate:
             * adds a second phone number
             */
            "BEGIN:VCARD\n"
            "VERSION:3.0\n"
            "TITLE:tester\n"
            "FN:Joan Doe\n"
            "N:Doe;Joan;;;\n"
            "X-EVOLUTION-FILE-AS:Doe\\, Joan\n"
            "TEL;TYPE=WORK;TYPE=VOICE:business 1\n"
            "TEL;TYPE=HOME;TYPE=VOICE:home 2\n"
            "BDAY:2006-01-08\n"
            "X-MOZILLA-HTML:TRUE\n"
            "END:VCARD\n",

            /* add a telephone number, email and X-AIM to initial item in testMerge() */
            "BEGIN:VCARD\n"
            "VERSION:3.0\n"
            "TITLE:tester\n"
            "FN:John Doe\n"
            "N:Doe;John;;;\n"
            "X-EVOLUTION-FILE-AS:Doe\\, John\n"
            "X-MOZILLA-HTML:FALSE\n"
            "TEL;TYPE=WORK;TYPE=VOICE:business 1\n"
            "EMAIL:john.doe@work.com\n"
            "X-AIM:AIM JOHN\n"
            "END:VCARD\n",

            // add a birthday, modify the title and X-MOZILLA-HTML
            "BEGIN:VCARD\n"
            "VERSION:3.0\n"
            "TITLE:developer\n"
            "FN:John Doe\n"
            "N:Doe;John;;;\n"
            "X-EVOLUTION-FILE-AS:Doe\\, John\n"
            "X-MOZILLA-HTML:TRUE\n"
            "BDAY:2006-01-08\n"
            "END:VCARD\n" )
        {}
};
#endif /* ENABLE_EBOOK */

#ifdef ENABLE_ECAL
/**
 * EvolutionCalendarSource configured for access to calendars,
 * with a constructor as expected by TestEvolution
 */
class TestEvolutionCalendarSource : public EvolutionCalendarSource
{
public:
    TestEvolutionCalendarSource(
        const string &name,
        SyncSourceConfig *sc,
        const string &changeId = string(""),
        const string &id = string("") ) :
        EvolutionCalendarSource(
            E_CAL_SOURCE_TYPE_EVENT,
            name,
            sc,
            changeId,
            id)
        {}
};

/**
 * TestEvolution configured for use with calendars
 */
class TestCalendar : public TestEvolution<TestEvolutionCalendarSource>
{
public:
    TestCalendar() :
        TestEvolution<TestEvolutionCalendarSource>(
            "calendar",

            "SUMMARY:UID",
            "DESCRIPTION",
            
            /* initial item */
            "BEGIN:VCALENDAR\n"
            "PRODID:-//Ximian//NONSGML Evolution Calendar//EN\n"
            "VERSION:2.0\n"
            "METHOD:PUBLISH\n"
            "BEGIN:VEVENT\n"
            "SUMMARY:phone meeting\n"
            "DTEND;20060406T163000Z\n"
            "DTSTART;20060406T160000Z\n"
            "UID:1234567890!@#$%^&*()<>@dummy\n"
            "DTSTAMP:20060406T211449Z\n"
            "LAST-MODIFIED:20060409T213201\n"
            "CREATED:20060409T213201\n"
            "LOCATION:my office\n"
            "DESCRIPTION:let's talk\n"
            "CLASS:PUBLIC\n"
            "TRANSP:OPAQUE\n"
            "SEQUENCE:1\n"
            "END:VEVENT\n"
            "END:VCALENDAR\n",

            /* default update item which replaces the initial item */
            "BEGIN:VCALENDAR\n"
            "PRODID:-//Ximian//NONSGML Evolution Calendar//EN\n"
            "VERSION:2.0\n"
            "METHOD:PUBLISH\n"
            "BEGIN:VEVENT\n"
            "SUMMARY:meeting on site\n"
            "DTEND;20060406T163000Z\n"
            "DTSTART;20060406T160000Z\n"
            "UID:1234567890!@#$%^&*()<>@dummy\n"
            "DTSTAMP:20060406T211449Z\n"
            "LAST-MODIFIED:20060409T213201\n"
            "CREATED:20060409T213201\n"
            "LOCATION:big meeting room\n"
            "DESCRIPTION:nice to see you\n"
            "CLASS:PUBLIC\n"
            "TRANSP:OPAQUE\n"
            "SEQUENCE:1\n"
            "END:VEVENT\n"
            "END:VCALENDAR\n",

            /* complex update item which replaces the initial item - empty because not needed */
            "",

            /* change location in initial item in testMerge() */
            "BEGIN:VCALENDAR\n"
            "PRODID:-//Ximian//NONSGML Evolution Calendar//EN\n"
            "VERSION:2.0\n"
            "METHOD:PUBLISH\n"
            "BEGIN:VEVENT\n"
            "SUMMARY:phone meeting\n"
            "DTEND;20060406T163000Z\n"
            "DTSTART;20060406T160000Z\n"
            "UID:1234567890!@#$%^&*()<>@dummy\n"
            "DTSTAMP:20060406T211449Z\n"
            "LAST-MODIFIED:20060409T213201\n"
            "CREATED:20060409T213201\n"
            "LOCATION:calling from home\n"
            "DESCRIPTION:let's talk\n"
            "CLASS:PUBLIC\n"
            "TRANSP:OPAQUE\n"
            "SEQUENCE:1\n"
            "END:VEVENT\n"
            "END:VCALENDAR\n",

            /* change time zone, description and X-LIC-LOCATION */
            "BEGIN:VCALENDAR\n"
            "PRODID:-//Ximian//NONSGML Evolution Calendar//EN\n"
            "VERSION:2.0\n"
            "METHOD:PUBLISH\n"
            "BEGIN:VEVENT\n"
            "SUMMARY:phone meeting\n"
            "DTEND;20060406T163000Z\n"
            "DTSTART;20060406T160000Z\n"
            "UID:1234567890!@#$%^&*()<>@dummy\n"
            "DTSTAMP:20060406T211449Z\n"
            "LAST-MODIFIED:20060409T213201\n"
            "CREATED:20060409T213201\n"
            "LOCATION:my office\n"
            "DESCRIPTION:what the heck\\, let's even shout a bit\n"
            "CLASS:PUBLIC\n"
            "TRANSP:OPAQUE\n"
            "SEQUENCE:1\n"
            "END:VEVENT\n"
            "END:VCALENDAR\n" )
        {}
};

/**
 * EvolutionCalendarSource configured for access to tasks,
 * with a constructor as expected by TestEvolution
 */
class TestEvolutionTaskSource : public EvolutionCalendarSource
{
public:
    TestEvolutionTaskSource(
        const string &name,
        SyncSourceConfig *sc,
        const string &changeId = string(""),
        const string &id = string("") ) :
        EvolutionCalendarSource(
            E_CAL_SOURCE_TYPE_TODO,
            name,
            sc,
            changeId,
            id)
        {}
};

/**
 * TestEvolution configured for use with tasks
 */
class TestTask : public TestEvolution<TestEvolutionTaskSource>
{
public:
    TestTask() :
        TestEvolution<TestEvolutionTaskSource>(
            "todo",

            "SUMMARY:UID",
            "DESCRIPTION",
            
            /* initial item */
            "BEGIN:VCALENDAR\n"
            "PRODID:-//Ximian//NONSGML Evolution Calendar//EN\n"
            "VERSION:2.0\n"
            "METHOD:PUBLISH\n"
            "BEGIN:VTODO\n"
            "UID:20060417T173712Z-4360-727-1-2730@gollum\n"
            "DTSTAMP:20060417T173712Z\n"
            "SUMMARY:do me\n"
            "DESCRIPTION:to be done\n"
            "PRIORITY:0\n"
            "STATUS:IN-PROCESS\n"
            "CREATED:20060417T173712\n"
            "LAST-MODIFIED:20060417T173712\n"
            "END:VTODO\n"
            "END:VCALENDAR\n",

            /* default update item which replaces the initial item */
            "BEGIN:VCALENDAR\n"
            "PRODID:-//Ximian//NONSGML Evolution Calendar//EN\n"
            "VERSION:2.0\n"
            "METHOD:PUBLISH\n"
            "BEGIN:VTODO\n"
            "UID:20060417T173712Z-4360-727-1-2730@gollum\n"
            "DTSTAMP:20060417T173712Z\n"
            "SUMMARY:do me ASAP\n"
            "DESCRIPTION:to be done\n"
            "PRIORITY:1\n"
            "STATUS:IN-PROCESS\n"
            "CREATED:20060417T173712\n"
            "LAST-MODIFIED:20060417T173712\n"
            "END:VTODO\n"
            "END:VCALENDAR\n",

            /* complex update item which replaces the initial item - empty because not needed */
            "",

            /* change summary in initial item in testMerge() */
            "BEGIN:VCALENDAR\n"
            "PRODID:-//Ximian//NONSGML Evolution Calendar//EN\n"
            "VERSION:2.0\n"
            "METHOD:PUBLISH\n"
            "BEGIN:VTODO\n"
            "UID:20060417T173712Z-4360-727-1-2730@gollum\n"
            "DTSTAMP:20060417T173712Z\n"
            "SUMMARY:do me please\\, please\n"
            "DESCRIPTION:to be done\n"
            "PRIORITY:0\n"
            "STATUS:IN-PROCESS\n"
            "CREATED:20060417T173712\n"
            "LAST-MODIFIED:20060417T173712\n"
            "END:VTODO\n"
            "END:VCALENDAR\n",
            
            /* change priority */
            "BEGIN:VCALENDAR\n"
            "PRODID:-//Ximian//NONSGML Evolution Calendar//EN\n"
            "VERSION:2.0\n"
            "METHOD:PUBLISH\n"
            "BEGIN:VTODO\n"
            "UID:20060417T173712Z-4360-727-1-2730@gollum\n"
            "DTSTAMP:20060417T173712Z\n"
            "SUMMARY:do me\n"
            "DESCRIPTION:to be done\n"
            "PRIORITY:7\n"
            "STATUS:IN-PROCESS\n"
            "CREATED:20060417T173712\n"
            "LAST-MODIFIED:20060417T173712\n"
            "END:VTODO\n"
            "END:VCALENDAR\n" )
        {}
};
#endif /* ENABLE_ECAL */

#define SOURCE_TESTS \
    CPPUNIT_TEST( testOpen ); \
    CPPUNIT_TEST( testSimpleInsert ); \
    CPPUNIT_TEST( testLocalDeleteAll ); \
    CPPUNIT_TEST( testIterateTwice ); \
    CPPUNIT_TEST( testComplexInsert ); \
    CPPUNIT_TEST( testLocalUpdate ); \
    CPPUNIT_TEST( testChanges ); \
    CPPUNIT_TEST( testImport ); \
    CPPUNIT_TEST( testImportDelete ); \
    CPPUNIT_TEST( testManyChanges );

#define SYNC_TESTS \
    CPPUNIT_TEST( testRefreshFromServerSync ); \
    CPPUNIT_TEST( testRefreshFromClientSync ); \
    CPPUNIT_TEST( testTwoWaySync ); \
    CPPUNIT_TEST( testSlowSync ); \
    CPPUNIT_TEST( testDeleteAllSync ); \
    CPPUNIT_TEST( testDeleteAllRefresh ); \
    CPPUNIT_TEST( testRefreshSemantic ); \
    CPPUNIT_TEST( testRefreshStatus ); \
    CPPUNIT_TEST( testCopy ); \
    CPPUNIT_TEST( testUpdate ); \
    CPPUNIT_TEST( testComplexUpdate ); \
    CPPUNIT_TEST( testDelete ); \
    CPPUNIT_TEST( testMerge ); \
    CPPUNIT_TEST( testItems ); \
    CPPUNIT_TEST( testAddUpdate ); \
    CPPUNIT_TEST( testOneWayFromServer ); \
    CPPUNIT_TEST( testOneWayFromClient ); \
    CPPUNIT_TEST( testMaxMsg ); \
    CPPUNIT_TEST( testLargeObject ); \
    CPPUNIT_TEST( testLargeObjectBin ); \
    /* CPPUNIT_TEST( testLargeObjectEncoded ); requires a server which supports b64, disabled */ \
    CPPUNIT_TEST( testTwinning );

#define STRESS_TESTS \
    CPPUNIT_TEST( testManyItems );


#ifdef ENABLE_EBOOK
class ContactSource : public TestContact
{
    CPPUNIT_TEST_SUITE( ContactSource );
    SOURCE_TESTS;
    CPPUNIT_TEST_SUITE_END();
};
CPPUNIT_TEST_SUITE_REGISTRATION( ContactSource );

class ContactSync : public TestContact
{
    CPPUNIT_TEST_SUITE( ContactSync );
    SYNC_TESTS;
    CPPUNIT_TEST_SUITE_END();
};
CPPUNIT_TEST_SUITE_REGISTRATION( ContactSync );

class ContactStress : public TestContact
{
    CPPUNIT_TEST_SUITE( ContactStress );
    STRESS_TESTS;
    CPPUNIT_TEST_SUITE_END();
};
CPPUNIT_TEST_SUITE_REGISTRATION( ContactStress );
#endif /* ENABLE_EBOOK */

#ifdef ENABLE_ECAL
class CalendarSource : public TestCalendar
{
    CPPUNIT_TEST_SUITE( CalendarSource );
    SOURCE_TESTS;
    CPPUNIT_TEST_SUITE_END();
};
CPPUNIT_TEST_SUITE_REGISTRATION( CalendarSource );

class CalendarSync : public TestCalendar
{
    CPPUNIT_TEST_SUITE( CalendarSync );
    SYNC_TESTS;
    CPPUNIT_TEST_SUITE_END();
};
CPPUNIT_TEST_SUITE_REGISTRATION( CalendarSync );

class CalendarStress : public TestCalendar
{
    CPPUNIT_TEST_SUITE( CalendarStress );
    STRESS_TESTS;
    CPPUNIT_TEST_SUITE_END();
};
CPPUNIT_TEST_SUITE_REGISTRATION( CalendarStress );


class TaskSource : public TestTask
{
    CPPUNIT_TEST_SUITE( TaskSource );
    SOURCE_TESTS;
    CPPUNIT_TEST_SUITE_END();
};
CPPUNIT_TEST_SUITE_REGISTRATION( TaskSource );

class TaskSync : public TestTask
{
    CPPUNIT_TEST_SUITE( TaskSync );
    SYNC_TESTS;
    CPPUNIT_TEST_SUITE_END();
};
CPPUNIT_TEST_SUITE_REGISTRATION( TaskSync );

class TaskStress : public TestTask
{
    CPPUNIT_TEST_SUITE( TaskStress );
    STRESS_TESTS;
    CPPUNIT_TEST_SUITE_END();
};
CPPUNIT_TEST_SUITE_REGISTRATION( TaskStress );
#endif /* ENABLE_ECAL */



template<class T> void TestEvolution<T>::testOpen()
{
    T source(
        string("dummy"),
        NULL,
        m_changeIds[0],
        m_databases[0]);

    EVOLUTION_ASSERT_NO_THROW( source, source.open() );
}

template<class T> void TestEvolution<T>::insert(const string *data,
                                                int changeid,
                                                int config)
{
    if (!data) {
        data = &m_insertItem;
    }
    
    T source(
        string( "dummy" ),
        NULL,
        m_changeIds[changeid],
        m_databases[config]);
    
    EVOLUTION_ASSERT_NO_THROW( source, source.open() );
    EVOLUTION_ASSERT( source, source.beginSync() == 0 );
    int numItems;
    CPPUNIT_ASSERT_NO_THROW( numItems = countItems( source ) );
    SyncItem item;
    item.setData( data->c_str(), data->size() + 1 );
    int status;
    EVOLUTION_ASSERT_NO_THROW( source, status = source.addItem( item ) );
    CPPUNIT_ASSERT( item.getKey() != NULL );
    CPPUNIT_ASSERT( strlen( item.getKey() ) > 0 );

    EVOLUTION_ASSERT_NO_THROW( source, source.close() );
    EVOLUTION_ASSERT_NO_THROW( source, source.open() );
    EVOLUTION_ASSERT( source, source.beginSync() == 0 );
    CPPUNIT_ASSERT( status == STC_OK || status == STC_CONFLICT_RESOLVED_WITH_MERGE );
    CPPUNIT_ASSERT( countItems( source ) == numItems +
                    (status == STC_CONFLICT_RESOLVED_WITH_MERGE ? 0 : 1) );
    CPPUNIT_ASSERT( countNewItems( source ) == 0 );
    CPPUNIT_ASSERT( countUpdatedItems( source ) == 0 );
    CPPUNIT_ASSERT( countDeletedItems( source ) == 0 );
    SyncItem *sameItem;
    EVOLUTION_ASSERT_NO_THROW(
        source,
        sameItem = source.createItem( item.getKey(), item.getState() ) );
    CPPUNIT_ASSERT( sameItem != NULL );
    CPPUNIT_ASSERT( !strcmp( sameItem->getKey(), item.getKey() ) );
    delete sameItem;
}

template<class T> void TestEvolution<T>::testSimpleInsert()
{
    insert();
}

template<class T> void TestEvolution<T>::deleteAll(int config, bool insertFirst)
{
    if (insertFirst) {
        CPPUNIT_ASSERT(config == 0);
        insert();
    }
        
    T source(
        string( "dummy" ),
        NULL,
        m_changeIds[0],
        m_databases[config]);

    EVOLUTION_ASSERT_NO_THROW( source, source.open() );
    EVOLUTION_ASSERT( source, source.beginSync() == 0 );
    if (insertFirst) {
        int numItems = countItems( source );
        CPPUNIT_ASSERT( numItems > 0 );
    }

    SyncItem *item;
    EVOLUTION_ASSERT_NO_THROW( source, item = source.getFirstItem() );
    while ( item ) {
        EVOLUTION_ASSERT_NO_THROW( source, source.deleteItem( *item ) );
        delete item;
        EVOLUTION_ASSERT_NO_THROW( source, item = source.getNextItem() );
    }

    EVOLUTION_ASSERT_NO_THROW( source, source.close() );
    EVOLUTION_ASSERT_NO_THROW( source, source.open() );
    EVOLUTION_ASSERT( source, source.beginSync() == 0 );
    EVOLUTION_ASSERT_MESSAGE(
        "should be empty now",
        source,
        countItems( source ) == 0 );
    CPPUNIT_ASSERT( countNewItems( source ) == 0 );
    CPPUNIT_ASSERT( countUpdatedItems( source ) == 0 );
    CPPUNIT_ASSERT( countDeletedItems( source ) == 0 );    
}

template<class T> void TestEvolution<T>::testLocalDeleteAll()
{
    deleteAll(0, true);
}

template<class T> void TestEvolution<T>::testIterateTwice()
{
    T source(
        string( "dummy" ),
        NULL,
        m_changeIds[0],
        m_databases[0]);

    EVOLUTION_ASSERT_NO_THROW( source, source.open() );
    EVOLUTION_ASSERT( source, source.beginSync() == 0 );
    EVOLUTION_ASSERT_MESSAGE(
        "iterating twice should produce identical results",
        source,
        countItems(source) == countItems(source) );
}

template<class T> void TestEvolution<T>::testComplexInsert()
{
    testLocalDeleteAll();
    testSimpleInsert();
    testIterateTwice();
}

template<class T> void TestEvolution<T>::update( int config, const char *data )
{
    if (!data) {
        data = m_updateItem.c_str();
    }

    T source(
        string( "dummy" ),
        NULL,
        m_changeIds[config],
        m_databases[config]);
    EVOLUTION_ASSERT_NO_THROW( source, source.open() );
    EVOLUTION_ASSERT( source, source.beginSync() == 0 );
    SyncItem *item;
    EVOLUTION_ASSERT_NO_THROW( source, item = source.getFirstItem() );
    CPPUNIT_ASSERT( item );
    item->setData(data, strlen(data) + 1);
    EVOLUTION_ASSERT_NO_THROW( source, source.updateItem( *item ) );
    EVOLUTION_ASSERT_NO_THROW( source, source.close() );
    
    EVOLUTION_ASSERT_NO_THROW( source, source.open() );
    EVOLUTION_ASSERT( source, source.beginSync() == 0 );
    CPPUNIT_ASSERT( countItems( source ) == 1 );
    CPPUNIT_ASSERT( countNewItems( source ) == 0 );
    CPPUNIT_ASSERT( countUpdatedItems( source ) == 0 );
    CPPUNIT_ASSERT( countDeletedItems( source ) == 0 );
    SyncItem *modifiedItem;
    EVOLUTION_ASSERT_NO_THROW( source, modifiedItem = source.getFirstItem() );
    CPPUNIT_ASSERT( strlen( item->getKey() ) );
    CPPUNIT_ASSERT( !strcmp( item->getKey(), modifiedItem->getKey() ) );

    delete item;
    delete modifiedItem;
}

template<class T> void TestEvolution<T>::testLocalUpdate()
{
    testLocalDeleteAll();
    testSimpleInsert();
    update();
}

template<class T> void TestEvolution<T>::testChanges()
{
    testLocalDeleteAll();
    testSimpleInsert();

    T source(
        string( "dummy" ),
        NULL,
        m_changeIds[1],
        m_databases[0]);
        
    // update change id #1
    EVOLUTION_ASSERT_NO_THROW( source, source.open() );
    EVOLUTION_ASSERT( source, source.beginSync() == 0 );
    EVOLUTION_ASSERT_NO_THROW( source, source.close() );

    // no new changes
    EVOLUTION_ASSERT_NO_THROW( source, source.open() );
    EVOLUTION_ASSERT( source, source.beginSync() == 0 );
    CPPUNIT_ASSERT( countItems( source ) == 1 );
    CPPUNIT_ASSERT( countNewItems( source ) == 0 );
    CPPUNIT_ASSERT( countUpdatedItems( source ) == 0 );
    CPPUNIT_ASSERT( countDeletedItems( source ) == 0 );
    SyncItem *item;
    EVOLUTION_ASSERT_NO_THROW( source, item = source.getFirstItem() );
    EVOLUTION_ASSERT_NO_THROW( source, source.close() );

    // delete item again
    testLocalDeleteAll();
    EVOLUTION_ASSERT_NO_THROW( source, source.open() );
    EVOLUTION_ASSERT( source, source.beginSync() == 0 );
    CPPUNIT_ASSERT( countItems( source ) == 0 );
    CPPUNIT_ASSERT( countNewItems( source ) == 0 );
    CPPUNIT_ASSERT( countUpdatedItems( source ) == 0 );
    CPPUNIT_ASSERT( countDeletedItems( source ) == 1 );
    SyncItem *deletedItem;
    EVOLUTION_ASSERT_NO_THROW( source, deletedItem = source.getFirstDeletedItem() );
    CPPUNIT_ASSERT( strlen( item->getKey() ) );
    CPPUNIT_ASSERT( strlen( deletedItem->getKey() ) );
    CPPUNIT_ASSERT( !strcmp( item->getKey(), deletedItem->getKey() ) );
    EVOLUTION_ASSERT_NO_THROW( source, source.close() );

    delete item;
    delete deletedItem;
        
    // insert another item
    testSimpleInsert();
    EVOLUTION_ASSERT_NO_THROW( source, source.open() );
    EVOLUTION_ASSERT( source, source.beginSync() == 0 );
    CPPUNIT_ASSERT( countItems( source ) == 1 );
    CPPUNIT_ASSERT( countNewItems( source ) == 1 );
    CPPUNIT_ASSERT( countUpdatedItems( source ) == 0 );
    CPPUNIT_ASSERT( countDeletedItems( source ) == 0 );
    EVOLUTION_ASSERT_NO_THROW( source, item = source.getFirstItem() );
    SyncItem *newItem;
    EVOLUTION_ASSERT_NO_THROW( source, newItem = source.getFirstNewItem() );
    CPPUNIT_ASSERT( strlen( item->getKey() ) );
    CPPUNIT_ASSERT( strlen( newItem->getKey() ) );
    CPPUNIT_ASSERT( !strcmp( item->getKey(), newItem->getKey() ) );
    EVOLUTION_ASSERT_NO_THROW( source, source.close() );

    delete newItem;

    // update item
    update();
    EVOLUTION_ASSERT_NO_THROW( source, source.open() );
    EVOLUTION_ASSERT( source, source.beginSync() == 0 );
    CPPUNIT_ASSERT( countItems( source ) == 1 );
    CPPUNIT_ASSERT( countNewItems( source ) == 0 );
    CPPUNIT_ASSERT( countUpdatedItems( source ) == 1 );
    CPPUNIT_ASSERT( countDeletedItems( source ) == 0 );
    SyncItem *updatedItem;
    EVOLUTION_ASSERT_NO_THROW( source, updatedItem = source.getFirstUpdatedItem() );
    CPPUNIT_ASSERT( !strcmp( item->getKey(), updatedItem->getKey() ) );

    delete item;
    delete updatedItem;
}

void importItem(EvolutionSyncSource &source, string &data)
{
    if (data.size()) {
        SyncItem item;
        item.setData( data.c_str(), data.size() + 1 );
        item.setDataType( "raw" );
        EVOLUTION_ASSERT_NO_THROW( source, source.addItem( item ) );
        CPPUNIT_ASSERT( item.getKey() != NULL );
        CPPUNIT_ASSERT( strlen( item.getKey() ) > 0 );
        data = "";
    }
}

template<class T> void TestEvolution<T>::import()
{
    testLocalDeleteAll();
    
    T source(
        string( "dummy" ),
        NULL,
        m_changeIds[0],
        m_databases[0]);

    // insert test cases
    EVOLUTION_ASSERT_NO_THROW( source, source.open() );
    EVOLUTION_ASSERT( source, source.beginSync() == 0 );
    CPPUNIT_ASSERT( !countItems( source ) );
    
    // import the file
    ifstream input;
    input.open(m_testItems.c_str());
    CPPUNIT_ASSERT(!input.bad());
    CPPUNIT_ASSERT(input.is_open());
    string data, line;
    while (input) {
        do {
            getline(input, line);
            CPPUNIT_ASSERT(!input.bad());
            // empty line marks end of record
            if (line != "\r" && line.size() > 0) {
                data += line;
                data += "\n";
            } else {
                importItem(source, data);
            }
        } while(!input.eof());
    }
    importItem(source, data);
}

template<class T> void TestEvolution<T>::testImport()
{
    import();
    compareDatabases("testImport", m_testItems.c_str(), 0);
}

template<class T> void TestEvolution<T>::testImportDelete()
{
    import();

    // delete again, because it was observed that this did not
    // work right with calendars
    testLocalDeleteAll();
}

template<class T> void TestEvolution<T>::testManyChanges()
{
    deleteAll(0);

    T copy(
        string( "dummy" ),
        NULL,
        m_changeIds[1],
        m_databases[0]);

    // check that everything is empty, also resets change counter
    EVOLUTION_ASSERT_NO_THROW( copy, copy.open() );
    EVOLUTION_ASSERT( copy, copy.beginSync() == 0 );
    CPPUNIT_ASSERT( !countItems( copy ) );
    EVOLUTION_ASSERT_NO_THROW( copy, copy.close() );

    // now insert plenty of items
    int numItems = insertManyItems(0);

    // check that exactly this number of items is listed as new
    EVOLUTION_ASSERT_NO_THROW( copy, copy.open() );
    EVOLUTION_ASSERT( copy, copy.beginSync() == 0 );
    CPPUNIT_ASSERT( numItems == countItems( copy ) );
    CPPUNIT_ASSERT( numItems == countNewItems( copy ) );
    CPPUNIT_ASSERT( !countUpdatedItems( copy ) );
    CPPUNIT_ASSERT( !countDeletedItems( copy ) );
    EVOLUTION_ASSERT_NO_THROW( copy, copy.close() );

    // delete all items
    deleteAll(0);

    // verify again
    EVOLUTION_ASSERT_NO_THROW( copy, copy.open() );
    EVOLUTION_ASSERT( copy, copy.beginSync() == 0 );
    CPPUNIT_ASSERT( !countItems( copy ) );
    CPPUNIT_ASSERT( !countNewItems( copy ) );
    CPPUNIT_ASSERT( !countUpdatedItems( copy ) );
    CPPUNIT_ASSERT( numItems == countDeletedItems( copy ) );
    EVOLUTION_ASSERT_NO_THROW( copy, copy.close() );
}

template<class T> string TestEvolution<T>::doSync(const string &logfilesuffix,
                                                  int config,
                                                  SyncMode syncMode,
                                                  long maxMsgSize,
                                                  long maxObjSize,
                                                  bool loSupport,
                                                  const char *encoding)
{
    int res = 0;

    // use LOG_LEVEL_INFO to avoid extra debug output outside of
    // EvolutionSyncClient::sync() which will set the level to DEBUG
    // automatically
    string logfile = getCurrentTest() + "." + logfilesuffix;
    simplifyFilename(logfile);
    
    remove( logfile.c_str() );
    setLogFile( logfile.c_str(), TRUE );
    LOG.setLevel(LOG_LEVEL_INFO);
    try {
        set<string> sources;
        sources.insert(m_source[config]);
        class TestSyncClient : public EvolutionSyncClient {
        public:
            TestSyncClient(const string &server,
                           const set<string> &sources,
                           SyncMode syncMode,
                           long maxMsgSize,
                           long maxObjSize,
                           bool loSupport,
                           const char *encoding) :
                EvolutionSyncClient(server, false, sources),
                m_syncMode(syncMode),
                m_maxMsgSize(maxMsgSize),
                m_maxObjSize(maxObjSize),
                m_loSupport(loSupport),
                m_encoding(encoding)
                {}

        protected:
            virtual void prepare(SyncManagerConfig &config,
                                 SyncSource **sources) {
                for (SyncSource **source = sources;
                     *source;
                     source++) {
                    if (m_encoding) {
                        (*source)->getConfig().setEncoding(m_encoding);
                    }
                    (*source)->setPreferredSyncMode(m_syncMode);
                }
                DeviceConfig &dc(config.getDeviceConfig());
                dc.setLoSupport(m_loSupport);
                dc.setMaxObjSize(m_maxObjSize);
                AccessConfig &ac(config.getAccessConfig());
                ac.setMaxMsgSize(m_maxMsgSize);
                EvolutionSyncClient::prepare(config, sources);
            }

        private:
            const SyncMode m_syncMode;
            const long m_maxMsgSize;
            const long m_maxObjSize;
            const bool m_loSupport;
            const char *m_encoding;
        } client(m_syncConfigs[config], sources, syncMode, maxMsgSize, maxObjSize, loSupport, encoding);
        client.sync();
    } catch(...) {
        EvolutionSyncSource::handleException();
        res = 1;
    }
    string oldlogfile = getCurrentTest() + ".log";
    simplifyFilename(oldlogfile);
    setLogFile( oldlogfile.c_str(), TRUE );
    
    // make a copy of the server's log (if found), then truncate it
    if (m_serverLog.size()) {
        int fd = open( m_serverLog.c_str(), O_RDWR );

        if (fd >= 0) {
            // let the server finish
            sleep(m_syncDelay);

            string serverLog = logfile;
            size_t pos = serverLog.find( "client" );
            if (pos != serverLog.npos ) {
                serverLog.replace( pos, 6, "server" );
            } else {
                serverLog += ".server.log";
            }
            string cmd = string("cp ") + m_serverLog + " " + serverLog;
            system( cmd.c_str() );
            ftruncate( fd, 0 );
        } else {
            perror( m_serverLog.c_str() );
	}
    } else {
        // let the server finish
        sleep(m_syncDelay);
    }

    CPPUNIT_ASSERT( !res );

    return logfile;
}

template<class T> void TestEvolution<T>::testRefreshFromServerSync()
{
    doSync( "client.log", 0, SYNC_REFRESH_FROM_SERVER );
}

template<class T> void TestEvolution<T>::testRefreshFromClientSync()
{
    doSync( "client.log", 0, SYNC_REFRESH_FROM_CLIENT );
}

template<class T> void TestEvolution<T>::testTwoWaySync()
{
    doSync( "client.log", 0, SYNC_TWO_WAY );
}

template<class T> void TestEvolution<T>::testSlowSync()
{
    doSync( "client.log", 0, SYNC_SLOW );
}

template<class T> void TestEvolution<T>::deleteAll( const string &prefix, int config, DeleteAllMode mode )
{
    switch (mode) {
     case DELETE_ALL_SYNC:
        // a refresh from server would slightly reduce the amount of data exchanged, but not all servers support it
        deleteAll(config);
        doSync( prefix + ".deleteall.init.client.log", config, SYNC_TWO_WAY );
        deleteAll(config);
        doSync( prefix + ".deleteall.twoway.client.log", config, SYNC_TWO_WAY );
        break;
     case DELETE_ALL_REFRESH:
        // delete locally
        deleteAll(config);
        // refresh server
        doSync( prefix + ".deleteall.refreshserver.client.log", config, SYNC_REFRESH_FROM_CLIENT );
        break;
    }
}

template<class T> void TestEvolution<T>::testDeleteAllSync()
{
    T source(
        string( "dummy" ),
        NULL,
        m_changeIds[1],
        m_databases[0]);

    // copy something to server first
    testSimpleInsert();
    doSync( "insert.1.client.log", 0, SYNC_SLOW );

    deleteAll( "testDeleteAllSync", 0, DELETE_ALL_SYNC );
    
    // nothing stored locally?
    EVOLUTION_ASSERT_NO_THROW( source, source.open() );
    EVOLUTION_ASSERT( source, source.beginSync() == 0 );
    CPPUNIT_ASSERT( countItems( source ) == 0 );
    EVOLUTION_ASSERT_NO_THROW( source, source.close() );

    // make sure server really deleted everything
    doSync( "check.client.log", 0, SYNC_SLOW );
    EVOLUTION_ASSERT_NO_THROW( source, source.open() );
    EVOLUTION_ASSERT( source, source.beginSync() == 0 );
    CPPUNIT_ASSERT( countItems( source ) == 0 );
    EVOLUTION_ASSERT_NO_THROW( source, source.close() );
}

template<class T> void TestEvolution<T>::testDeleteAllRefresh()
{
    T source(
        string( "dummy" ),
        NULL,
        m_changeIds[1],
        m_databases[0]);

    // copy something to server first
    testSimpleInsert();
    doSync( "insert.2.client.log", 0, SYNC_SLOW );

    // now try deleting using another sync method
    deleteAll( "testDeleteAllRefresh", 0, DELETE_ALL_REFRESH );
    
    // nothing stored locally?
    EVOLUTION_ASSERT_NO_THROW( source, source.open() );
    EVOLUTION_ASSERT( source, source.beginSync() == 0 );
    CPPUNIT_ASSERT( countItems( source ) == 0 );
    EVOLUTION_ASSERT_NO_THROW( source, source.close() );

    // make sure server really deleted everything
    refreshClient("check.client.log", 0);
    EVOLUTION_ASSERT_NO_THROW( source, source.open() );
    EVOLUTION_ASSERT( source, source.beginSync() == 0 );
    CPPUNIT_ASSERT( countItems( source ) == 0 );
    EVOLUTION_ASSERT_NO_THROW( source, source.close() );
}

template<class T> void TestEvolution<T>::testRefreshSemantic()
{
    // insert a local item immediately before refresh with empty server
    // -> no items should exist afterwards
    deleteAll( "testRefreshSemantic", 0 );
    testSimpleInsert();
    doSync( "client.log", 0, SYNC_REFRESH_FROM_SERVER);
    
    T source(
        string( "dummy" ),
        NULL,
        m_changeIds[1],
        m_databases[0]);
    EVOLUTION_ASSERT_NO_THROW( source, source.open() );
    EVOLUTION_ASSERT( source, source.beginSync() == 0 );
    CPPUNIT_ASSERT( countItems( source ) == 0 );
}

template<class T> void TestEvolution<T>::testRefreshStatus()
{
    insert();
    deleteAll(0);
    insert();
    doSync(string("refresh-server.log"), 0, SYNC_REFRESH_FROM_CLIENT );
    string logfile = doSync(string("two-way.log"), 0, SYNC_TWO_WAY );

    // check that the two-way sync did not transfer any items
    ifstream file(logfile.c_str());
    while (file) {
        string line;
        getline(file, line);
        if (line.find("INFO")) {
            CPPUNIT_ASSERT( line.find(": new") == line.npos );
            CPPUNIT_ASSERT( line.find(": updated") == line.npos );
            CPPUNIT_ASSERT( line.find(": deleted") == line.npos );
        }
    }
}

template<class T> void TestEvolution<T>::doCopy( const string &prefix )
{
    deleteAll( prefix + ".0", 0 );
    deleteAll( prefix + ".1", 1 );

    // insert into first database, copy to server
    testSimpleInsert();
    doSync( prefix + ".0.client.log", 0, SYNC_TWO_WAY );

    // copy into second database
    doSync( prefix + ".1.client.log", 1, SYNC_TWO_WAY );

    T copy(
        string( "dummy" ),
        NULL,
        m_changeIds[0],
        m_databases[1]);
    EVOLUTION_ASSERT_NO_THROW( copy, copy.open() );
    EVOLUTION_ASSERT( copy, copy.beginSync() == 0 );
    CPPUNIT_ASSERT( countItems( copy ) == 1 );
}

template<class T> void TestEvolution<T>::testCopy()
{
    doCopy("copy");
    compareDatabases("");
}

template<class T> void TestEvolution<T>::testUpdate()
{
    doCopy( "copy" );
    update();

    doSync( "update.0.client.log", 0, SYNC_TWO_WAY );
    doSync( "update.1.client.log", 1, SYNC_TWO_WAY );

    compareDatabases("");
}

template<class T> void TestEvolution<T>::testComplexUpdate()
{
    if (!m_complexUpdateItem.size()) {
        return;
    }
    
    doCopy( "copy" );
    update( 0, m_complexUpdateItem.c_str() );

    doSync( "update.0.client.log", 0, SYNC_TWO_WAY );
    doSync( "update.1.client.log", 1, SYNC_TWO_WAY );

    compareDatabases("");
}

template<class T> void TestEvolution<T>::testDelete()
{
    doCopy( "copy" );
    testLocalDeleteAll();
    doSync( "delete.0.client.log", 0, SYNC_TWO_WAY );
    doSync( "delete.1.client.log", 1, SYNC_TWO_WAY );
    
    T copy(
        string( "dummy" ),
        NULL,
        m_changeIds[1],
        m_databases[1]);
    EVOLUTION_ASSERT_NO_THROW( copy, copy.open() );
    EVOLUTION_ASSERT( copy, copy.beginSync() == 0 );
    CPPUNIT_ASSERT( countItems( copy ) == 0 );
}

template<class T> void TestEvolution<T>::testMerge()
{
    doCopy( "copy" );

    // update in first client
    update( 0, m_mergeItem1.c_str() );
    // update in second client with a non-conflicting item
    update( 1, m_mergeItem2.c_str() );
    
    doSync( "send.0.client.log", 0, SYNC_TWO_WAY );
    doSync( "recv.1.client.log", 1, SYNC_TWO_WAY );

    // figure out how the conflict during recv.1.client was handled
    
    T client1(
        string( "dummy" ),
        NULL,
        m_changeIds[1],
        m_databases[1]);
    EVOLUTION_ASSERT_NO_THROW( client1, client1.open() );
    EVOLUTION_ASSERT( client1, client1.beginSync() == 0 );
    CPPUNIT_ASSERT( countItems( client1 ) >= 1 );
    CPPUNIT_ASSERT( countItems( client1 ) <= 2 );

    string result;
    if (countItems( client1 ) == 1 ) {
        result += "conflicting items where merged";
    } else {
        result += "both of the conflicting items where preserved";
    }
    cout << " " << result << " ";
    cout.flush();
    compareDatabases("after-conflict", NULL, 1, false);
    
    // doSync( "recv.0.client.log", 0, SYNC_TWO_WAY );

    // check that both address books are identical (regardless of actual content):
    // disabled because the address books won't be identical with Sync4j.
    // What happens instead is that the server sends a
    // STC_CONFLICT_RESOLVED_WITH_SERVER_DATA and
    // T::setItemStatus() creates a copy.
    // TODO: check what the server did (from testMerge.recv.1.client.log) and
    //       test either for identical address books or how many items exist
    // compareDatabases( 1 );

    // figure out how the conflict durinc 

#if 0
    // this code here assumes STC_CONFLICT_RESOLVED_WITH_SERVER_DATA
    T client0(
        string( "dummy" ),
        NULL,
        m_changeIds[0],
        m_databases[0]);
    
    EVOLUTION_ASSERT_NO_THROW( client0, client0.open() );
    EVOLUTION_ASSERT( client0, client0.beginSync() == 0 );
    CPPUNIT_ASSERT( 1 == countItems( client0 ) );
    
    T client1(
        string( "dummy" ),
        NULL,
        m_changeIds[1],
        m_databases[1]);
    
    EVOLUTION_ASSERT_NO_THROW( client1, client1.open() );
    EVOLUTION_ASSERT( client1, client1.beginSync() == 0 );
    CPPUNIT_ASSERT( 2 == countItems( client1 ) );
#endif
}

template<class T> void TestEvolution<T>::compareDatabases(const string &prefix,
                                                          const char *refData,
                                                          int copyDatabase,
                                                          bool raiseAssertion)
{
    string sourceData, copyData;
    if (refData) {
        sourceData = refData;
    } else {
        sourceData = getCurrentTest() + (prefix.size() ? "." : "") + prefix + ".source.test.vcf";
        simplifyFilename(sourceData);
        T source(
            string( "dummy" ),
            NULL,
            m_changeIds[0],
            m_databases[0]);
        EVOLUTION_ASSERT_NO_THROW( source, source.open() );
        EVOLUTION_ASSERT( source, source.beginSync() == 0 );

        ofstream osource(sourceData.c_str());
        source.exportData(osource);
        osource.close();
        CPPUNIT_ASSERT(!osource.bad());
    }

    copyData = getCurrentTest() + (prefix.size() ? "." : "") + prefix + ".copy.test.vcf";
    simplifyFilename(copyData);
    T copy(
        string( "dummy" ),
        NULL,
        m_changeIds[copyDatabase],
        m_databases[copyDatabase]);
    EVOLUTION_ASSERT_NO_THROW( copy, copy.open() );
    EVOLUTION_ASSERT( copy, copy.beginSync() == 0 );

    ofstream ocopy(copyData.c_str());
    copy.exportData(ocopy);
    ocopy.close();
    CPPUNIT_ASSERT(!ocopy.bad());

    stringstream cmd;

    string diff = getCurrentTest() + (prefix.size() ? "." : "") + prefix + ".diff";
    simplifyFilename(diff);
    cmd << "perl synccompare " << sourceData << " " << copyData << ">" << diff;
    cmd << "  || (echo; echo '*** " << diff << " non-empty ***'; cat " << diff << "; exit 1 )";

    string cmdstr = cmd.str();
    if (system(cmdstr.c_str()) && raiseAssertion) {
        CPPUNIT_ASSERT(((void)"address books identical", false));
    }
}

template<class T> void TestEvolution<T>::testItems()
{
    // clean server and first test database
    deleteAll("testItems", 0);

    // import data
    import();

    // transfer back and forth
    doSync( "send.client.log", 0, SYNC_TWO_WAY );
    refreshClient("recv.client.log", 1);
    
    compareDatabases("testItems", m_testItems.c_str());
}

template<class T> void TestEvolution<T>::testAddUpdate()
{
    // clean server and both test databases
    deleteAll("testItems", 0);
    refreshClient("delete.client.log", 1);

    // add item
    insert();
    doSync( "add.client.log", 0, SYNC_TWO_WAY );

    // update it
    update();
    doSync( "update.client.log", 0, SYNC_TWO_WAY );

    // now download the updated item into the second client
    doSync( "recv.client.log", 1, SYNC_TWO_WAY );

    // compare the two databases
    compareDatabases("", NULL, 1);
}

template<class T> void TestEvolution<T>::testTwinning()
{
    // clean server and first test database
    deleteAll("testItems", 0);

    // import data
    import();

    // send data to server
    doSync( "send.client.log", 0, SYNC_TWO_WAY );

    // ensure that client has the same data, ignoring data conversion
    // issues (those are covered by testItems())
    refreshClient("refresh.client.log", 0);

    // copy into second client
    refreshClient("recv.client.log", 1);

    // slow sync now should not change anything
    doSync( "twinning.client.log", 0, SYNC_SLOW );

    // compare
    compareDatabases("", NULL, 1);
}

template<class T> void TestEvolution<T>::testOneWayFromServer()
{
    T first(
        string( "dummy" ),
        NULL,
        m_changeIds[1],
        m_databases[0]);
    T second(
        string( "dummy" ),
        NULL,
        m_changeIds[1],
        m_databases[1]);

    // get clients and server in sync
    deleteAll( "delete.0", 0 );
    deleteAll( "delete.1", 1 );

    // check that everything is empty, also resets change counter
    EVOLUTION_ASSERT_NO_THROW( first, first.open() );
    EVOLUTION_ASSERT( first, first.beginSync() == 0 );
    CPPUNIT_ASSERT( !countItems( first ) );
    EVOLUTION_ASSERT_NO_THROW( first, first.close() );

    EVOLUTION_ASSERT_NO_THROW( second, second.open() );
    EVOLUTION_ASSERT( second, second.beginSync() == 0 );
    CPPUNIT_ASSERT( !countItems( second ) );
    EVOLUTION_ASSERT_NO_THROW( second, second.close() );

    // add one item on first client, copy to server
    insertManyItems(0, 1, 1);
    doSync("send.0.client.log", 0, SYNC_TWO_WAY);
    EVOLUTION_ASSERT_NO_THROW( first, first.open() );
    EVOLUTION_ASSERT( first, first.beginSync() == 0 );
    CPPUNIT_ASSERT( 1 == countItems( first ) );
    CPPUNIT_ASSERT( 1 == countNewItems( first ) );
    CPPUNIT_ASSERT( 0 == countDeletedItems( first ) );
    CPPUNIT_ASSERT( 0 == countUpdatedItems( first ) );
    EVOLUTION_ASSERT_NO_THROW( first, first.close() );
    
    // add a different item on second client, one-way-from-server
    // => one item added locally, none sent to server
    insertManyItems(1, 2, 1);
    EVOLUTION_ASSERT_NO_THROW( second, second.open() );
    EVOLUTION_ASSERT( second, second.beginSync() == 0 );
    CPPUNIT_ASSERT( 1 == countItems( second ) );
    CPPUNIT_ASSERT( 1 == countNewItems( second ) );
    CPPUNIT_ASSERT( 0 == countDeletedItems( second ) );
    CPPUNIT_ASSERT( 0 == countUpdatedItems( second ) );
    EVOLUTION_ASSERT_NO_THROW( second, second.close() );

    doSync("recv.1.client.log", 1, SYNC_ONE_WAY_FROM_SERVER);
    EVOLUTION_ASSERT_NO_THROW( second, second.open() );
    EVOLUTION_ASSERT( second, second.beginSync() == 0 );
    CPPUNIT_ASSERT( 2 == countItems( second ) );
    CPPUNIT_ASSERT( 1 == countNewItems( second ) );
    CPPUNIT_ASSERT( 0 == countDeletedItems( second ) );
    CPPUNIT_ASSERT( 0 == countUpdatedItems( second ) );
    EVOLUTION_ASSERT_NO_THROW( second, second.close() );
    
    // two-way sync with first client for verification
    // => no changes
    doSync("check.0.client.log", 0, SYNC_TWO_WAY);
    EVOLUTION_ASSERT_NO_THROW( first, first.open() );
    EVOLUTION_ASSERT( first, first.beginSync() == 0 );
    CPPUNIT_ASSERT( 1 == countItems( first ) );
    CPPUNIT_ASSERT( 0 == countNewItems( first ) );
    CPPUNIT_ASSERT( 0 == countDeletedItems( first ) );
    CPPUNIT_ASSERT( 0 == countUpdatedItems( first ) );
    EVOLUTION_ASSERT_NO_THROW( first, first.close() );

    // TODO: update on first client, then sync the change to
    // second one via two-way with first client and
    // one-way with second one

    // delete items on first client, sync to server
    deleteAll(0);
    EVOLUTION_ASSERT_NO_THROW( first, first.open() );
    EVOLUTION_ASSERT( first, first.beginSync() == 0 );
    CPPUNIT_ASSERT( 0 == countItems( first ) );
    CPPUNIT_ASSERT( 0 == countNewItems( first ) );
    CPPUNIT_ASSERT( 1 == countDeletedItems( first ) );
    CPPUNIT_ASSERT( 0 == countUpdatedItems( first ) );
    EVOLUTION_ASSERT_NO_THROW( first, first.close() );
    
    doSync("delete.0.client.log", 0, SYNC_TWO_WAY);
    EVOLUTION_ASSERT_NO_THROW( first, first.open() );
    EVOLUTION_ASSERT( first, first.beginSync() == 0 );
    CPPUNIT_ASSERT( 0 == countItems( first ) );
    CPPUNIT_ASSERT( 0 == countNewItems( first ) );
    CPPUNIT_ASSERT( 0 == countDeletedItems( first ) );
    CPPUNIT_ASSERT( 0 == countUpdatedItems( first ) );
    EVOLUTION_ASSERT_NO_THROW( first, first.close() );

    // sync the same change to second client
    // => one item left (the one inserted locally)
    doSync("delete.1.client.log", 1, SYNC_ONE_WAY_FROM_SERVER);
    EVOLUTION_ASSERT_NO_THROW( second, second.open() );
    EVOLUTION_ASSERT( second, second.beginSync() == 0 );
    CPPUNIT_ASSERT( 1 == countItems( second ) );
    CPPUNIT_ASSERT( 0 == countNewItems( second ) );
    CPPUNIT_ASSERT( 1 == countDeletedItems( second ) );
    CPPUNIT_ASSERT( 0 == countUpdatedItems( second ) );
    EVOLUTION_ASSERT_NO_THROW( second, second.close() );
}

template<class T> void TestEvolution<T>::testOneWayFromClient()
{
    T first(
        string( "dummy" ),
        NULL,
        m_changeIds[1],
        m_databases[0]);
    T second(
        string( "dummy" ),
        NULL,
        m_changeIds[1],
        m_databases[1]);

    // get clients and server in sync
    deleteAll( "delete.0", 0 );
    deleteAll( "delete.1", 1 );

    // check that everything is empty, also resets change counter
    EVOLUTION_ASSERT_NO_THROW( first, first.open() );
    EVOLUTION_ASSERT( first, first.beginSync() == 0 );
    CPPUNIT_ASSERT( !countItems( first ) );
    EVOLUTION_ASSERT_NO_THROW( first, first.close() );

    EVOLUTION_ASSERT_NO_THROW( second, second.open() );
    EVOLUTION_ASSERT( second, second.beginSync() == 0 );
    CPPUNIT_ASSERT( !countItems( second ) );
    EVOLUTION_ASSERT_NO_THROW( second, second.close() );

    // add one item on first client, copy to server
    insertManyItems(0, 1, 1);
    doSync("send.0.client.log", 0, SYNC_TWO_WAY);
    EVOLUTION_ASSERT_NO_THROW( first, first.open() );
    EVOLUTION_ASSERT( first, first.beginSync() == 0 );
    CPPUNIT_ASSERT( 1 == countItems( first ) );
    CPPUNIT_ASSERT( 1 == countNewItems( first ) );
    CPPUNIT_ASSERT( 0 == countDeletedItems( first ) );
    CPPUNIT_ASSERT( 0 == countUpdatedItems( first ) );
    EVOLUTION_ASSERT_NO_THROW( first, first.close() );
    
    // add a different item on second client, one-way-from-client
    // => no item added locally, one sent to server
    insertManyItems(1, 2, 1);
    EVOLUTION_ASSERT_NO_THROW( second, second.open() );
    EVOLUTION_ASSERT( second, second.beginSync() == 0 );
    CPPUNIT_ASSERT( 1 == countItems( second ) );
    CPPUNIT_ASSERT( 1 == countNewItems( second ) );
    CPPUNIT_ASSERT( 0 == countDeletedItems( second ) );
    CPPUNIT_ASSERT( 0 == countUpdatedItems( second ) );
    EVOLUTION_ASSERT_NO_THROW( second, second.close() );

    doSync("send.1.client.log", 1, SYNC_ONE_WAY_FROM_CLIENT);
    EVOLUTION_ASSERT_NO_THROW( second, second.open() );
    EVOLUTION_ASSERT( second, second.beginSync() == 0 );
    CPPUNIT_ASSERT( 1 == countItems( second ) );
    CPPUNIT_ASSERT( 0 == countNewItems( second ) );
    CPPUNIT_ASSERT( 0 == countDeletedItems( second ) );
    CPPUNIT_ASSERT( 0 == countUpdatedItems( second ) );
    EVOLUTION_ASSERT_NO_THROW( second, second.close() );
    
    // two-way sync with first client for verification
    // => receive one item
    doSync("check.0.client.log", 0, SYNC_TWO_WAY);
    EVOLUTION_ASSERT_NO_THROW( first, first.open() );
    EVOLUTION_ASSERT( first, first.beginSync() == 0 );
    CPPUNIT_ASSERT( 2 == countItems( first ) );
    CPPUNIT_ASSERT( 1 == countNewItems( first ) );
    CPPUNIT_ASSERT( 0 == countDeletedItems( first ) );
    CPPUNIT_ASSERT( 0 == countUpdatedItems( first ) );
    EVOLUTION_ASSERT_NO_THROW( first, first.close() );

    // TODO: update on second client, then sync the change to
    // first one via one-way-from-client with second client and
    // two-way with first one

    // delete items on second client, sync to server
    deleteAll(1);
    EVOLUTION_ASSERT_NO_THROW( second, second.open() );
    EVOLUTION_ASSERT( second, second.beginSync() == 0 );
    CPPUNIT_ASSERT( 0 == countItems( second ) );
    CPPUNIT_ASSERT( 0 == countNewItems( second ) );
    CPPUNIT_ASSERT( 1 == countDeletedItems( second ) );
    CPPUNIT_ASSERT( 0 == countUpdatedItems( second ) );
    EVOLUTION_ASSERT_NO_THROW( second, second.close() );
    
    doSync("delete.1.client.log", 1, SYNC_ONE_WAY_FROM_CLIENT);
    EVOLUTION_ASSERT_NO_THROW( second, second.open() );
    EVOLUTION_ASSERT( second, second.beginSync() == 0 );
    CPPUNIT_ASSERT( 0 == countItems( second ) );
    CPPUNIT_ASSERT( 0 == countNewItems( second ) );
    CPPUNIT_ASSERT( 0 == countDeletedItems( second ) );
    CPPUNIT_ASSERT( 0 == countUpdatedItems( second ) );
    EVOLUTION_ASSERT_NO_THROW( second, second.close() );

    // sync the same change to first client
    // => one item left (the one inserted locally)
    doSync("delete.0.client.log", 0, SYNC_TWO_WAY);
    EVOLUTION_ASSERT_NO_THROW( first, first.open() );
    EVOLUTION_ASSERT( first, first.beginSync() == 0 );
    CPPUNIT_ASSERT( 1 == countItems( first ) );
    CPPUNIT_ASSERT( 0 == countNewItems( first ) );
    CPPUNIT_ASSERT( 1 == countDeletedItems( first ) );
    CPPUNIT_ASSERT( 0 == countUpdatedItems( first ) );
    EVOLUTION_ASSERT_NO_THROW( first, first.close() );
}
    
template<class T> int TestEvolution<T>::insertManyItems(int config, int startIndex, int numItems, int size)
{
    T source(
        string( "dummy" ),
        NULL,
        m_changeIds[0],
        m_databases[config]);
    EVOLUTION_ASSERT_NO_THROW( source, source.open() );
    EVOLUTION_ASSERT( source, source.beginSync() == 0 );
    CPPUNIT_ASSERT( startIndex > 1 || !countItems( source ) );

    const char *setting = getenv("TEST_EVOLUTION_NUM_ITEMS");
    int firstIndex = startIndex;
    if (firstIndex < 0) {
        firstIndex = 1;
    }
    int lastIndex = firstIndex + (numItems >= 1 ? numItems : setting ? atoi(setting) : 200) - 1;
    for (int item = firstIndex; item <= lastIndex; item++) {
        string data = m_insertItem;
        stringstream prefix;

        prefix << setfill('0') << setw(3) << item << " ";
        
        for (list<string>::iterator it = m_uniqueProperties.begin();
             it != m_uniqueProperties.end();
             it++) {
            string property;
            // property is expected to not start directly at the
            // beginning
            property = "\n";
            property += *it;
            property += ":";
            size_t off = data.find(property);
            if (off != data.npos) {
                data.insert(off + property.size(), prefix.str());
            }
        }
        if (size > 0 && data.size() < size) {
            int additionalBytes = size - data.size();
            int added = 0;
            
            /* stuff the item so that it reaches at least that size */
            size_t off = data.find(m_sizeProperty);
            CPPUNIT_ASSERT(off != data.npos);
            stringstream stuffing;
            while(added < additionalBytes) {
                int linelen = 0;

                while(added + 4 < additionalBytes &&
                      linelen < 60) {
                    stuffing << 'x';
                    added++;
                    linelen++;
                }
                stuffing << "x\\nx";
                added += 4;
            }
            data.insert(off + 1 + m_sizeProperty.size(), stuffing.str());
        }
        
        importItem(source, data);
    }

    return lastIndex - firstIndex + 1;
}

template<class T> void TestEvolution<T>::doVarSizes(bool withMaxMsgSize,
                                                    bool withLargeObject,
                                                    const char *encoding)
{
    static const int maxMsgSize = 8 * 1024;
    
    // clean server and first test database
    deleteAll("varsizes", 0);

    // insert items, doubling their size, then restart with small size
    int item = 1;
    for (int i = 0; i < 2; i++ ) {
        int size = 1;
        while (size < 2 * maxMsgSize) {
            insertManyItems(0, item, 1, m_insertItem.size() + 10 + size);
            size *= 2;
            item++;
        }
    }

    // transfer to server
    doSync("send.client.log", 0, SYNC_TWO_WAY,
           withMaxMsgSize ? maxMsgSize : 0,
           withMaxMsgSize ? maxMsgSize * 100 : 0,
           withLargeObject,
           encoding);

    // copy to second client
    doSync("recv.client.log", 1, SYNC_REFRESH_FROM_SERVER,
           withLargeObject ? maxMsgSize : withMaxMsgSize ? maxMsgSize * 100 /* large enough so that server can sent the largest item */ : 0,
           withMaxMsgSize ? maxMsgSize * 100 : 0,
           withLargeObject,
           encoding);

    // compare
    compareDatabases("", NULL, 1);
}

template<class T> void TestEvolution<T>::testManyItems()
{
    // clean server and first test database
    deleteAll("testItems", 0);

    // import artificial data
    insertManyItems(0);
    
    // send data to server
    doSync( "send.client.log", 0, SYNC_TWO_WAY );

    // ensure that client has the same data, ignoring data conversion
    // issues (those are covered by testItems())
    refreshClient("refresh.client.log", 0);

    // also copy to second client
    refreshClient("recv.client.log", 1);

    // slow sync now should not change anything
    doSync( "twinning.client.log", 0, SYNC_SLOW );

    // compare
    compareDatabases("", NULL, 1);
}
