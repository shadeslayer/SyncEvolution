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
#include <common/vocl/VObject.h>
#include <common/vocl/VConverter.h>

const EvolutionContactSource::extensions EvolutionContactSource::m_vcardExtensions;
const EvolutionContactSource::unique EvolutionContactSource::m_uniqueProperties;

EvolutionContactSource::EvolutionContactSource( const string &name,
                                                const string &changeId,
                                                const string &id,
                                                EVCardFormat vcardFormat ) :
    EvolutionSyncSource( name, changeId, id ),
    m_vcardFormat( vcardFormat )
{
}

EvolutionContactSource::EvolutionContactSource( const EvolutionContactSource &other ) :
        EvolutionSyncSource( other ),
        m_vcardFormat( other.m_vcardFormat )
{
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

void EvolutionContactSource::beginSyncThrow(bool needAll,
                                            bool needPartial,
                                            bool deleteLocal)
{
    GError *gerror = NULL;

    if (deleteLocal) {
        gptr<EBookQuery> allItemsQuery( e_book_query_any_field_contains(""), "query" );
        GList *nextItem;
        if (!e_book_get_contacts( m_addressbook, allItemsQuery, &nextItem, &gerror )) {
            throwError( "reading all items", gerror );
        }
        while (nextItem) {
            const char *uid = (const char *)e_contact_get_const(E_CONTACT(nextItem->data),
                                                                E_CONTACT_UID);
            if (!e_book_remove_contact( m_addressbook, uid, &gerror ) ) {
                throwError( string( "deleting contact" ) + uid,
                            gerror );
            }
            nextItem = nextItem->next;
        }
    }

    if (needAll) {
        gptr<EBookQuery> allItemsQuery( e_book_query_any_field_contains(""), "query" );
        GList *nextItem;
        if (!e_book_get_contacts( m_addressbook, allItemsQuery, &nextItem, &gerror )) {
            throwError( "reading all items", gerror );
        }
        while (nextItem) {
            const char *uid = (const char *)e_contact_get_const(E_CONTACT(nextItem->data),
                                                                E_CONTACT_UID);
            m_allItems.addItem(uid);
            nextItem = nextItem->next;
        }
    }

    if (needPartial) {
        GList *nextItem;
        if (!e_book_get_changes( m_addressbook, (char *)m_changeId.c_str(), &nextItem, &gerror )) {
            throwError( "reading changes", gerror );
        }
        while (nextItem) {
            EBookChange *ebc = (EBookChange *)nextItem->data;
            const char *uid = (const char *)e_contact_get_const( ebc->contact, E_CONTACT_UID );

            switch (ebc->change_type) {            
             case E_BOOK_CHANGE_CARD_ADDED:
                m_newItems.addItem(uid);
                break;
             case E_BOOK_CHANGE_CARD_MODIFIED:
                m_updatedItems.addItem(uid);
                break;
             case E_BOOK_CHANGE_CARD_DELETED:
                m_deletedItems.addItem(uid);
                break;
            }
            nextItem = nextItem->next;
        }
    }
}

void EvolutionContactSource::endSyncThrow()
{
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

void EvolutionContactSource::exportData(ostream &out)
{
    gptr<EBookQuery> allItemsQuery( e_book_query_any_field_contains(""), "query" );
    GList *nextItem;
    GError *gerror = NULL;
    if (!e_book_get_contacts( m_addressbook, allItemsQuery, &nextItem, &gerror )) {
        throwError( "reading all items", gerror );
    }
    while (nextItem) {
        gptr<char> vcardstr(e_vcard_to_string(&E_CONTACT(nextItem->data)->parent,
                                              m_vcardFormat));

        out << (const char *)vcardstr << "\r\n\r\n";
        nextItem = nextItem->next;
    }
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
        LOG.debug( vcardstr );

        // convert from 3.0 to 2.1 so that Sync4j 2.3 accepts it
        std::auto_ptr<VObject> vobj(VConverter::parse(vcardstr));
        if (vobj.get() == 0) {
            throwError( string( "parsing contact" ) + uid, NULL );
        }
        vobj->toNativeEncoding();

        // escape extended properties so that they are preserved
        // as custom values by the server
        for (int index = vobj->propertiesCount() - 1;
             index >= 0;
             index--) {
            VProperty *vprop = vobj->getProperty(index);
            string name = vprop->getName();
            if (m_vcardExtensions.find(name) != m_vcardExtensions.end()) {
                name = m_vcardExtensions.prefix + name;
                vprop->setName(name.c_str());
            }
        }
        
        vobj->setVersion("2.1");
        VProperty *vprop = vobj->getProperty("VERSION");
        vprop->setValue("2.1");
        vobj->fromNativeEncoding();
        char *finalstr = vobj->toString();
        LOG.debug("after conversion to 2.1:");
        LOG.debug(finalstr);

        auto_ptr<SyncItem> item( new SyncItem( uid.c_str() ) );
        item->setData( finalstr, strlen( finalstr ) + 1 );
        item->setDataType( getMimeType() );
        item->setModificationTime( 0 );
        item->setState( state );

        return item.release();
    } catch (...) {
        m_hasFailed = true;
    }

    return NULL;
}

string EvolutionContactSource::preparseVCard(SyncItem& item)
{
    string data = getData(item);
    // convert to 3.0 to get rid of quoted-printable encoded
    // non-ASCII chars, because Evolution does not support
    // decoding them
    LOG.debug(data.c_str());
    std::auto_ptr<VObject> vobj(VConverter::parse((char *)data.c_str()));
    if (vobj.get() == 0) {
        throwError( string( "parsing contact" ) + item.getKey(), NULL );
    }
    vobj->toNativeEncoding();

    // convert our escaped properties back,
    // extend certain properties so that Evolution can parse them,
    // ensure that unique properties appear indeed only once
    set<string> found;
    for (int index = vobj->propertiesCount() - 1;
         index >= 0;
         index--) {
        VProperty *vprop = vobj->getProperty(index);
        string name = vprop->getName();
        if (name.size() > m_vcardExtensions.prefix.size() &&
            !name.compare(0, m_vcardExtensions.prefix.size(), m_vcardExtensions.prefix)) {
            name = name.substr(m_vcardExtensions.prefix.size());
            vprop->setName(name.c_str());
        } else if (name == "ADR" || name == "EMAIL" ) {
            // ensure that at least one TYPE is set
            if (!vprop->containsParameter("TYPE") &&
                !vprop->containsParameter("HOME") &&
                !vprop->containsParameter("WORK")) {
                vprop->addParameter("TYPE", "OTHER");
            }
        }

        if (m_uniqueProperties.find(name) != m_uniqueProperties.end()) {
            // has to be unique
            if (found.find(name) != found.end()) {
                // remove older entry
                vobj->removeProperty(index);
            } else {
                // remember that valid instance exists
                found.insert(name);
            }
        }
    }

    vobj->setVersion("3.0");
    VProperty *vprop = vobj->getProperty("VERSION");
    vprop->setValue("3.0");
    vobj->fromNativeEncoding();
    data = vobj->toString();
    LOG.debug("after conversion to 3.0:");
    LOG.debug(data.c_str());
    return data;
}

void EvolutionContactSource::setItemStatusThrow(const char *key, int status)
{
    switch (status) {
     case STC_CONFLICT_RESOLVED_WITH_SERVER_DATA: {
        // make a copy before allowing the server to overwrite it
        char buffer[200];

        sprintf(buffer,
                "contact %.80s: conflict, will be replaced by server contact - create copy\n",
                key);
        LOG.error(buffer);
        
        EContact *contact;
        GError *gerror = NULL;
        if (! e_book_get_contact( m_addressbook,
                                  key,
                                  &contact,
                                  &gerror ) ) {
            sprintf(buffer,
                    "item %.80s: reading original for copy failed\n",
                    key);
            LOG.error(buffer);
            break;
        }
        EContact *copy = e_contact_duplicate(contact);
        if(!copy ||
           ! e_book_add_contact(m_addressbook,
                                copy,
                                &gerror)) {
            sprintf(buffer,
                    "item %.80s: making copy failed\n",
                    key);
            LOG.error(buffer);
            break;
        }
        break;
     }
     default:
        EvolutionSyncSource::setItemStatusThrow(key, status);
        break;
    }
}

void EvolutionContactSource::addItemThrow(SyncItem& item)
{
    string data;
    if( strcmp(item.getDataType(), "raw" ) ) {
        data = preparseVCard(item);
    } else {
        data = (const char *)item.getData();
    }
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
}

void EvolutionContactSource::updateItemThrow(SyncItem& item)
{
    string data = preparseVCard(item);
    gptr<EContact, GObject> contact(e_contact_new_from_vcard(data.c_str()));
    if( contact ) {
        GError *gerror = NULL;

        // The following code commits the new_from_vcard contact using the
        // existing UID. It has been observed in Evolution 2.0.4 that the
        // changes were then not "noticed" properly by the Evolution GUI.
        //
        // The code below was supposed to "notify" Evolution of the change by
        // loaded the updated contact, modifying it, committing, restoring
        // and committing once more, but that did not solve the problem.
        //
        // TODO: test with current Evolution
        e_contact_set( contact, E_CONTACT_UID, item.getKey() );
        if ( e_book_commit_contact(m_addressbook, contact, &gerror) ) {
            const char *uid = (const char *)e_contact_get_const(contact, E_CONTACT_UID);
            if (uid) {
                item.setKey( uid );
            }

#if 0
            EContact *refresh_contact;
            if (! e_book_get_contact( m_addressbook,
                                      uid,
                                      &refresh_contact,
                                      &gerror ) ) {
                throwError( string( "reading refresh contact" ) + uid,
                            gerror );
            }
            string nick = (const char *)e_contact_get_const(refresh_contact, E_CONTACT_NICKNAME);
            string nick_mod = nick + "_";
            e_contact_set(refresh_contact, E_CONTACT_NICKNAME, (void *)nick_mod.c_str());
            e_book_commit_contact(m_addressbook, refresh_contact, &gerror);
            e_contact_set(refresh_contact, E_CONTACT_NICKNAME, (void *)nick.c_str());
            e_book_commit_contact(m_addressbook, refresh_contact, &gerror);
#endif
        } else {
            throwError( string( "updating contact" ) + item.getKey(), gerror );
        }
    } else {
        throwError( string( "parsing vcard" ) + data,
                    NULL );
    }
}

void EvolutionContactSource::deleteItemThrow(SyncItem& item)
{
    GError *gerror = NULL;
    if (!e_book_remove_contact( m_addressbook, item.getKey(), &gerror ) ) {
        throwError( string( "deleting contact" ) + item.getKey(),
                    gerror );
    }

    m_isModified = true;
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
        const char *data = (const char *)item.getData();
        int datasize = item.getDataSize();
        if (datasize <= 0) {
            data = "";
            datasize = 0;
        }
        string vcard( data, datasize );

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


EContact *EvolutionContactSource::getContact( const string &uid )
{
    EContact *contact;
    GError *gerror = NULL;
    if (e_book_get_contact( m_addressbook,
                            uid.c_str(),
                            &contact,
                            &gerror )) {
        return contact;
    } else {
        return NULL;
    }
}
