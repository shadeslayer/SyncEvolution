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
#include <EvolutionSyncClient.h>
#include <common/spds/SyncStatus.h>
#include <posix/base/posixlog.h>

#include <string.h>
#include <fcntl.h>

#include <iostream>
using namespace std;

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

class TestEvolution : public CppUnit::TestFixture
{
    CPPUNIT_TEST_SUITE( TestEvolution );

    CPPUNIT_TEST( testContactOpen );
    CPPUNIT_TEST( testContactSimpleInsert );
    CPPUNIT_TEST( testContactDeleteAll );
    CPPUNIT_TEST( testContactIterateTwice );
    CPPUNIT_TEST( testContactComplexInsert );
    CPPUNIT_TEST( testContactUpdate );
    CPPUNIT_TEST( testContactChanges );

    CPPUNIT_TEST( testRefreshSync );
    CPPUNIT_TEST( testTwoWaySync );
    CPPUNIT_TEST( testSlowSync );
    CPPUNIT_TEST( testDeleteAll );
    CPPUNIT_TEST( testRefreshSemantic );
    CPPUNIT_TEST( testCopy );
    CPPUNIT_TEST( testUpdate );
    CPPUNIT_TEST( testDelete );
    CPPUNIT_TEST( testMerge );
    CPPUNIT_TEST( testVCard );

    CPPUNIT_TEST_SUITE_END();

    /** the name of the contact databases */
    string m_contactNames[2];

    /** two different sync configurations, referencing the address books in m_contactNames */
    string m_syncConfigs[2];

    /** different change ids */
    string m_changeIds[2];

    /** filename of server log */
    string m_serverLog;

    /** assumes that one element is currently inserted and updates it */
    void contactUpdate( int config = 0, const char *vcard =
                        "BEGIN:VCARD\n"
                        "VERSION:3.0\n"
                        "URL:\n"
                        "TITLE:\n"
                        "ROLE:\n"
                        "X-EVOLUTION-MANAGER:\n"
                        "X-EVOLUTION-ASSISTANT:\n"
                        "NICKNAME:user1\n"
                        "X-EVOLUTION-SPOUSE:\n"
                        "NOTE:\n"
                        "FN:Joan Doe\n"
                        "N:Doe;Joan;;;\n"
                        "X-EVOLUTION-FILE-AS:Doe\\, Joan\n"
                        "X-EVOLUTION-BLOG-URL:\n"
                        "BDAY:2006-01-08\n"
                        "CALURI:\n"
                        "FBURL:\n"
                        "X-EVOLUTION-VIDEO-URL:\n"
                        "X-MOZILLA-HTML:FALSE\n"
                        "END:VCARD\n"
        );

    /** performs one sync operation */
    void doSync(const string &logfile, int config, SyncMode syncMode);

    /** deletes all contacts */
    void contactDeleteAll(int config);

    /** deletes all items locally and on server */
    void deleteAll( const string &prefix, int config );

    /** create item in one database, then copy to the other */
    void doCopy( const string &prefix );

    /** compare all entries in the two address books and assert that they are equal */
    void compareAddressbooks( int numtestcases );

public:
    void setUp() {
        m_contactNames[0] = "sync4jevolution test #1";
        m_contactNames[1] = "sync4jevolution test #2";
        m_syncConfigs[0] = "localhost_1";
        m_syncConfigs[1] = "localhost_2";
        m_changeIds[0] = "Sync4jEvolution Change ID #0";
        m_changeIds[1] = "Sync4jEvolution Change ID #1";
        const char *log = getenv( "SYNC4J_LOG" );
        if (log) {
            m_serverLog = log;
        }
    }
    void tearDown() {
    }

    //
    // tests involving only EvolutionContactSource:
    // - done on m_contactNames[0]
    // - change tracking is tested with two different
    //   change markers in m_changeIds[0/1]
    //

    // opening address book
    void testContactOpen();
    // insert one contact
    void testContactSimpleInsert();
    // delete all contacts
    void testContactDeleteAll();
    // restart scanning of contacts
    void testContactIterateTwice();
    // clean address book, then insert
    void testContactComplexInsert();
    // clean address book, insert contact, update it
    void testContactUpdate();
    // complex sequence of address book changes
    void testContactChanges();

    //
    // tests involving real synchronization:
    // - expects existing configurations called as in m_syncConfigs
    // - changes due to syncing are monitored via direct access through EvolutionContactSource
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
    // creates several contact test cases, transmits them back and forth and
    // then compares which of them have been preserved
    void testVCard();
    
};

// Registers the fixture into the 'registry'
CPPUNIT_TEST_SUITE_REGISTRATION( TestEvolution );

void TestEvolution::testContactOpen()
{
    EvolutionContactSource source( string( "dummy" ),
                                   m_changeIds[0],
                                   m_contactNames[0] );

    EVOLUTION_ASSERT_NO_THROW( source, source.open() );
}

void TestEvolution::testContactSimpleInsert()
{
    const char *vcard =
        "BEGIN:VCARD\n"
        "VERSION:3.0\n"
        "URL:\n"
        "TITLE:tester\n"
        "ROLE:\n"
        "X-EVOLUTION-MANAGER:\n"
        "X-EVOLUTION-ASSISTANT:\n"
        "NICKNAME:user1\n"
        "X-EVOLUTION-SPOUSE:\n"
        "NOTE:\n"
        "FN:John Doe\n"
        "N:Doe;John;;;\n"
        "X-EVOLUTION-FILE-AS:Doe\\, John\n"
        "X-EVOLUTION-BLOG-URL:\n"
        "CALURI:\n"
        "FBURL:\n"
        "X-EVOLUTION-VIDEO-URL:\n"
        "X-MOZILLA-HTML:FALSE\n"
        "END:VCARD\n";

    EvolutionContactSource source(
        string( "dummy" ),
        m_changeIds[0],
        m_contactNames[0] );
    
    EVOLUTION_ASSERT_NO_THROW( source, source.open() );
    EVOLUTION_ASSERT( source, source.beginSync() == 0 );
    int numItems;
    CPPUNIT_ASSERT_NO_THROW( numItems = countItems( source ) );
    SyncItem item;
    item.setData( vcard, strlen(vcard) + 1 );
    EVOLUTION_ASSERT_NO_THROW( source, source.addItem( item ) );
    CPPUNIT_ASSERT( item.getKey() != NULL );
    CPPUNIT_ASSERT( strlen( item.getKey() ) > 0 );

    EVOLUTION_ASSERT_NO_THROW( source, source.close() );
    EVOLUTION_ASSERT_NO_THROW( source, source.open() );
    EVOLUTION_ASSERT( source, source.beginSync() == 0 );
    CPPUNIT_ASSERT( countItems( source ) == numItems + 1 );
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

void TestEvolution::contactDeleteAll(int config)
{
    testContactSimpleInsert();
        
    EvolutionContactSource source( string( "dummy" ),
                                   m_changeIds[0],
                                   m_contactNames[config] );

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

void TestEvolution::testContactDeleteAll()
{
    contactDeleteAll(0);
}

void TestEvolution::testContactIterateTwice()
{
    EvolutionContactSource source( string( "dummy" ),
                                   m_changeIds[0],
                                   m_contactNames[0] );

    EVOLUTION_ASSERT_NO_THROW( source, source.open() );
    EVOLUTION_ASSERT( source, source.beginSync() == 0 );
    EVOLUTION_ASSERT_MESSAGE(
        "iterating twice should produce identical results",
        source,
        countItems(source) == countItems(source) );
}

void TestEvolution::testContactComplexInsert()
{
    testContactDeleteAll();
    testContactSimpleInsert();
    testContactIterateTwice();
}

void TestEvolution::contactUpdate( int config, const char *vcard )
{
    EvolutionContactSource source( string( "dummy" ),
                                   m_changeIds[config],
                                   m_contactNames[config] );
    EVOLUTION_ASSERT_NO_THROW( source, source.open() );
    EVOLUTION_ASSERT( source, source.beginSync() == 0 );
    SyncItem *item;
    EVOLUTION_ASSERT_NO_THROW( source, item = source.getFirstItem() );
    item->setData( vcard, strlen( vcard ) + 1 );
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

void TestEvolution::testContactUpdate()
{
    testContactDeleteAll();
    testContactSimpleInsert();
    contactUpdate();
}

void TestEvolution::testContactChanges()
{
    testContactDeleteAll();
    testContactSimpleInsert();

    EvolutionContactSource source( string( "dummy" ),
                                   m_changeIds[1],
                                   m_contactNames[0] );
        
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
    testContactDeleteAll();
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
    testContactSimpleInsert();
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
    contactUpdate();
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

void TestEvolution::doSync(const string &logfile, int config, SyncMode syncMode)
{
    int res = 0;

    remove( logfile.c_str() );
    setLogFile( logfile.c_str(), TRUE );
    {
        EvolutionSyncClient client( m_syncConfigs[config] );
        try {
            client.sync(syncMode);
        } catch(...) {
            res = 1;
        }
    }
    setLogFile( "sync.log", FALSE );
    
    // make a copy of the server's log (if found), then truncate it
    if (m_serverLog.size()) {
        int fd = open( m_serverLog.c_str(), O_RDWR );

        if (fd >= 0) {
            // let the server finish
            sleep(10);

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
        }
    } else {
        // let the server finish
        sleep(10);
    }

    CPPUNIT_ASSERT( !res );
}

void TestEvolution::testRefreshSync()
{
    doSync( "testRefreshSync.client.log", 0, SYNC_REFRESH_FROM_SERVER );
}

void TestEvolution::testTwoWaySync()
{
    doSync( "testTwoWaySync.client.log", 0, SYNC_TWO_WAY );
}

void TestEvolution::testSlowSync()
{
    doSync( "testSlowSync.client.log", 0, SYNC_SLOW );
}

void TestEvolution::deleteAll( const string &prefix, int config )
{
    // refresh (in case something is missing locally), then delete
    doSync( prefix + ".deleteall.refresh.client.log", config, SYNC_REFRESH_FROM_SERVER );
    testContactDeleteAll();
    doSync( prefix + ".deleteall.twoway.client.log", config, SYNC_TWO_WAY );
}

void TestEvolution::testDeleteAll()
{
    // copy something to server first
    testContactSimpleInsert();
    doSync( "testDeleteAll.insert.client.log", 0, SYNC_SLOW );

    deleteAll( "testDeleteAll", 0 );
    
    // nothing stored locally?
    EvolutionContactSource source( string( "dummy" ),
                                   m_changeIds[1],
                                   m_contactNames[0] );
    EVOLUTION_ASSERT_NO_THROW( source, source.open() );
    EVOLUTION_ASSERT( source, source.beginSync() == 0 );
    CPPUNIT_ASSERT( countItems( source ) == 0 );
    EVOLUTION_ASSERT_NO_THROW( source, source.close() );
}

void TestEvolution::testRefreshSemantic()
{
    // insert a local item immediately before refresh with empty server
    // -> no items should exist afterwards
    deleteAll( "testRefreshSemantic", 0 );
    testContactSimpleInsert();
    doSync( "testRefreshSemantic.client.log", 0, SYNC_REFRESH_FROM_SERVER);
    
    EvolutionContactSource source( string( "dummy" ),
                                   m_changeIds[1],
                                   m_contactNames[0] );
    EVOLUTION_ASSERT_NO_THROW( source, source.open() );
    EVOLUTION_ASSERT( source, source.beginSync() == 0 );
    CPPUNIT_ASSERT( countItems( source ) == 0 );
}

void TestEvolution::doCopy( const string &prefix )
{
    deleteAll( prefix + ".0", 0 );
    deleteAll( prefix + ".1", 1 );

    // insert into first database, copy to server
    testContactSimpleInsert();
    doSync( prefix + ".0.client.log", 0, SYNC_TWO_WAY );

    // copy into second database
    doSync( prefix + ".1.client.log", 1, SYNC_TWO_WAY );

    EvolutionContactSource source( string( "dummy" ),
                                   m_changeIds[0],
                                   m_contactNames[1] );
    EVOLUTION_ASSERT_NO_THROW( source, source.open() );
    EVOLUTION_ASSERT( source, source.beginSync() == 0 );
    CPPUNIT_ASSERT( countItems( source ) == 1 );
}

void TestEvolution::testCopy()
{
    doCopy( "testCopy" );
    compareAddressbooks( 1 );
}

void TestEvolution::testUpdate()
{
    doCopy( "testUpdate.copy" );
    contactUpdate();

    doSync( "testUpdate.update.0.client.log", 0, SYNC_TWO_WAY );
    doSync( "testUpdate.update.1.client.log", 1, SYNC_TWO_WAY );

    compareAddressbooks( 1 );
}

void TestEvolution::testDelete()
{
    doCopy( "testDelete.copy" );
    testContactDeleteAll();
    doSync( "testDelete.delete.0.client.log", 0, SYNC_TWO_WAY );
    doSync( "testDelete.delete.1.client.log", 1, SYNC_TWO_WAY );
    
    EvolutionContactSource source( string( "dummy" ),
                                   m_changeIds[1],
                                   m_contactNames[1] );
    EVOLUTION_ASSERT_NO_THROW( source, source.open() );
    EVOLUTION_ASSERT( source, source.beginSync() == 0 );
    CPPUNIT_ASSERT( countItems( source ) == 0 );
}

void TestEvolution::testMerge()
{
    doCopy( "testMerge.copy" );

    // add a telephone number
    contactUpdate( 0,
                   "BEGIN:VCARD\n"
                   "VERSION:3.0\n"
                   "URL:\n"
                   "TITLE:tester\n"
                   "ROLE:\n"
                   "X-EVOLUTION-MANAGER:\n"
                   "X-EVOLUTION-ASSISTANT:\n"
                   "NICKNAME:user1\n"
                   "X-EVOLUTION-SPOUSE:\n"
                   "NOTE:\n"
                   "FN:John Doe\n"
                   "N:Doe;John;;;\n"
                   "X-EVOLUTION-FILE-AS:Doe\\, John\n"
                   "X-EVOLUTION-BLOG-URL:\n"
                   "CALURI:\n"
                   "FBURL:\n"
                   "X-EVOLUTION-VIDEO-URL:\n"
                   "X-MOZILLA-HTML:FALSE\n"
                   "TEL;TYPE=WORK:business 1\n"
                   "END:VCARD\n" );
    // add a birthday and modify the title
    contactUpdate( 1,
                   "BEGIN:VCARD\n"
                   "VERSION:3.0\n"
                   "URL:\n"
                   "TITLE:developer\n"
                   "ROLE:\n"
                   "X-EVOLUTION-MANAGER:\n"
                   "X-EVOLUTION-ASSISTANT:\n"
                   "NICKNAME:user1\n"
                   "X-EVOLUTION-SPOUSE:\n"
                   "NOTE:\n"
                   "FN:John Doe\n"
                   "N:Doe;John;;;\n"
                   "X-EVOLUTION-FILE-AS:Doe\\, John\n"
                   "X-EVOLUTION-BLOG-URL:\n"
                   "CALURI:\n"
                   "FBURL:\n"
                   "X-EVOLUTION-VIDEO-URL:\n"
                   "X-MOZILLA-HTML:FALSE\n"
                   "BDAY:2006-01-08\n"
                   "END:VCARD\n" );
    
    doSync( "testMerge.send.0.client.log", 0, SYNC_TWO_WAY );
    doSync( "testMerge.recv.1.client.log", 1, SYNC_TWO_WAY );
    doSync( "testMerge.recv.0.client.log", 0, SYNC_TWO_WAY );

    // check that both address books are identical (regardless of actual content):
    // disabled because the address books won't be identical with Sync4j.
    // What happens instead is that the server sends a
    // STC_CONFLICT_RESOLVED_WITH_SERVER_DATA and
    // EvolutionContactSource::setItemStatus() creates a copy.
    // TODO: check what the server did (from testMerge.recv.1.client.log) and
    //       test either for identical address books or how many items exist
    // compareAddressbooks( 1 );

    // this code here assumes STC_CONFLICT_RESOLVED_WITH_SERVER_DATA
    EvolutionContactSource client0(
        string( "dummy" ),
        m_changeIds[0],
        m_contactNames[0] );
    
    EVOLUTION_ASSERT_NO_THROW( client0, client0.open() );
    EVOLUTION_ASSERT( client0, client0.beginSync() == 0 );
    CPPUNIT_ASSERT( 1 == countItems( client0 ) );
    
    EvolutionContactSource client1(
        string( "dummy" ),
        m_changeIds[1],
        m_contactNames[1] );
    
    EVOLUTION_ASSERT_NO_THROW( client1, client1.open() );
    EVOLUTION_ASSERT( client1, client1.beginSync() == 0 );
    CPPUNIT_ASSERT( 2 == countItems( client1 ) );
}

static int compareContacts( const string &name, EContact *sourceContact, EContact *copiedContact )
{
    const EContactField essential[] = {
	E_CONTACT_FILE_AS,     	 /* string field */

	/* Name fields */
	E_CONTACT_FULL_NAME,   	 /* string field */
	E_CONTACT_GIVEN_NAME,  	 /* synthetic string field */
	E_CONTACT_FAMILY_NAME, 	 /* synthetic string field */
	E_CONTACT_NICKNAME,    	 /* string field */

	/* Email fields */
	E_CONTACT_EMAIL_1,     	 /* synthetic string field */
	E_CONTACT_EMAIL_2,     	 /* synthetic string field */
	E_CONTACT_EMAIL_3,     	 /* synthetic string field */
	E_CONTACT_EMAIL_4,       /* synthetic string field */

	E_CONTACT_MAILER,        /* string field */

	/* Address Labels */
	E_CONTACT_ADDRESS_LABEL_HOME,  /* synthetic string field */
	E_CONTACT_ADDRESS_LABEL_WORK,  /* synthetic string field */
	E_CONTACT_ADDRESS_LABEL_OTHER, /* synthetic string field */

	/* Phone fields */
	E_CONTACT_PHONE_ASSISTANT,
	E_CONTACT_PHONE_BUSINESS,
	E_CONTACT_PHONE_BUSINESS_2,
	E_CONTACT_PHONE_BUSINESS_FAX,
	E_CONTACT_PHONE_CALLBACK,
	E_CONTACT_PHONE_CAR,
	E_CONTACT_PHONE_COMPANY,
	E_CONTACT_PHONE_HOME,
	E_CONTACT_PHONE_HOME_2,
	E_CONTACT_PHONE_HOME_FAX,
	E_CONTACT_PHONE_ISDN,
	E_CONTACT_PHONE_MOBILE,
	E_CONTACT_PHONE_OTHER,
	E_CONTACT_PHONE_OTHER_FAX,
	E_CONTACT_PHONE_PAGER,
	E_CONTACT_PHONE_PRIMARY,
	E_CONTACT_PHONE_RADIO,
	E_CONTACT_PHONE_TELEX,
	E_CONTACT_PHONE_TTYTDD,

	/* Organizational fields */
	E_CONTACT_ORG,        	 /* string field */
	E_CONTACT_ORG_UNIT,   	 /* string field */
	E_CONTACT_OFFICE,     	 /* string field */
	E_CONTACT_TITLE,      	 /* string field */
	E_CONTACT_ROLE,       	 /* string field */
	E_CONTACT_MANAGER,    	 /* string field */
	E_CONTACT_ASSISTANT,  	 /* string field */

	/* Web fields */
	E_CONTACT_HOMEPAGE_URL,  /* string field */
	E_CONTACT_BLOG_URL,      /* string field */

	/* Contact categories */
	E_CONTACT_CATEGORIES,    /* string field */

	/* Collaboration fields */
	E_CONTACT_CALENDAR_URI,  /* string field */
	E_CONTACT_FREEBUSY_URL,  /* string field */
	E_CONTACT_ICS_CALENDAR,  /* string field */
	E_CONTACT_VIDEO_URL,      /* string field */

	/* misc fields */
	E_CONTACT_SPOUSE,        /* string field */
	E_CONTACT_NOTE,          /* string field */

	E_CONTACT_IM_AIM_HOME_1,       /* Synthetic string field */
	E_CONTACT_IM_AIM_HOME_2,       /* Synthetic string field */
	E_CONTACT_IM_AIM_HOME_3,       /* Synthetic string field */
	E_CONTACT_IM_AIM_WORK_1,       /* Synthetic string field */
	E_CONTACT_IM_AIM_WORK_2,       /* Synthetic string field */
	E_CONTACT_IM_AIM_WORK_3,       /* Synthetic string field */
	E_CONTACT_IM_GROUPWISE_HOME_1, /* Synthetic string field */
	E_CONTACT_IM_GROUPWISE_HOME_2, /* Synthetic string field */
	E_CONTACT_IM_GROUPWISE_HOME_3, /* Synthetic string field */
	E_CONTACT_IM_GROUPWISE_WORK_1, /* Synthetic string field */
	E_CONTACT_IM_GROUPWISE_WORK_2, /* Synthetic string field */
	E_CONTACT_IM_GROUPWISE_WORK_3, /* Synthetic string field */
	E_CONTACT_IM_JABBER_HOME_1,    /* Synthetic string field */
	E_CONTACT_IM_JABBER_HOME_2,    /* Synthetic string field */
	E_CONTACT_IM_JABBER_HOME_3,    /* Synthetic string field */
	E_CONTACT_IM_JABBER_WORK_1,    /* Synthetic string field */
	E_CONTACT_IM_JABBER_WORK_2,    /* Synthetic string field */
	E_CONTACT_IM_JABBER_WORK_3,    /* Synthetic string field */
	E_CONTACT_IM_YAHOO_HOME_1,     /* Synthetic string field */
	E_CONTACT_IM_YAHOO_HOME_2,     /* Synthetic string field */
	E_CONTACT_IM_YAHOO_HOME_3,     /* Synthetic string field */
	E_CONTACT_IM_YAHOO_WORK_1,     /* Synthetic string field */
	E_CONTACT_IM_YAHOO_WORK_2,     /* Synthetic string field */
	E_CONTACT_IM_YAHOO_WORK_3,     /* Synthetic string field */
	E_CONTACT_IM_MSN_HOME_1,       /* Synthetic string field */
	E_CONTACT_IM_MSN_HOME_2,       /* Synthetic string field */
	E_CONTACT_IM_MSN_HOME_3,       /* Synthetic string field */
	E_CONTACT_IM_MSN_WORK_1,       /* Synthetic string field */
	E_CONTACT_IM_MSN_WORK_2,       /* Synthetic string field */
	E_CONTACT_IM_MSN_WORK_3,       /* Synthetic string field */
	E_CONTACT_IM_ICQ_HOME_1,       /* Synthetic string field */
	E_CONTACT_IM_ICQ_HOME_2,       /* Synthetic string field */
	E_CONTACT_IM_ICQ_HOME_3,       /* Synthetic string field */
	E_CONTACT_IM_ICQ_WORK_1,       /* Synthetic string field */
	E_CONTACT_IM_ICQ_WORK_2,       /* Synthetic string field */
	E_CONTACT_IM_ICQ_WORK_3        /* Synthetic string field */
    };

    // some of these are important, but cannot be compare yet
    const EContactField ignore[] = {
	/* Address fields */
	E_CONTACT_ADDRESS,       /* Multi-valued structured (EContactAddress) */
	E_CONTACT_ADDRESS_HOME,  /* synthetic structured field (EContactAddress) */
	E_CONTACT_ADDRESS_WORK,  /* synthetic structured field (EContactAddress) */
	E_CONTACT_ADDRESS_OTHER, /* synthetic structured field (EContactAddress) */

	E_CONTACT_CATEGORY_LIST, /* multi-valued */

	/* Photo/Logo */
	E_CONTACT_PHOTO,       	 /* structured field (EContactPhoto) */
	E_CONTACT_LOGO,       	 /* structured field (EContactPhoto) */

	E_CONTACT_NAME,        	 /* structured field (EContactName) */
	E_CONTACT_EMAIL,       	 /* Multi-valued */

	/* Instant Messaging fields */
	E_CONTACT_IM_AIM,     	 /* Multi-valued */
	E_CONTACT_IM_GROUPWISE,  /* Multi-valued */
	E_CONTACT_IM_JABBER,  	 /* Multi-valued */
	E_CONTACT_IM_YAHOO,   	 /* Multi-valued */
	E_CONTACT_IM_MSN,     	 /* Multi-valued */
	E_CONTACT_IM_ICQ,     	 /* Multi-valued */
       
	E_CONTACT_WANTS_HTML,    /* boolean field */

	/* fields used for describing contact lists.  a contact list
	   is just a contact with _IS_LIST set to true.  the members
	   are listed in the _EMAIL field. */
	E_CONTACT_IS_LIST,             /* boolean field */
	E_CONTACT_LIST_SHOW_ADDRESSES, /* boolean field */


	E_CONTACT_BIRTH_DATE,    /* structured field (EContactDate) */
	E_CONTACT_ANNIVERSARY,   /* structured field (EContactDate) */

	/* Security Fields */
	E_CONTACT_X509_CERT      /* structured field (EContactCert) */
    };

    int identical = 1;
    for (int field = 0;
         field < sizeof(essential)/sizeof(essential[0]);
         field++) {
        EContactField fieldType = essential[field];
        string source = e_contact_get_const( sourceContact, fieldType ) ?
            (const char *)e_contact_get_const( sourceContact, fieldType ) :
            "";
        string copy = e_contact_get_const( copiedContact, fieldType ) ?
            (const char *)e_contact_get_const( copiedContact, fieldType ) :
            "";

        if (source != copy) {
            identical = 0;
            cout << name << ": " << e_contact_field_name( fieldType ) << " not identical.\n";
            cout << "   Source: " << source << "\n";
            cout << "   Copy: " << copy << "\n";
        }
    }

    return identical;
}

void TestEvolution::compareAddressbooks( int numtestcases )
{
    int identical = 1;
    int allcopied = 1;
    
    // now iterate over copied contacts and compare against original ones
    EvolutionContactSource source(
        string( "dummy" ),
        m_changeIds[0],
        m_contactNames[0] );
    EVOLUTION_ASSERT_NO_THROW( source, source.open() );
    EVOLUTION_ASSERT( source, source.beginSync() == 0 );
    EvolutionContactSource copy(
        string( "dummy" ),
        m_changeIds[1],
        m_contactNames[1] );
    EVOLUTION_ASSERT_NO_THROW( copy, copy.open() );
    EVOLUTION_ASSERT( copy, copy.beginSync() == 0 );

    for (SyncItem *sourceItem = source.getFirstItem();
         sourceItem;
         sourceItem = source.getNextItem()) {
        // match items by nickname
        EContact *sourceContact = source.getContact( sourceItem->getKey() );
        CPPUNIT_ASSERT( sourceContact );
        string sourceNick = e_contact_get_const( sourceContact, E_CONTACT_NICKNAME ) ?
            (const char *)e_contact_get_const( sourceContact, E_CONTACT_NICKNAME ) :
            "";
            

        int found = 0;
        for (SyncItem *copiedItem = copy.getFirstItem();
             copiedItem && !found;
             copiedItem = copy.getNextItem()) {
            EContact *copiedContact = copy.getContact( copiedItem->getKey() );
            CPPUNIT_ASSERT( copiedContact );
            string copiedNick = e_contact_get_const( copiedContact, E_CONTACT_NICKNAME ) ?
                (const char *)e_contact_get_const( copiedContact, E_CONTACT_NICKNAME ) :
                "";

            if (copiedNick == sourceNick) {
                found = 1;
                if (!compareContacts( sourceNick, sourceContact, copiedContact )) {
                    identical = 0;
                }
            }
        }
        if (!found) {
            cout << sourceNick << " not found in copy, perhaps the nickname was modified?\n";
            allcopied = 0;
        }
    }    

    CPPUNIT_ASSERT( numtestcases == countItems( source ) );
    CPPUNIT_ASSERT( identical );
    CPPUNIT_ASSERT( numtestcases == countItems( copy ) );
    CPPUNIT_ASSERT( allcopied );
}

void TestEvolution::testVCard()
{
    // these test cases were created in Evolution, exported
    // as vcard 3.0 and converted to C source code with
    // this perl snippet:
    // perl -e '$_ = join("",<>); %mapping = ( "\"" => "\\\"", "\n" => "\\n", "\r" => "\\r", "\t" => "\\t", "\\" => "\\\\" ); s/(.)/defined $mapping{$1} ? $mapping{$1} : ord($1) < 127 ? $1 : sprintf( "\\x%02x", ord($1) )/ges; s/\\r\\n/\\r\\n"\n        "/g; s/(END:VCARD\\r\\n")\n\s*"\\r\\n"/$1,\n/g; print "        \"$_\"\n";'
    
    const char *testcases[] = {
        "BEGIN:VCARD\r\n"
        "VERSION:3.0\r\n"
        "URL:\r\n"
        "TITLE:\r\n"
        "ROLE:\r\n"
        "X-EVOLUTION-MANAGER:\r\n"
        "X-EVOLUTION-ASSISTANT:\r\n"
        "NICKNAME:user8\r\n"
        "X-EVOLUTION-SPOUSE:\r\n"
        "NOTE:Here are some special characters: comma \\, colon : semicolon \\; backsl\r\n"
        " ash \\\\\r\n"
        "FN:special characters\r\n"
        "N:characters;special;;;\r\n"
        "X-EVOLUTION-FILE-AS:characters\\, special\r\n"
        "X-EVOLUTION-BLOG-URL:\r\n"
        "CALURI:\r\n"
        "FBURL:\r\n"
        "X-EVOLUTION-VIDEO-URL:\r\n"
        "X-MOZILLA-HTML:FALSE\r\n"
        "UID:pas-id-43C15E84000001AC\r\n"
        "END:VCARD\r\n",

        "BEGIN:VCARD\r\n"
        "VERSION:3.0\r\n"
        "URL:\r\n"
        "TITLE:\r\n"
        "ROLE:\r\n"
        "X-EVOLUTION-MANAGER:\r\n"
        "X-EVOLUTION-ASSISTANT:\r\n"
        "NICKNAME:user7\r\n"
        "X-EVOLUTION-SPOUSE:\r\n"
        "NOTE:This test case uses line breaks. This is line 1.\\nLine 2.\\n\\nLine brea\r\n"
        " ks in vcard 2.1 are encoded as =0D=0A.\\nThat means the = has to be encod\r\n"
        " ed itself...\r\n"
        "FN:line breaks\r\n"
        "N:breaks;line;;;\r\n"
        "X-EVOLUTION-FILE-AS:breaks\\, line\r\n"
        "X-EVOLUTION-BLOG-URL:\r\n"
        "CALURI:\r\n"
        "FBURL:\r\n"
        "X-EVOLUTION-VIDEO-URL:\r\n"
        "X-MOZILLA-HTML:FALSE\r\n"
        "ADR;TYPE=HOME:;Address Line 2\\nAddress Line 3;Address Line 1;;;;\r\n"
        "LABEL;TYPE=HOME:Address Line 1\\nAddress Line 2\\nAddress Line 3\r\n"
        "UID:pas-id-43C15DFB000001AB\r\n"
        "END:VCARD\r\n",

        "BEGIN:VCARD\r\n"
        "VERSION:3.0\r\n"
        "URL:\r\n"
        "TITLE:\r\n"
        "ROLE:\r\n"
        "X-EVOLUTION-MANAGER:\r\n"
        "X-EVOLUTION-ASSISTANT:\r\n"
        "NICKNAME:user6\r\n"
        "X-EVOLUTION-SPOUSE:\r\n"
        "NOTE:The middle name is \"middle \\; special \\;\".\r\n"
        "FN:Mr. First middle \\; special \\; last\r\n"
        "N:last;First;middle \\; special \\;;Mr.;\r\n"
        "X-EVOLUTION-FILE-AS:last\\, First\r\n"
        "X-EVOLUTION-BLOG-URL:\r\n"
        "CALURI:\r\n"
        "FBURL:\r\n"
        "X-EVOLUTION-VIDEO-URL:\r\n"
        "X-MOZILLA-HTML:FALSE\r\n"
        "UID:pas-id-43C15D55000001AA\r\n"
        "END:VCARD\r\n",

        "BEGIN:VCARD\r\n"
        "VERSION:3.0\r\n"
        "URL:\r\n"
        "TITLE:\r\n"
        "ROLE:\r\n"
        "X-EVOLUTION-MANAGER:\r\n"
        "X-EVOLUTION-ASSISTANT:\r\n"
        "NICKNAME:user5\r\n"
        "X-EVOLUTION-SPOUSE:\r\n"
        "NOTE:image in JPG format\r\n"
        "FN:Ms. JPG\r\n"
        "N:;JPG;;Ms.;\r\n"
        "X-EVOLUTION-FILE-AS:JPG\r\n"
        "X-EVOLUTION-BLOG-URL:\r\n"
        "CALURI:\r\n"
        "FBURL:\r\n"
        "X-EVOLUTION-VIDEO-URL:\r\n"
        "X-MOZILLA-HTML:FALSE\r\n"
        "PHOTO;ENCODING=b;TYPE=JPEG:/9j/4AAQSkZJRgABAQEASABIAAD/4QAWRXhpZgAATU0AKgAA\r\n"
        " AAgAAAAAAAD//gAXQ3JlYXRlZCB3aXRoIFRoZSBHSU1Q/9sAQwAFAwQEBAMFBAQEBQUFBgcM\r\n"
        " CAcHBwcPCwsJDBEPEhIRDxERExYcFxMUGhURERghGBodHR8fHxMXIiQiHiQcHh8e/9sAQwEF\r\n"
        " BQUHBgcOCAgOHhQRFB4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4e\r\n"
        " Hh4eHh4eHh4e/8AAEQgAFwAkAwEiAAIRAQMRAf/EABkAAQADAQEAAAAAAAAAAAAAAAAGBwgE\r\n"
        " Bf/EADIQAAECBQMCAwQLAAAAAAAAAAECBAADBQYRBxIhEzEUFSIIFjNBGCRHUVZ3lqXD0+P/\r\n"
        " xAAUAQEAAAAAAAAAAAAAAAAAAAAA/8QAFBEBAAAAAAAAAAAAAAAAAAAAAP/aAAwDAQACEQMR\r\n"
        " AD8AuX6UehP45/aXv9MTPTLVKxNSvMPcqu+a+XdLxf1SfJ6fU37PioTnOxfbOMc/KIZ7U/2V\r\n"
        " fmTR/wCaKlu6+blu/Ui72zxWtUmmUOrTaWwkWDT09FPR4K587OVrUfVsIwElPPPAbAjxr2um\r\n"
        " hWXbDu5rmfeApLPZ4hx0lzNm9aUJ9KAVHKlJHAPf7ozPLqWt9y6Z0EPGmoLNjTq48a1iaybJ\r\n"
        " YV52yEtCms5KJmAT61JXtJyUdyQTEc1WlMql7N1/oZ6jagVZVFfUyZPpFy5lvWcxU7Z03BUk\r\n"
        " GZLWJqVhPYLkIIPBEBtSEUyNAsjI1q1m/VP+UICwL/sqlXp7v+aOHsnyGttq218MtKd8+Ru2\r\n"
        " JXuScoO45Awe2CIi96aKW1cVyubkYVy6rTqz0J8a5t2qqZl0UjAMwYKScfPAJ+cIQHHP0Dth\r\n"
        " VFaMWt0XwxetnM50Ks2rsxL6ZMnJlJmb5hBBBEiVxjA28dznqo+hdksbQuS3Hs6tVtNzdM1Z\r\n"
        " /VH5nO3Bl/CJmYHKDynjv3zCEB5rLQNo0bIbydWNWxKljbLQLoWkISOAkBKAABCEID//2Q==\r\n"
        "UID:pas-id-43C0F0B500000005\r\n"
        "END:VCARD\r\n",

        "BEGIN:VCARD\r\n"
        "VERSION:3.0\r\n"
        "URL:\r\n"
        "TITLE:\r\n"
        "ROLE:\r\n"
        "X-EVOLUTION-MANAGER:\r\n"
        "X-EVOLUTION-ASSISTANT:\r\n"
        "NICKNAME:user4\r\n"
        "X-EVOLUTION-SPOUSE:\r\n"
        "NOTE:image in PNG format\r\n"
        "FN:Mrs. PNG\r\n"
        "N:;PNG;;Mrs.;\r\n"
        "X-EVOLUTION-FILE-AS:PNG\r\n"
        "X-EVOLUTION-BLOG-URL:\r\n"
        "CALURI:\r\n"
        "FBURL:\r\n"
        "X-EVOLUTION-VIDEO-URL:\r\n"
        "X-MOZILLA-HTML:FALSE\r\n"
        "PHOTO;ENCODING=b;TYPE=PNG:iVBORw0KGgoAAAANSUhEUgAAACQAAAAXCAYAAABj7u2bAAAAB\r\n"
        " mJLR0QA/wD/AP+gvaeTAAAACXBIWXMAAAsTAAALEwEAmpwYAAAAB3RJTUUH1gEICjgdiWkBO\r\n"
        " QAAAB10RVh0Q29tbWVudABDcmVhdGVkIHdpdGggVGhlIEdJTVDvZCVuAAABaElEQVRIx+3Wu\r\n"
        " 0tcURAG8F98gRKTYGORRqwksJV/QOqFFIFgKgsRYbHV1larDQQCKQxpUscyhUmXJuCSNpYWP\r\n"
        " sAU6wPxHW6aWbgsu+ve3RUs7geHc+fON3O+M4c5HHLkyHG/eISkg5heIGmUr++hVWigyY6TH\r\n"
        " lejbWSt0Bv8QBXX2MF7jKU4IyjjJ45xg31sYKZuw7Xv9Gh6vvXO9QbBtbGNJ8Ert+AlTURkF\r\n"
        " jQX9g5e4ykGUcBm+FaDexx2MUQOYhIL2Lpj09oV9CvsQgPuePj+hP037BL6M6yRSdDZHWVOc\r\n"
        " BHcEv7FvyN8xxqmeynovA1Baf4UVvANhyn/Uq8E/Q57ssNufhvx1QZrDHfS9p9i3sQsnscdN\r\n"
        " owXWEQlOBXMYyI4j3EavqFUzpOYl4OTqUJ9+NzmkbXyb6Ryfumm7Wso4it2cYXL6K6PeBmcV\r\n"
        " 8E5iEvxPDjv8CyVaxQfsIfbqGIlf17k6Bb/Ae0cnahfg6KuAAAAAElFTkSuQmCC\r\n"
        "UID:pas-id-43C0F07900000004\r\n"
        "END:VCARD\r\n",

        "BEGIN:VCARD\r\n"
        "VERSION:3.0\r\n"
        "URL:\r\n"
        "TITLE:\r\n"
        "ROLE:\r\n"
        "X-EVOLUTION-MANAGER:\r\n"
        "X-EVOLUTION-ASSISTANT:\r\n"
        "NICKNAME:user3\r\n"
        "X-EVOLUTION-SPOUSE:\r\n"
        "NOTE:image in GIF format\r\n"
        "FN:Mr. GIF\r\n"
        "N:;GIF;;Mr.;\r\n"
        "X-EVOLUTION-FILE-AS:GIF\r\n"
        "X-EVOLUTION-BLOG-URL:\r\n"
        "CALURI:\r\n"
        "FBURL:\r\n"
        "X-EVOLUTION-VIDEO-URL:\r\n"
        "X-MOZILLA-HTML:FALSE\r\n"
        "PHOTO;ENCODING=b;TYPE=GIF:R0lGODlhJAAXAIABAAAAAP///yH+FUNyZWF0ZWQgd2l0aCBUa\r\n"
        " GUgR0lNUAAh+QQBCgABACwAAAAAJAAXAAACVYyPqcvtD6OctNqLFdi8b/sd3giAJRNmqXaKH\r\n"
        " TIaZJKSpx3McLtyeSuTAWm34e+4WBGFuJ/P1QjZek9ksjiRGqFCTW5pZblmzdiO+GJWncqM+\r\n"
        " w2PwwsAOw==\r\n"
        "UID:pas-id-43C0F04B00000003\r\n"
        "END:VCARD\r\n",

        "BEGIN:VCARD\r\n"
        "VERSION:3.0\r\n"
        "URL:\r\n"
        "TITLE:\r\n"
        "ROLE:\r\n"
        "X-EVOLUTION-MANAGER:\r\n"
        "X-EVOLUTION-ASSISTANT:\r\n"
        "NICKNAME:user2\r\n"
        "X-EVOLUTION-SPOUSE:\r\n"
        "NOTE:This user tests some of the advanced aspects of vcards:\\n- non-ASCII c\r\n"
        " haracters (with umlauts in the name)\\n- line break (in this note and the\r\n"
        "  mailing address)\\n- long lines (in this note)\\n- special characters (in\r\n"
        "  this note)\\n- tabs (in this note)\\n\\nVery long line\\, very very long th\r\n"
        " is time... still not finished... blah blah blah blah blah 1 2 3 4 5 6 7 \r\n"
        " 8 9 10 11 12 13 14 15 16\\n\\ncomma \\,\\ncollon :\\nsemicolon \\;\\nbackslash \r\n"
        " \\\\\\n\\nThe same\\, in the middle of a line:\\ncomma \\, comma\\ncollon : coll\r\n"
        " on\\nsemicolon \\; semicolon\\nbackslash \\\\ backslash\\n\\nA tab \ttab done\\n\t\r\n"
        " line starts with tab\\n\r\n"
        "FN:Umlaut \xc3\x84 \xc3\x96 \xc3\x9c \xc3\x9f\r\n"
        "N:\xc3\x9c;\xc3\x84;\xc3\x96;Umlaut;\xc3\x9f\r\n"
        "X-EVOLUTION-FILE-AS:\xc3\x9c\\, \xc3\x84\r\n"
        "CATEGORIES:Business\r\n"
        "X-EVOLUTION-BLOG-URL:\r\n"
        "CALURI:\r\n"
        "FBURL:\r\n"
        "X-EVOLUTION-VIDEO-URL:\r\n"
        "X-MOZILLA-HTML:FALSE\r\n"
        "ADR;TYPE=HOME:test 5;Line 2\\n;Line 1;test 1;test 3;test 2;test 4\r\n"
        "LABEL;TYPE=HOME:Line 1\\nLine 2\\n\\ntest 1\\, test 3\\ntest 2\\ntest 5\\ntest 4\r\n"
        "UID:pas-id-43C0EF0A00000002\r\n"
        "END:VCARD\r\n",

        "BEGIN:VCARD\r\n"
        "VERSION:3.0\r\n"
        "URL:http://john.doe.com\r\n"
        "TITLE:Senior Tester\r\n"
        "ORG:Test Inc.;Testing;test#1\r\n"
        "ROLE:professional test case\r\n"
        "X-EVOLUTION-MANAGER:John Doe Senior\r\n"
        "X-EVOLUTION-ASSISTANT:John Doe Junior\r\n"
        "NICKNAME:user1\r\n"
        "BDAY:2006-01-08\r\n"
        "X-EVOLUTION-ANNIVERSARY:2006-01-09\r\n"
        "X-EVOLUTION-SPOUSE:Joan Doe\r\n"
        "NOTE:This is a test case which uses almost all Evolution fields.\r\n"
        "FN:John Doe\r\n"
        "N:Doe;John;;;\r\n"
        "X-EVOLUTION-FILE-AS:Doe\\, John\r\n"
        "CATEGORIES:TEST\r\n"
        "X-EVOLUTION-BLOG-URL:web log\r\n"
        "CALURI:calender\r\n"
        "FBURL:free/busy\r\n"
        "X-EVOLUTION-VIDEO-URL:chat\r\n"
        "X-MOZILLA-HTML:FALSE\r\n"
        "ADR;TYPE=WORK:Test Box #2;;Test Drive 2;Test Town;Upper Test County;12346;O\r\n"
        " ld Testovia\r\n"
        "LABEL;TYPE=WORK:Test Drive 2\\nTest Town\\, Upper Test County\\n12346\\nTest Bo\r\n"
        " x #2\\nOld Testovia\r\n"
        "ADR;TYPE=HOME:Test Box #1;;Test Drive 1;Test Village;Lower Test County;1234\r\n"
        " 5;Testovia\r\n"
        "LABEL;TYPE=HOME:Test Drive 1\\nTest Village\\, Lower Test County\\n12345\\nTest\r\n"
        "  Box #1\\nTestovia\r\n"
        "ADR;TYPE=OTHER:Test Box #3;;Test Drive 3;Test Megacity;Test County;12347;Ne\r\n"
        " w Testonia\r\n"
        "LABEL;TYPE=OTHER:Test Drive 3\\nTest Megacity\\, Test County\\n12347\\nTest Box\r\n"
        "  #3\\nNew Testonia\r\n"
        "UID:pas-id-43C0ED3900000001\r\n"
        "EMAIL;TYPE=WORK;X-EVOLUTION-UI-SLOT=1:john.doe@work.com\r\n"
        "EMAIL;TYPE=HOME;X-EVOLUTION-UI-SLOT=2:john.doe@home.priv\r\n"
        "TEL;TYPE=WORK;TYPE=VOICE;X-EVOLUTION-UI-SLOT=1:business 1\r\n"
        "TEL;TYPE=HOME;TYPE=VOICE;X-EVOLUTION-UI-SLOT=2:home 2\r\n"
        "TEL;TYPE=CELL;X-EVOLUTION-UI-SLOT=3:mobile 3\r\n"
        "TEL;TYPE=WORK;TYPE=FAX;X-EVOLUTION-UI-SLOT=4:businessfax 4\r\n"
        "TEL;TYPE=HOME;TYPE=FAX;X-EVOLUTION-UI-SLOT=5:homefax 5\r\n"
        "TEL;TYPE=PAGER;X-EVOLUTION-UI-SLOT=6:pager 6\r\n"
        "TEL;TYPE=CAR;X-EVOLUTION-UI-SLOT=7:car 7\r\n"
        "TEL;TYPE=PREF;X-EVOLUTION-UI-SLOT=8:primary 8\r\n"
        "X-AIM;TYPE=HOME;X-EVOLUTION-UI-SLOT=1:AIM JOHN\r\n"
        "X-YAHOO;TYPE=HOME;X-EVOLUTION-UI-SLOT=2:YAHOO JDOE\r\n"
        "X-ICQ;TYPE=HOME;X-EVOLUTION-UI-SLOT=3:ICQ JD\r\n"
        "X-GROUPWISE;TYPE=HOME;X-EVOLUTION-UI-SLOT=4:GROUPWISE DOE\r\n"
        "END:VCARD"
    };
    const int numtestcases = sizeof(testcases) / sizeof(testcases[0]);
    int testcase;

    // clean server and first test database
    deleteAll( "testcases", 0);
    
    EvolutionContactSource source(
        string( "dummy" ),
        m_changeIds[0],
        m_contactNames[0] );

    // insert test cases
    setLogFile( "testcases.insert.log", TRUE );
    EVOLUTION_ASSERT_NO_THROW( source, source.open() );
    EVOLUTION_ASSERT( source, source.beginSync() == 0 );
    int numItems;
    CPPUNIT_ASSERT( !countItems( source ) );
    for (testcase = 0; testcase < numtestcases; testcase++ ) {
        SyncItem item;
        item.setData( testcases[testcase], strlen(testcases[testcase]) + 1 );
        EVOLUTION_ASSERT_NO_THROW( source, source.addItem( item ) );
        CPPUNIT_ASSERT( item.getKey() != NULL );
        CPPUNIT_ASSERT( strlen( item.getKey() ) > 0 );
    }
    EVOLUTION_ASSERT_NO_THROW( source, source.close() );

    // transfer back and forth
    doSync( "testcases.send.client.log", 0, SYNC_TWO_WAY );
    doSync( "testcases.recv.client.log", 1, SYNC_REFRESH_FROM_SERVER );

    compareAddressbooks( numtestcases );
}
