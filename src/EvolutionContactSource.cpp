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

#include <common/base/Log.h>

EvolutionContactSource::EvolutionContactSource( const string &name,
                                                const string &changeId,
                                                const string &id,
                                                EVCardFormat vcardFormat ) :
    EvolutionSyncSource( name, changeId, id ),
    m_vcardFormat( vcardFormat )
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
    
    ESource *source = findSource( sources, m_id );
    if (!source) {
        throw string(getName()) + ": no such address book: '" + m_id + "'";
    }

    GError *gerror = NULL;
    m_addressbook.set( e_book_new( source, &gerror ), "address book" );

    if (!e_book_open( m_addressbook, TRUE, &gerror) ) {
        throwError( "opening address book", gerror );
    }
}

int EvolutionContactSource::beginSync()
{
    m_isModified = false;
    try {
        GError *gerror = NULL;

        // find all items
        gptr<EBookQuery> allItemsQuery( e_book_query_any_field_contains(""), "query" );
        GList *nextItem;
        if (!e_book_get_contacts( m_addressbook, allItemsQuery, &nextItem, &gerror )) {
            throwError( "reading all items", gerror );
        }
        while (nextItem) {
            const char *uid = (const char *)e_contact_get_const(E_CONTACT(nextItem->data),
                                                                E_CONTACT_UID);
            logItem( string(uid), "existing item" );
            m_allItems.push_back(uid);
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
                logItem( string(uid), "was added" );
                m_newItems.push_back( uid );
                break;
             case E_BOOK_CHANGE_CARD_MODIFIED:
                logItem( string(uid), "was modified" );
                m_updatedItems.push_back( uid );
                break;
             case E_BOOK_CHANGE_CARD_DELETED:
                logItem( string(uid), "was deleted" );
                m_deletedItems.push_back( uid );
                break;
            }
            nextItem = nextItem->next;
        }
    } catch( ... ) {
        m_hasFailed = true;
        // TODO: properly set error
        return 1;
    }
    return 0;
}

int EvolutionContactSource::endSync()
{
    try {
        endSyncThrow();
    } catch ( ... ) {
        m_hasFailed = true;
        return 1;
    }
    return 0;
}

void EvolutionContactSource::endSyncThrow()
{
    LOG.info( m_isModified ? "EvolutionContactSource: address book was modified" : "EvolutionContactSource: no modifications" );
    if (m_isModified) {
        GError *gerror = NULL;
        GList *nextItem;
        // move change_id forward so that our own changes are not listed the next time
        if (!e_book_get_changes( m_addressbook, (char *)m_changeId.c_str(), &nextItem, &gerror )) {
            throwError( "reading changes", gerror );
        }
    }
    resetItems();
    m_isModified = false;
}

void EvolutionContactSource::close()
{
    endSyncThrow();
    m_addressbook = NULL;
}


SyncItem *EvolutionContactSource::createItem( const string &uid, SyncState state )
{
    // this function must never throw an exception
    // because it is called inside the Sync4j C++ API library
    // which cannot handle exceptions
    try {
        logItem( uid, "extracting from EV" );
        
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

        // hack: patch version so that Sync4j 2.3 accepts it
        char *ver = strstr(vcardstr, "VERSION:3.0" );
        if (ver) {
            ver[8] = '2';
            ver[10] = '1';
        }

        LOG.debug( vcardstr );
        auto_ptr<SyncItem> item( new SyncItem( uid.c_str() ) );
        item->setData( vcardstr, strlen( vcardstr ) + 1 );
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
        logItem( item, "adding" );

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

        m_isModified = true;
    } catch ( ... ) {
        m_hasFailed = true;
        return STC_COMMAND_FAILED;
    }
    return STC_OK;
}

int EvolutionContactSource::updateItem(SyncItem& item)
{
    try {
        logItem( item, "updating" );

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

        m_isModified = true;
    } catch ( ... ) {
        m_hasFailed = true;
        return STC_COMMAND_FAILED;
    }
    return STC_OK;
}

int EvolutionContactSource::deleteItem(SyncItem& item)
{
    try {
        logItem( item, "deleting" );

        GError *gerror = NULL;
        if (!e_book_remove_contact( m_addressbook, item.getKey(), &gerror ) ) {
            throwError( string( "deleting contact" ) + item.getKey(),
                        gerror );
        }

        m_isModified = true;
    } catch( ... ) {
        m_hasFailed = true;
        return STC_COMMAND_FAILED;
    }
    return STC_OK;
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

void EvolutionContactSource::logItem( const string &uid, const string &info )
{
    if (LOG.getLevel() >= LOG_LEVEL_INFO) {
        string line;
        EContact *contact;
        GError *gerror = NULL;

        if (e_book_get_contact( m_addressbook,
                                uid.c_str(),
                                &contact,
                                &gerror )) {
            const char *fileas = (const char *)e_contact_get_const( contact, E_CONTACT_FILE_AS );
            const char *name = (const char *)e_contact_get_const( contact, E_CONTACT_FULL_NAME );

            line += fileas ? fileas :
                name ? name :
                "<unnamed contact>";
        } else {
            line += "<unknown contact>";
        }
        line += " (";
        line += uid;
        line += "): ";
        line += info;
        
        LOG.info( line.c_str() );
    }
}

void EvolutionContactSource::logItem( SyncItem &item, const string &info )
{
    if (LOG.getLevel() >= LOG_LEVEL_INFO) {
        string line;
        string vcard( (const char *)item.getData(), item.getDataSize() );

        int offset = vcard.find( "FN:");
        if (offset != vcard.npos) {
            int len = vcard.find( "\r", offset ) - offset - 3;
            line += vcard.substr( offset + 3, len );
        } else {
            line += "<unnamed SyncItem>";
        }

        if (!item.getKey() ) {
            line += ", NULL UID (?!)";
        } else if (!strlen( item.getKey() )) {
            line += ", empty UID";
        } else {
            line += ", ";
            line += item.getKey();
        
            EContact *contact;
            GError *gerror = NULL;
            if (e_book_get_contact( m_addressbook,
                                    item.getKey(),
                                    &contact,
                                    &gerror )) {
                line += "EV ";
                const char *fileas = (const char *)e_contact_get_const( contact, E_CONTACT_FILE_AS );
                const char *name = (const char *)e_contact_get_const( contact, E_CONTACT_FULL_NAME );

                line += fileas ? fileas :
                    name ? name :
                    "<unnamed contact>";
            } else {
                line += ", not in Evolution";
            }
        }
        line += ": ";
        line += info;
        
        LOG.info( line.c_str() );
    }
}
