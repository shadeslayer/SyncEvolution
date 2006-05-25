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


#include <EvolutionContactSource.h>
#include <EvolutionCalendarSource.h>
#include <EvolutionSyncClient.h>
#include <common/spds/SyncStatus.h>
#include <posix/base/posixlog.h>

#include <string.h>
#include <fcntl.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>
using namespace std;

// until a better solution is found use the helper function from TestMain.cpp
extern const string &getCurrentTest();

// sets the log file to the current test




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
     * file containing items to be copied and compared after copying
     */
    const string m_testItems;
    
    /**
     * initial item which gets inserted by testSimpleInsert(),
     * default item to be used for updating it,
     * updates of it for triggering a merge conflict,
     * 
     */
    const string m_insertItem,
        m_updateItem,
        m_mergeItem1, m_mergeItem2;

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
    void doSync(const string &logfile, int config, SyncMode syncMode);

    /** deletes all items locally via T sync source */
    void deleteAll(int config);

    enum DeleteAllMode {
        DELETE_ALL_SYNC,   /**< make sure client and server are in sync,
                              delete locally,
                              sync again */
        DELETE_ALL_REFRESH /**< delete locally, refresh server */
    };

    /** deletes all items locally and on server, using different methods */
    void deleteAll( const string &prefix, int config, DeleteAllMode mode = DELETE_ALL_SYNC );

    /** create item in one database, then copy to the other */
    void doCopy( const string &prefix );

    /**
     * takes two databases, exports them,
     * then compares them using synccompare
     *
     * @param refData      existing file with source reference items (defaults to first config)
     * @param copyDatabase config of database with the copied items (defaults to second config)
     */
    void compareDatabases(const string &prefix, const char *refData = NULL, int copyDatabase = 1);

public:
    TestEvolution(
        const char *syncSourceName,
        const char *insertItem,
        const char *updateItem,
        const char *mergeItem1,
        const char *mergeItem2
        ) :
        m_syncSourceName(syncSourceName),
        m_insertItem(insertItem),
        m_updateItem(updateItem),
        m_mergeItem1(mergeItem1),
        m_mergeItem2(mergeItem2),
        m_testItems(string(syncSourceName) + ".tests")
        {}

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

    //
    // tests involving real synchronization:
    // - expects existing configurations called as in m_syncConfigs
    // - changes due to syncing are monitored via direct access through T sync source
    //

    // do a refresh sync without additional checks
    void testRefreshSync();
    // do a two-way sync without additional checks
    void testTwoWaySync();
    // do a slow sync without additional checks
    void testSlowSync();
    // delete all items, locally and on server
    void testDeleteAll();
    // test that a refresh sync of an empty server leads to an empty datatbase
    void testRefreshSemantic();
    // test that a two-way sync copies an item from one address book into the other
    void testCopy();
    // test that a two-way sync copies updates from database to the other
    void testUpdate();
    // test that a two-way sync deletes the copy of an item in the other database
    void testDelete();
    // test what the server does when it finds that different
    // fields of the same item have been modified
    void testMerge();
    // test what the server does when it has to execute a slow sync
    // with identical data on client and server:
    // expected behaviour is that nothing changes
    void testTwinning();
    // creates several items, transmits them back and forth and
    // then compares which of them have been preserved
    void testItems();
    
};

/**
 * TestEvolution configured for use with contacts
 */
class TestContact : public TestEvolution<EvolutionContactSource>
{
public:
    TestContact() :
        TestEvolution<EvolutionContactSource>(
            "addressbook",
            
            /* initial item */
            "BEGIN:VCARD\n"
            "VERSION:3.0\n"
            "TITLE:tester\n"
            "FN:John Doe\n"
            "N:Doe;John;;;\n"
            "TEL;TYPE=WORK;TYPE=VOICE:business 1\n"
            "X-EVOLUTION-FILE-AS:Doe\\, John\n"
            "X-MOZILLA-HTML:FALSE\n"
            "END:VCARD\n",

            /* default update item which replaces the initial item */
            "BEGIN:VCARD\n"
            "VERSION:3.0\n"
            "TITLE:tester\n"
            "FN:Joan Doe\n"
            "N:Doe;Joan;;;\n"
            "X-EVOLUTION-FILE-AS:Doe\\, Joan\n"
            "TEL;TYPE=WORK;TYPE=VOICE:business 1\n"
            "TEL;TYPE=WORK;TYPE=VOICE:business 2\n"
            "BDAY:2006-01-08\n"
            "X-MOZILLA-HTML:TRUE\n"
            "END:VCARD\n",

            /* add a telephone number to initial item in testMerge() */
            "BEGIN:VCARD\n"
            "VERSION:3.0\n"
            "TITLE:tester\n"
            "FN:John Doe\n"
            "N:Doe;John;;;\n"
            "X-EVOLUTION-FILE-AS:Doe\\, John\n"
            "X-MOZILLA-HTML:FALSE\n"
            "TEL;TYPE=WORK:business 1\n"
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

/**
 * EvolutionCalendarSource configured for access to calendars,
 * with a constructor as expected by TestEvolution
 */
class TestEvolutionCalendarSource : public EvolutionCalendarSource
{
public:
    TestEvolutionCalendarSource(
        const string &name,
        const string &changeId = string(""),
        const string &id = string("") ) :
        EvolutionCalendarSource(
            E_CAL_SOURCE_TYPE_EVENT,
            name,
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
            
            /* initial item */
            "BEGIN:VCALENDAR\n"
            "PRODID:-//Ximian//NONSGML Evolution Calendar//EN\n"
            "VERSION:2.0\n"
            "METHOD:PUBLISH\n"
            "BEGIN:VEVENT\n"
            "SUMMARY:phone meeting\n"
            "DTEND;20060406T163000Z\n"
            "DTSTART;20060406T160000Z\n"
            "UID:20060406T211449Z-4562-727-1-63@gollum\n"
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
            "UID:20060406T211449Z-4562-727-1-63@gollum\n"
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

            /* change location in initial item in testMerge() */
            "BEGIN:VCALENDAR\n"
            "PRODID:-//Ximian//NONSGML Evolution Calendar//EN\n"
            "VERSION:2.0\n"
            "METHOD:PUBLISH\n"
            "BEGIN:VEVENT\n"
            "SUMMARY:phone meeting\n"
            "DTEND;20060406T163000Z\n"
            "DTSTART;20060406T160000Z\n"
            "UID:20060406T211449Z-4562-727-1-63@gollum\n"
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
            "UID:20060406T211449Z-4562-727-1-63@gollum\n"
            "DTSTAMP:20060406T211449Z\n"
            "LAST-MODIFIED:20060409T213201\n"
            "CREATED:20060409T213201\n"
            "LOCATION:my office\n"
            "DESCRIPTION:what the heck, let's even shout a bit\n"
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
        const string &changeId = string(""),
        const string &id = string("") ) :
        EvolutionCalendarSource(
            E_CAL_SOURCE_TYPE_TODO,
            name,
            changeId,
            id)
        {}
};

/**
 * TestEvolution configured for use with calendars
 */
class TestTask : public TestEvolution<TestEvolutionTaskSource>
{
public:
    TestTask() :
        TestEvolution<TestEvolutionTaskSource>(
            "todo",
            
            /* initial item */
            "BEGIN:VCALENDAR\n"
            "PRODID:-//Ximian//NONSGML Evolution Calendar//EN\n"
            "VERSION:2.0\n"
            "METHOD:PUBLISH\n"
            "BEGIN:VTODO\n"
            "UID:20060417T173712Z-4360-727-1-2730@gollum\n"
            "DTSTAMP:20060417T173712Z\n"
            "SUMMARY:do me\n"
            "PRIORITY:0\n"
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
            "PRIORITY:1\n"
            "CREATED:20060417T173712\n"
            "LAST-MODIFIED:20060417T173712\n"
            "END:VTODO\n"
            "END:VCALENDAR\n",

            /* change summary in initial item in testMerge() */
            "BEGIN:VCALENDAR\n"
            "PRODID:-//Ximian//NONSGML Evolution Calendar//EN\n"
            "VERSION:2.0\n"
            "METHOD:PUBLISH\n"
            "BEGIN:VTODO\n"
            "UID:20060417T173712Z-4360-727-1-2730@gollum\n"
            "DTSTAMP:20060417T173712Z\n"
            "SUMMARY:do me please, please\n"
            "PRIORITY:0\n"
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
            "PRIORITY:7\n"
            "CREATED:20060417T173712\n"
            "LAST-MODIFIED:20060417T173712\n"
            "END:VTODO\n"
            "END:VCALENDAR\n" )
        {}
};


#define SOURCE_TESTS \
    CPPUNIT_TEST( testOpen ); \
    CPPUNIT_TEST( testSimpleInsert ); \
    CPPUNIT_TEST( testLocalDeleteAll ); \
    CPPUNIT_TEST( testIterateTwice ); \
    CPPUNIT_TEST( testComplexInsert ); \
    CPPUNIT_TEST( testLocalUpdate ); \
    CPPUNIT_TEST( testChanges ); \
    CPPUNIT_TEST( testImport );

#define SYNC_TESTS \
    CPPUNIT_TEST( testRefreshSync ); \
    CPPUNIT_TEST( testTwoWaySync ); \
    CPPUNIT_TEST( testSlowSync ); \
    CPPUNIT_TEST( testDeleteAll ); \
    CPPUNIT_TEST( testRefreshSemantic ); \
    CPPUNIT_TEST( testCopy ); \
    CPPUNIT_TEST( testUpdate ); \
    CPPUNIT_TEST( testDelete ); \
    CPPUNIT_TEST( testMerge ); \
    CPPUNIT_TEST( testItems ); \
    CPPUNIT_TEST( testTwinning );

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




template<class T> void TestEvolution<T>::testOpen()
{
    T source(
        string("dummy"),
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

template<class T> void TestEvolution<T>::deleteAll(int config)
{
    testSimpleInsert();
        
    T source(
        string( "dummy" ),
        m_changeIds[0],
        m_databases[config]);

    EVOLUTION_ASSERT_NO_THROW( source, source.open() );
    EVOLUTION_ASSERT( source, source.beginSync() == 0 );
    int numItems = countItems( source );
    CPPUNIT_ASSERT( numItems > 0 );

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
    deleteAll(0);
}

template<class T> void TestEvolution<T>::testIterateTwice()
{
    T source(
        string( "dummy" ),
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
        m_changeIds[config],
        m_databases[config]);
    EVOLUTION_ASSERT_NO_THROW( source, source.open() );
    EVOLUTION_ASSERT( source, source.beginSync() == 0 );
    SyncItem *item;
    EVOLUTION_ASSERT_NO_THROW( source, item = source.getFirstItem() );
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

    // delete again, because it was observed that this did not
    // work right with calendars
    testLocalDeleteAll();
}

template<class T> void TestEvolution<T>::doSync(const string &logfilesuffix, int config, SyncMode syncMode)
{
    int res = 0;

    // use LOG_LEVEL_INFO to avoid extra debug output outside of
    // EvolutionSyncClient::sync() which will set the level to DEBUG
    // automatically
    string logfile = getCurrentTest() + "." + logfilesuffix;
    remove( logfile.c_str() );
    setLogFile( logfile.c_str(), TRUE );
    LOG.setLevel(LOG_LEVEL_INFO);
    {
        set<string> sources;
        sources.insert(m_source[config]);
        EvolutionSyncClient client(m_syncConfigs[config], sources);
        try {
            client.sync(syncMode);
        } catch(...) {
            res = 1;
        }
    }
    string oldlogfile = getCurrentTest() + ".log";
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
}

template<class T> void TestEvolution<T>::testRefreshSync()
{
    doSync( "client.log", 0, SYNC_REFRESH_FROM_SERVER );
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
        // refresh (in case something is missing locally), then delete
        doSync( prefix + ".deleteall.refresh.client.log", config, SYNC_REFRESH_FROM_SERVER );
        testLocalDeleteAll();
        doSync( prefix + ".deleteall.twoway.client.log", config, SYNC_TWO_WAY );
        break;
     case DELETE_ALL_REFRESH:
        // delete locally
        testLocalDeleteAll();
        // refresh server
        doSync( prefix + ".deleteall.refreshserver.client.log", config, SYNC_REFRESH_FROM_CLIENT );
        break;
    }
}

template<class T> void TestEvolution<T>::testDeleteAll()
{
    T source(
        string( "dummy" ),
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
    doSync( "check.1.client.log", 0, SYNC_REFRESH_FROM_SERVER );
    EVOLUTION_ASSERT_NO_THROW( source, source.open() );
    EVOLUTION_ASSERT( source, source.beginSync() == 0 );
    CPPUNIT_ASSERT( countItems( source ) == 0 );
    EVOLUTION_ASSERT_NO_THROW( source, source.close() );    

    // copy something to server again
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
    doSync( "check.2.client.log", 0, SYNC_REFRESH_FROM_SERVER );
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
        m_changeIds[1],
        m_databases[0]);
    EVOLUTION_ASSERT_NO_THROW( source, source.open() );
    EVOLUTION_ASSERT( source, source.beginSync() == 0 );
    CPPUNIT_ASSERT( countItems( source ) == 0 );
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

template<class T> void TestEvolution<T>::testDelete()
{
    doCopy( "copy" );
    testLocalDeleteAll();
    doSync( "delete.0.client.log", 0, SYNC_TWO_WAY );
    doSync( "delete.1.client.log", 1, SYNC_TWO_WAY );
    
    T copy(
        string( "dummy" ),
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
    doSync( "recv.0.client.log", 0, SYNC_TWO_WAY );

    // check that both address books are identical (regardless of actual content):
    // disabled because the address books won't be identical with Sync4j.
    // What happens instead is that the server sends a
    // STC_CONFLICT_RESOLVED_WITH_SERVER_DATA and
    // T::setItemStatus() creates a copy.
    // TODO: check what the server did (from testMerge.recv.1.client.log) and
    //       test either for identical address books or how many items exist
    // compareDatabases( 1 );

    // this code here assumes STC_CONFLICT_RESOLVED_WITH_SERVER_DATA
    T client0(
        string( "dummy" ),
        m_changeIds[0],
        m_databases[0]);
    
    EVOLUTION_ASSERT_NO_THROW( client0, client0.open() );
    EVOLUTION_ASSERT( client0, client0.beginSync() == 0 );
    CPPUNIT_ASSERT( 1 == countItems( client0 ) );
    
    T client1(
        string( "dummy" ),
        m_changeIds[1],
        m_databases[1]);
    
    EVOLUTION_ASSERT_NO_THROW( client1, client1.open() );
    EVOLUTION_ASSERT( client1, client1.beginSync() == 0 );
    CPPUNIT_ASSERT( 2 == countItems( client1 ) );
}

/**
 * exports the data of all items into the file
 */
static void exportData( const string &filename, SyncSource &source )
{
    ofstream out(filename.c_str());
    
    for (SyncItem *sourceItem = source.getFirstItem();
         sourceItem;
         sourceItem = source.getNextItem()) {
        out << (const char *)sourceItem->getData() << "\n";
    }
    out.close();
    CPPUNIT_ASSERT( out.good() );
}

template<class T> void TestEvolution<T>::compareDatabases(const string &prefix,
                                                          const char *refData,
                                                          int copyDatabase)
{
    string sourceData, copyData;
    if (refData) {
        sourceData = refData;
    } else {
        sourceData = getCurrentTest() + (prefix.size() ? "." : "") + prefix + ".source.test.vcf";

        T source(
            string( "dummy" ),
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
    T copy(
        string( "dummy" ),
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
    cmd << "perl synccompare " << sourceData << " " << copyData << ">" << diff;
    cmd << "  || (echo; echo '*** " << diff << " non-empty ***'; cat " << diff << "; exit 1 )";

    string cmdstr = cmd.str();
    if (system(cmdstr.c_str())) {
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
    doSync( "recv.client.log", 1, SYNC_REFRESH_FROM_SERVER );

    
    compareDatabases("testItems", m_testItems.c_str());
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
    doSync( "refresh.client.log", 0, SYNC_REFRESH_FROM_SERVER );

    // slow sync now should not change anything
    doSync( "twinning.client.log", 0, SYNC_SLOW );

    // copy into second client and compare
    doSync( "recv.client.log", 1, SYNC_REFRESH_FROM_SERVER );
    compareDatabases("", NULL, 1);
}
