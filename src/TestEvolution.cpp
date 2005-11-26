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
#include <common/spds/SyncStatus.h>

#include <string.h>

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
    CPPUNIT_TEST_SUITE_END();

    /** the name of the contact database */
    string m_contactName;

    /** different change ids */
    string m_changeIds[2];

    /**
     * helper function, not a separate test:
     * assumes that one element is currently inserted and updates it
     */
    void contactUpdate();

public:
    void setUp() {
        m_contactName = "Sync4jEvolution Test Address Book";
        m_changeIds[0] = "Sync4jEvolution Change ID #0";
        m_changeIds[1] = "Sync4jEvolution Change ID #1";
    }
    void tearDown() {
    }

    void testContactOpen();
    void testContactSimpleInsert();
    void testContactDeleteAll();
    void testContactIterateTwice();
    void testContactComplexInsert();
    void testContactUpdate();
    void testContactChanges();
};

// Registers the fixture into the 'registry'
CPPUNIT_TEST_SUITE_REGISTRATION( TestEvolution );

void TestEvolution::testContactOpen()
{
    EvolutionContactSource source( string( "dummy" ),
                                   m_changeIds[0],
                                   m_contactName );

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
        m_contactName );
    
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

void TestEvolution::testContactDeleteAll()
{
    testContactSimpleInsert();
        
    EvolutionContactSource source( string( "dummy" ),
                                   m_changeIds[0],
                                   m_contactName );

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

void TestEvolution::testContactIterateTwice()
{
    EvolutionContactSource source( string( "dummy" ),
                                   m_changeIds[0],
                                   m_contactName );

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
                                   m_contactName );
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
                                   m_contactName );
        
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
