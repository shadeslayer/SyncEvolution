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

#include <memory>
using namespace std;

#include "EvolutionContactSource.h"

class EvolutionContactItem : public SyncItem
{
public:
    EvolutionContactItem( EContact *contact );
};

EvolutionContactItem::EvolutionContactItem( EContact *contact )
{
}

EvolutionContactSource::EvolutionContactSource( const string &name,
                                                const string &changeId,
                                                const string &id,
                                                bool idIsName ) :
    EvolutionSyncSource( name, changeId, id, idIsName ),
    m_vcardFormat( EVC_FORMAT_VCARD_30 )
{
}

EvolutionContactSource::~EvolutionContactSource()
{
    close();
}

EvolutionSyncSource::sources EvolutionContactSource::getSyncBackends()
{
    ESourceList *sources = NULL;

    if (!e_book_get_addressbooks(&sources, NULL)) {
        throw "unable to access address books";
    }

    EvolutionSyncSource::sources result;

    for (GSList *g = e_source_list_peek_groups (sources); g; g = g->next) {
        ESourceGroup *group = E_SOURCE_GROUP (g->data);
        for (GSList *s = e_source_group_peek_sources (group); s; s = s->next) {
            ESource *source = E_SOURCE (s->data);
            result.push_back( EvolutionSyncSource::source( e_source_peek_name(source),
                                                           e_source_get_uri(source) ) );
        }
    }
    return result;
}

void EvolutionContactSource::open()
{
    ESourceList *sources;
    if (!e_book_get_addressbooks(&sources, NULL)) {
        throw "unable to access address books";
    }
    
    ESource *source = findSource( sources, m_id, m_idIsName );
    if (!source) {
        throw string( "no such address book: " ) + m_id;
    }

    GError *gerror = NULL;
    m_addressbook.set( e_book_new( source, &gerror ), "address book" );

    if (!e_book_open( m_addressbook, TRUE, &gerror) ) {
        throwError( "opening address book", gerror );
    }

    // find all items
    gptr<EBookQuery> allItemsQuery( e_book_query_any_field_contains(""), "query" );
    GList *nextItem;
    if (!e_book_get_contacts( m_addressbook, allItemsQuery, &nextItem, &gerror )) {
        throwError( "reading all items", gerror );
    }
    while (nextItem) {
        m_allItems.push_back( (const char *)e_contact_get_const(E_CONTACT(nextItem->data),
                                                                E_CONTACT_UID) );
        nextItem = nextItem->next;
    }
    allItemsQuery = NULL;

    // scan modified items since the last instantiation
    if (!e_book_get_changes( m_addressbook, (char *)m_changeId.c_str(), &nextItem, &gerror )) {
        throwError( "reading changes", gerror );
    } 
    while (nextItem) {
        EBookChange *ebc = (EBookChange *)nextItem->data;
        const char *uid = (const char *)e_contact_get_const( ebc->contact, E_CONTACT_UID );

        switch (ebc->change_type) {            
         case E_BOOK_CHANGE_CARD_ADDED:
            m_newItems.push_back( uid );
            break;
         case E_BOOK_CHANGE_CARD_MODIFIED:
            m_updatedItems.push_back( uid );
            break;
         case E_BOOK_CHANGE_CARD_DELETED:
            m_deletedItems.push_back( uid );
            break;
        }
        nextItem = nextItem->next;
    }
}

void EvolutionContactSource::close()
{
    if (m_addressbook) {
        GError *gerror = NULL;
        GList *nextItem;
        // move change_id forward so that our own changes are not listed the next time
        if (!e_book_get_changes( m_addressbook, (char *)m_changeId.c_str(), &nextItem, &gerror )) {
            throwError( "reading changes", gerror );
        }
    }
    m_addressbook = NULL;
    m_allItems.clear();
    m_newItems.clear();
    m_updatedItems.clear();
    m_deletedItems.clear();
}


SyncItem *EvolutionContactSource::createItem( const string &uid, SyncState state )
{
    // this function must never throw an exception
    // because it is called inside the Sync4j C++ API library
    // which cannot handle exceptions
    try {
        EContact *contact;
        GError *gerror = NULL;
        if (! e_book_get_contact( m_addressbook,
                                  uid.c_str(),
                                  &contact,
                                  &gerror ) ) {
            throwError( string( "reading contact" ) + uid,
                        gerror );
        }
        gptr<EContact, GObject> contactptr( contact, "contact" );
        gptr<char> vcardstr(e_vcard_to_string( &contactptr->parent,
                                               m_vcardFormat ) );
        if (!vcardstr) {
            throwError( string( "converting contact" ) + uid, NULL );
        }

        auto_ptr<SyncItem> item( new SyncItem( uid.c_str() ) );
        item->setData( vcardstr, strlen( vcardstr ) );
        item->setDataType( getMimeType() );
        item->setModificationTime( 0 );
        item->setState( state );

        return item.release();
    } catch (...) {
        m_hasFailed = true;
    }

    return NULL;
}

int EvolutionContactSource::addItem(SyncItem& item)
{
    try {
        string data = getData(item);
        gptr<EContact, GObject> contact(e_contact_new_from_vcard(data.c_str()));
        if( contact ) {
            GError *gerror = NULL;
            e_contact_set(contact, E_CONTACT_UID, NULL);
            if (e_book_add_contact(m_addressbook, contact, &gerror)) {
                item.setKey( (const char *)e_contact_get_const( contact, E_CONTACT_UID ) );
            } else {
                throwError( "storing new contact", gerror );
            }
        } else {
            throwError( string( "parsing vcard" ) + data,
                        NULL );
        }
    } catch ( ... ) {
        m_hasFailed = true;
    }
    return 0;
}

int EvolutionContactSource::updateItem(SyncItem& item)
{
    try {
        string data = getData(item);
        gptr<EContact, GObject> contact(e_contact_new_from_vcard(data.c_str()));
        if( contact ) {
            GError *gerror = NULL;
            e_contact_set( contact, E_CONTACT_UID, item.getKey() );
            if ( e_book_commit_contact(m_addressbook, contact, &gerror) ) {
                const char *uid = (const char *)e_contact_get_const(contact, E_CONTACT_UID);
                if (uid) {
                    item.setKey( uid );
                }
            } else {
                throwError( string( "updating contact" ) + item.getKey(), gerror );
            }
        } else {
            throwError( string( "parsing vcard" ) + data,
                        NULL );
        }
    } catch ( ... ) {
        m_hasFailed = true;
    }
    return 0;
}

int EvolutionContactSource::deleteItem(SyncItem& item)
{
    try {
        GError *gerror = NULL;
        if (!e_book_remove_contact( m_addressbook, item.getKey(), &gerror ) ) {
            throwError( string( "deleting contact" ) + item.getKey(),
                        gerror );
        }
    } catch( ... ) {
        m_hasFailed = true;
    }
    return 0;
}

const char *EvolutionContactSource::getMimeType()
{
    // todo: be more precise here
    switch( m_vcardFormat ) {
     case EVC_FORMAT_VCARD_21:
        return "text/vcard";
        break;
     case EVC_FORMAT_VCARD_30:
        return "text/vcard";
    }

    return "test/vcard";
}
