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

#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>
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
    CPPUNIT_TEST( testContactImport );

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
                        "TEL;TYPE=WORK;TYPE=VOICE;X-EVOLUTION-UI-SLOT=1:business 1\n"
                        "TEL;TYPE=WORK;TYPE=VOICE;X-EVOLUTION-UI-SLOT=2:business 2\n"
                        "X-EVOLUTION-BLOG-URL:\n"
                        "BDAY:2006-01-08\n"
                        "X-EVOLUTION-VIDEO-URL:\n"
                        "X-MOZILLA-HTML:TRUE\n"
                        "END:VCARD\n"
        );

    /** performs one sync operation */
    void doSync(const string &logfile, int config, SyncMode syncMode);

    /** deletes all contacts locally via EvolutionContactSource */
    void contactDeleteAll(int config);

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

    /** compare all entries in the two address books and assert that they are equal */
    void compareAddressbooks(const string &prefix, const char *refVCard = NULL);

public:
    void setUp() {
        m_contactNames[0] = "SyncEvolution test #1";
        m_contactNames[1] = "SyncEvolution test #2";
        m_syncConfigs[0] = "localhost_1";
        m_syncConfigs[1] = "localhost_2";
        m_changeIds[0] = "SyncEvolution Change ID #0";
        m_changeIds[1] = "SyncEvolution Change ID #1";
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
    // clean database, import file, then export again and compare
    void testContactImport();

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
        "TEL;TYPE=WORK;TYPE=VOICE;X-EVOLUTION-UI-SLOT=1:business 1\n"
        "X-EVOLUTION-FILE-AS:Doe\\, John\n"
        "X-EVOLUTION-BLOG-URL:\n"
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

void TestEvolution::testContactImport()
{
    testContactDeleteAll();
    
    EvolutionContactSource source(
        string( "dummy" ),
        m_changeIds[0],
        m_contactNames[0] );

    // insert test cases
    setLogFile( "testVCard.insert.log", TRUE );
    EVOLUTION_ASSERT_NO_THROW( source, source.open() );
    EVOLUTION_ASSERT( source, source.beginSync() == 0 );
    int numItems;
    CPPUNIT_ASSERT( !countItems( source ) );
    
    // import the .vcf file
    ifstream input;
    input.open("testVCard.vcf");
    CPPUNIT_ASSERT(!input.bad());
    string vcard, line;
    const string endvcard("END:VCARD");
    while (!input.eof()) {
        do {
            getline(input, line);
            CPPUNIT_ASSERT(!input.bad());
            if (line != "\r" ) {
                vcard += line;
                vcard += "\n";
            }
            if (!line.compare(0, endvcard.size(), endvcard)) {
                SyncItem item;
                item.setData( vcard.c_str(), vcard.size() + 1 );
                item.setDataType( "raw" );
                EVOLUTION_ASSERT_NO_THROW( source, source.addItem( item ) );
                CPPUNIT_ASSERT( item.getKey() != NULL );
                CPPUNIT_ASSERT( strlen( item.getKey() ) > 0 );

                vcard = "";
            }
        } while(!input.eof());
    }

    // verify that importing/exporting did not already modify cards
    ofstream out("testVCard.export.test.vcf");
    source.exportData(out);
    out.close();
    CPPUNIT_ASSERT( !system("./normalize_vcard testVCard.vcf testVCard.export.test.vcf") );
    
    EVOLUTION_ASSERT_NO_THROW( source, source.close() );
}

void TestEvolution::doSync(const string &logfile, int config, SyncMode syncMode)
{
    int res = 0;

    // use LOG_LEVEL_INFO to avoid extra debug output outside of
    // EvolutionSyncClient::sync() which will set the level to DEBUG
    // automatically
    remove( logfile.c_str() );
    setLogFile( logfile.c_str(), TRUE );
    LOG.setLevel(LOG_LEVEL_INFO);
    {
        EvolutionSyncClient client(m_syncConfigs[config]);
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
        } else {
            perror( m_serverLog.c_str() );
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

void TestEvolution::deleteAll( const string &prefix, int config, DeleteAllMode mode )
{
    switch (mode) {
     case DELETE_ALL_SYNC:
        // refresh (in case something is missing locally), then delete
        doSync( prefix + ".deleteall.refresh.client.log", config, SYNC_REFRESH_FROM_SERVER );
        testContactDeleteAll();
        doSync( prefix + ".deleteall.twoway.client.log", config, SYNC_TWO_WAY );
        break;
     case DELETE_ALL_REFRESH:
        // delete locally
        testContactDeleteAll();
        // refresh server
        doSync( prefix + ".deleteall.refreshserver.client.log", config, SYNC_REFRESH_FROM_CLIENT );
        break;
    }
}

void TestEvolution::testDeleteAll()
{
    EvolutionContactSource source( string( "dummy" ),
                                   m_changeIds[1],
                                   m_contactNames[0] );

    // copy something to server first
    testContactSimpleInsert();
    doSync( "testDeleteAll.insert.1.client.log", 0, SYNC_SLOW );

    deleteAll( "testDeleteAllSync", 0, DELETE_ALL_SYNC );
    
    // nothing stored locally?
    EVOLUTION_ASSERT_NO_THROW( source, source.open() );
    EVOLUTION_ASSERT( source, source.beginSync() == 0 );
    CPPUNIT_ASSERT( countItems( source ) == 0 );
    EVOLUTION_ASSERT_NO_THROW( source, source.close() );

    // make sure server really deleted everything
    doSync( "testDeleteAll.check.1.client.log", 0, SYNC_REFRESH_FROM_SERVER );
    EVOLUTION_ASSERT_NO_THROW( source, source.open() );
    EVOLUTION_ASSERT( source, source.beginSync() == 0 );
    CPPUNIT_ASSERT( countItems( source ) == 0 );
    EVOLUTION_ASSERT_NO_THROW( source, source.close() );    

    // copy something to server again
    testContactSimpleInsert();
    doSync( "testDeleteAll.insert.2.client.log", 0, SYNC_SLOW );

    // now try deleting using another sync method
    deleteAll( "testDeleteAllRefresh", 0, DELETE_ALL_REFRESH );
    
    // nothing stored locally?
    EVOLUTION_ASSERT_NO_THROW( source, source.open() );
    EVOLUTION_ASSERT( source, source.beginSync() == 0 );
    CPPUNIT_ASSERT( countItems( source ) == 0 );
    EVOLUTION_ASSERT_NO_THROW( source, source.close() );

    // make sure server really deleted everything
    doSync( "testDeleteAll.check.2.client.log", 0, SYNC_REFRESH_FROM_SERVER );
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

    EvolutionContactSource copy( string( "dummy" ),
                                   m_changeIds[0],
                                   m_contactNames[1] );
    EVOLUTION_ASSERT_NO_THROW( copy, copy.open() );
    EVOLUTION_ASSERT( copy, copy.beginSync() == 0 );
    CPPUNIT_ASSERT( countItems( copy ) == 1 );
}

void TestEvolution::testCopy()
{
    doCopy( "testCopy" );
    compareAddressbooks("testCopy");
}

void TestEvolution::testUpdate()
{
    doCopy( "testUpdate.copy" );
    contactUpdate();

    doSync( "testUpdate.update.0.client.log", 0, SYNC_TWO_WAY );
    doSync( "testUpdate.update.1.client.log", 1, SYNC_TWO_WAY );

    compareAddressbooks("testUpdate");
}

void TestEvolution::testDelete()
{
    doCopy( "testDelete.copy" );
    testContactDeleteAll();
    doSync( "testDelete.delete.0.client.log", 0, SYNC_TWO_WAY );
    doSync( "testDelete.delete.1.client.log", 1, SYNC_TWO_WAY );
    
    EvolutionContactSource copy( string( "dummy" ),
                                   m_changeIds[1],
                                   m_contactNames[1] );
    EVOLUTION_ASSERT_NO_THROW( copy, copy.open() );
    EVOLUTION_ASSERT( copy, copy.beginSync() == 0 );
    CPPUNIT_ASSERT( countItems( copy ) == 0 );
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
                   "X-EVOLUTION-VIDEO-URL:\n"
                   "X-MOZILLA-HTML:FALSE\n"
                   "TEL;TYPE=WORK:business 1\n"
                   "END:VCARD\n" );
    // add a birthday, modify the title and X-MOZILLA-HTML
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
                   "X-EVOLUTION-VIDEO-URL:\n"
                   "X-MOZILLA-HTML:TRUE\n"
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

static string tostring( const char *str )
{
    return string( str ? str : "" );
}
            
static string Address2String( EContactAddress *addr )
{
    return !addr ? string() :
        string() +
        "format: " + tostring( addr->address_format ) + "\n" +
        "po: " + tostring( addr->po ) + "\n" +
        "ext: " + tostring( addr->ext ) + "\n" +
        "street: " + tostring( addr->street ) + "\n" +
        "locality: " + tostring( addr->locality ) + "\n" +
        "region: " + tostring( addr->region ) + "\n" +
        "code: " + tostring( addr->code ) + "\n" +
        "country: " + tostring( addr->country ) + "\n";
}

static string Name2String( EContactName *name )
{
    return !name ? string() :
        string() +
        "family: " + tostring( name->family ) + "\n" +
        "given: " + tostring( name->given ) + "\n" +
        "additional: " + tostring( name->additional ) + "\n" +
        "prefixes: " + tostring( name->prefixes ) + "\n" +
        "suffixes: " + tostring( name->suffixes ) + "\n";
}

static string Date2String( EContactDate *date )
{
    string res;
    if (date) {
        char *date_cstr = e_contact_date_to_string( date );
        res = date_cstr;
        free( date_cstr );
    }
    return res;
}

static string Photo2String( EContactPhoto *photo )
{
    stringstream res;
    if (photo) {
        res << "length: " << photo->length << "\n";
        if (photo->length) {
            res << "first/last byte: " << hex << showbase << (unsigned int)(unsigned char)photo->data[0]
                << "..." << (unsigned int)(unsigned char)photo->data[photo->length - 1] << "\n";
        }
    }
    return res.str();
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

/**
 * takes two address books, exports them as vcards,
 * then compares them using normalize_vcards.pl
 * and shell commands
 *
 * @param refVCard      existing file with existing reference vcards (optional)
 */
void TestEvolution::compareAddressbooks(const string &prefix, const char *refVCard)
{
    string sourceVCard, copyVCard;
    if (refVCard) {
        sourceVCard = refVCard;
    } else {
        sourceVCard = prefix + ".source..test.vcf";

        EvolutionContactSource source(
            string( "dummy" ),
            m_changeIds[0],
            m_contactNames[0] );
        EVOLUTION_ASSERT_NO_THROW( source, source.open() );
        EVOLUTION_ASSERT( source, source.beginSync() == 0 );

        ofstream osource(sourceVCard.c_str());
        source.exportData(osource);
        osource.close();
        CPPUNIT_ASSERT(!osource.bad());
    }

    copyVCard = prefix + ".copy.test.vcf";
    EvolutionContactSource copy(
        string( "dummy" ),
        m_changeIds[1],
        m_contactNames[1] );
    EVOLUTION_ASSERT_NO_THROW( copy, copy.open() );
    EVOLUTION_ASSERT( copy, copy.beginSync() == 0 );

    ofstream ocopy(copyVCard.c_str());
    copy.exportData(ocopy);
    ocopy.close();
    CPPUNIT_ASSERT(!ocopy.bad());

    stringstream cmd;

    string diff = prefix + ".diff";
    cmd << "perl normalize_vcard.pl " << sourceVCard << " " << copyVCard << ">" << diff;
    cmd << "  || (echo; echo '*** " << diff << " non-empty ***'; cat " << diff << "; exit 1 )";

    string cmdstr = cmd.str();
    if (system(cmdstr.c_str())) {
        CPPUNIT_ASSERT(((void)"address books identical", false));
    }
}

void TestEvolution::testVCard()
{
    // clean server and first test database
    deleteAll( "testVCard", 0);

    // import data
    testContactImport();

    // transfer back and forth
    doSync( "testVCard.send.client.log", 0, SYNC_TWO_WAY );
    doSync( "testVCard.recv.client.log", 1, SYNC_REFRESH_FROM_SERVER );

    compareAddressbooks("testVCard", "testVCard.vcf");
}
