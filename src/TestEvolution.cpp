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
    return countAnyItems(
        source,
        &EvolutionSyncSource::getFirstNewItem,
        &EvolutionSyncSource::getNextNewItem );
}

static int countUpdatedItems( EvolutionSyncSource &source )
{
    return countAnyItems(
        source,
        &EvolutionSyncSource::getFirstUpdatedItem,
        &EvolutionSyncSource::getNextUpdatedItem );
}

static int countDeletedItems( EvolutionSyncSource &source )
{
    return countAnyItems(
        source,
        &EvolutionSyncSource::getFirstDeletedItem,
        &EvolutionSyncSource::getNextDeletedItem );
}

static int countItems( EvolutionSyncSource &source )
{
    return countAnyItems(
        source,
        &EvolutionSyncSource::getFirstItem,
        &EvolutionSyncSource::getNextItem );
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
    void contactUpdate();

    /** performs one sync operation */
    void doSync(const string &logfile, int config, SyncMode syncMode);

    /** deletes all contacts */
    void contactDeleteAll(int config);

    /** deletes all items locally and on server */
    void deleteAll( const string &prefix, int config );

    /** create item in one database, then copy to the other */
    void doCopy( const string &prefix );

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
        "TITLE:\n"
        "ROLE:\n"
        "X-EVOLUTION-MANAGER:\n"
        "X-EVOLUTION-ASSISTANT:\n"
        "NICKNAME:johnny\n"
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
        "EMAIL;TYPE=WORK;X-EVOLUTION-UI-SLOT=1:john.doe@neverland.org\n"
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

void TestEvolution::contactUpdate()
{
    EvolutionContactSource source( string( "dummy" ),
                                   m_changeIds[0],
                                   m_contactNames[0] );
    EVOLUTION_ASSERT_NO_THROW( source, source.open() );
    EVOLUTION_ASSERT( source, source.beginSync() == 0 );
    SyncItem *item;
    EVOLUTION_ASSERT_NO_THROW( source, item = source.getFirstItem() );
    const char *vcard =
        "BEGIN:VCARD\n"
        "VERSION:3.0\n"
        "URL:\n"
        "TITLE:\n"
        "ROLE:\n"
        "X-EVOLUTION-MANAGER:\n"
        "X-EVOLUTION-ASSISTANT:\n"
        "NICKNAME:johnny\n"
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
        "EMAIL;TYPE=WORK;X-EVOLUTION-UI-SLOT=1:john.doe@everywhere.com\n"
        "END:VCARD\n";
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
    remove( logfile.c_str() );
    setLogFile( logfile.c_str() );
    {
        EvolutionSyncClient client( m_syncConfigs[config] );
        CPPUNIT_ASSERT_NO_THROW( client.sync(syncMode) );
    }
    setLogFile( "sync.log" );
    
    // let the server finish
    sleep(10);

    // make a copy of the server's log (if found), then truncate it
    if (m_serverLog.size()) {
        int fd = open( m_serverLog.c_str(), O_RDWR );

        if (fd >= 0) {
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
    }
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
}

void TestEvolution::testUpdate()
{
    doCopy( "testUpdate.copy" );
    contactUpdate();

    doSync( "testUpdate.update.0.client.log", 0, SYNC_TWO_WAY );
    doSync( "testUpdate.update.1.client.log", 1, SYNC_TWO_WAY );

    // TODO: check that the second database contains the updated contact
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
