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

#include <memory>
#include <map>
#include <sstream>
using namespace std;

#include "config.h"

#ifdef ENABLE_EBOOK

#include "EvolutionContactSource.h"

#include <common/base/Log.h>
#include "vocl/VConverter.h"

using namespace vocl;

const EvolutionContactSource::extensions EvolutionContactSource::m_vcardExtensions;
const EvolutionContactSource::unique EvolutionContactSource::m_uniqueProperties;

EvolutionContactSource::EvolutionContactSource( const string &name,
                                                SyncSourceConfig *sc,
                                                const string &changeId,
                                                const string &id,
                                                EVCardFormat vcardFormat ) :
    EvolutionSyncSource( name, sc, changeId, id ),
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
        throw runtime_error("unable to access address books");
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

    // No results? Try system address book (workaround for embedded Evolution Dataserver).
    if (!result.size()) {
        eptr<EBook, GObject> book;
        GError *gerror;
        const char *name;

        name = "<<system>>";
        book = e_book_new_system_addressbook (&gerror);
        g_clear_error(&gerror);
        if (!book) {
            name = "<<default>>";
            book = e_book_new_default_addressbook (&gerror);
        }
        g_clear_error(&gerror);

        if (book) {
            const char *uri = e_book_get_uri (book);
            result.push_back (EvolutionSyncSource::source (name, uri));
        }
    }
    
    return result;
}

void EvolutionContactSource::open()
{
    ESourceList *sources;
    if (!e_book_get_addressbooks(&sources, NULL)) {
        throw runtime_error("unable to access address books");
    }
    
    GError *gerror = NULL;
    ESource *source = findSource( sources, m_id );
    if (!source) {
        // might have been special "<<system>>" or "<<default>>", try that before giving up
        if (m_id == "<<system>>") {
            m_addressbook.set( e_book_new_system_addressbook (&gerror), "system address book" );
        } else if (m_id == "<<default>>") {
            m_addressbook.set( e_book_new_default_addressbook (&gerror), "default address book" );
        } else {
            throw runtime_error(string(getName()) + ": no such address book: '" + m_id + "'");
        }
    } else {
        m_addressbook.set( e_book_new( source, &gerror ), "address book" );
    }
 
    if (!e_book_open( m_addressbook, TRUE, &gerror) ) {
        throwError( "opening address book", gerror );
    }

    // users are not expected to configure an authentication method,
    // so pick one automatically if the user indicated that he wants authentication
    // by setting user or password
    string user, passwd;
    getAuthentication(user, passwd);
    if (passwd.size() || passwd.size()) {
        GList *authmethod;
        if (!e_book_get_supported_auth_methods(m_addressbook, &authmethod, &gerror)) {
            throwError("getting authentication methods", gerror );
        }
        while (authmethod) {
            const char *method = (const char *)authmethod->data;
            LOG.debug("%s: trying authentication method \"%s\", user %s, password %s",
                      getName(), method,
                      user.size() ? "configured" : "not configured",
                      passwd.size() ? "configured" : "not configured");
            if (e_book_authenticate_user(m_addressbook,
                                         user.c_str(),
                                         passwd.c_str(),
                                         method,
                                         &gerror)) {
                LOG.debug("%s: authentication succeeded", getName());
                break;
            } else {
                LOG.error("%s: authentication failed: %s",
                          getName(), gerror->message);
                g_clear_error(&gerror);
            }
        }
        authmethod = authmethod->next;
    }
}

void EvolutionContactSource::beginSyncThrow(bool needAll,
                                            bool needPartial,
                                            bool deleteLocal)
{
    GError *gerror = NULL;

    if (deleteLocal) {
        eptr<EBookQuery> allItemsQuery( e_book_query_any_field_contains(""), "query" );
        GList *nextItem;
        if (!e_book_get_contacts( m_addressbook, allItemsQuery, &nextItem, &gerror )) {
            throwError( "reading all items", gerror );
        }
        while (nextItem) {
            const char *uid = (const char *)e_contact_get_const(E_CONTACT(nextItem->data),
                                                                E_CONTACT_UID);
            if (!e_book_remove_contact( m_addressbook, uid, &gerror ) ) {
                throwError( string( "deleting contact " ) + uid,
                            gerror );
            }
            nextItem = nextItem->next;
        }
    }

    if (needAll) {
        eptr<EBookQuery> allItemsQuery( e_book_query_any_field_contains(""), "query" );
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

            if (ebc->contact) {
                const char *uid = (const char *)e_contact_get_const( ebc->contact, E_CONTACT_UID );
                
                if (uid) {
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
                }
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
    eptr<EBookQuery> allItemsQuery( e_book_query_any_field_contains(""), "query" );
    GList *nextItem;
    GError *gerror = NULL;
    if (!e_book_get_contacts( m_addressbook, allItemsQuery, &nextItem, &gerror )) {
        throwError( "reading all items", gerror );
    }
    while (nextItem) {
        eptr<char> vcardstr(e_vcard_to_string(&E_CONTACT(nextItem->data)->parent,
                                              EVC_FORMAT_VCARD_30));

        out << (const char *)vcardstr << "\r\n\r\n";
        nextItem = nextItem->next;
    }
}

SyncItem *EvolutionContactSource::createItem( const string &uid, SyncState state )
{
    logItem( uid, "extracting from EV" );
        
    EContact *contact;
    GError *gerror = NULL;
    if (! e_book_get_contact( m_addressbook,
                              uid.c_str(),
                              &contact,
                              &gerror ) ) {
        throwError( string( "reading contact " ) + uid,
                    gerror );
    }
    eptr<EContact, GObject> contactptr( contact, "contact" );
    eptr<char> vcardstr(e_vcard_to_string( &contactptr->parent,
                                           EVC_FORMAT_VCARD_30 ) );
    if (!vcardstr) {
        throwError( string( "contact from Evolution " ) + uid, NULL );
    }
    LOG.debug( vcardstr );

    std::auto_ptr<VObject> vobj(VConverter::parse(vcardstr));
    if (vobj.get() == 0) {
        throwError( string( "parsing contact " ) + uid, NULL );
    }

    vobj->toNativeEncoding();

    for (int index = vobj->propertiesCount() - 1;
         index >= 0;
         index--) {
        VProperty *vprop = vobj->getProperty(index);

        // map ADR;TYPE=OTHER (not standard-compliant)
        // to ADR;TYPE=PARCEL and vice-versa in preparseVCard();
        // other TYPE=OTHER instances are simply removed

        bool parcel = false;

        int param = 0;
        while (param < vprop->parameterCount()) {
            if (!strcasecmp(vprop->getParameter(param), "TYPE") &&
                !strcasecmp(vprop->getParameterValue(param), "OTHER")) {
                vprop->removeParameter(param);
                if (!strcasecmp(vprop->getName(), "ADR")) {
                    parcel = true;
                }
            } else {
                param++;
            }
        }

        if (parcel) {
            vprop->addParameter("TYPE", "PARCEL");
        }
    }

    // convert from 3.0 to 2.1?
    if (m_vcardFormat == EVC_FORMAT_VCARD_21) {
        LOG.debug("convert to 2.1");

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

            // replace 3.0 ENCODING=B with 3.0 ENCODING=BASE64
            char *encoding = vprop->getParameterValue("ENCODING");
            if (encoding &&
                !strcasecmp("B", encoding)) {
                vprop->removeParameter("ENCODING");
                vprop->addParameter("ENCODING", "BASE64");
            }
        }

        vobj->setVersion("2.1");
        VProperty *vprop = vobj->getProperty("VERSION");
        vprop->setValue("2.1");
    }

    vobj->fromNativeEncoding();

    arrayptr<char> finalstr(vobj->toString(), "VOCL string");
    LOG.debug("after conversion:");
    LOG.debug("%s", (char *)finalstr);

    auto_ptr<SyncItem> item( new SyncItem( uid.c_str() ) );
    item->setData( (char *)finalstr, strlen(finalstr) );
    item->setDataType( getMimeType() );
    item->setModificationTime( 0 );
    item->setState( state );

    return item.release();
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
        throwError( string( "parsing contact " ) + item.getKey(), NULL );
    }
    vobj->toNativeEncoding();

    // - convert our escaped properties back
    // - extend certain properties so that Evolution can parse them
    // - ensure that unique properties appear indeed only once (because
    //   for some properties the server doesn't know that they have to be
    //   unique)
    // - add X-EVOLUTION-UI-SLOT to TEL and MAIL properties (code just added
    //   for experiments, never enabled)
    // - split TYPE=WORK,VOICE into TYPE=WORK;TYPE=VOICE
    set<string> found;

#ifdef SET_UI_SLOT
    class slots : public map< string, set<string> > {
    public:
        slots() {
            insert(value_type(string("ADR"), set<string>()));
            insert(value_type(string("EMAIL"), set<string>()));
            insert(value_type(string("TEL"), set<string>()));
        }
        string assignFree(string type) {
            int slot = 1;
            set<string> &used((*this)[type]);
            
            while (true) {
                stringstream buffer;
                buffer << slot;
                string slotstr = buffer.str();
                if (used.find(slotstr) == used.end()) {
                    used.insert(slotstr);
                    return slotstr;
                }
                slot++;
            }
        }
    } usedSlots;
#endif

    for (int index = vobj->propertiesCount() - 1;
         index >= 0;
         index--) {
        VProperty *vprop = vobj->getProperty(index);
        string name = vprop->getName();
        if (name.size() > m_vcardExtensions.prefix.size() &&
            !name.compare(0, m_vcardExtensions.prefix.size(), m_vcardExtensions.prefix)) {
            name = name.substr(m_vcardExtensions.prefix.size());
            vprop->setName(name.c_str());
        } else if (name == "ADR" || name == "EMAIL" || name == "TEL") {
            const char *type = vprop->getParameterValue("TYPE");
            if (type) {
                if (!strcasecmp(type, "PARCEL")) {
                    // remove unsupported TYPE=PARCEL that was
                    // added in createItem()
                    vprop->removeParameter("TYPE");
                } else if (!strcasecmp(type, "PREF,VOICE")) {
                    // this is not mapped by Evolution to "Primary Phone",
                    // help a little bit
                    vprop->removeParameter("TYPE");
                    vprop->addParameter("TYPE", "PREF");
                } else if (strchr(type, ',')) {
                    // Evolution cannot handle e.g. "WORK,VOICE". Split into
                    // different parts.
                    string buffer = type, value;
                    int start = 0, end;
                    vprop->removeParameter("TYPE");
                    while ((end = buffer.find(',', start)) != buffer.npos) {
                        value = buffer.substr(start, end - start);
                        vprop->addParameter("TYPE", value.c_str());
                        start = end + 1;
                    }
                    value = buffer.substr(start);
                    vprop->addParameter("TYPE", value.c_str());
                }
            }

            // ensure that at least one TYPE is set
            if (!vprop->containsParameter("TYPE") &&
                !vprop->containsParameter("INTERNET") &&
                !vprop->containsParameter("HOME") &&
                !vprop->containsParameter("WORK")) {
                vprop->addParameter("TYPE", "HOME");
            }

#ifdef SET_UI_SLOT
            // remember which slots are set
            const char *slot = vprop->getParameterValue("X-EVOLUTION-UI-SLOT");
            if (slot) {
                usedSlots[name].insert(slot);
            }
#endif
        }

        // replace 2.1 ENCODING=BASE64 with 3.0 ENCODING=B
        char *encoding = vprop->getParameterValue("ENCODING");
        if (encoding &&
            !strcasecmp("BASE64", encoding)) {
            vprop->removeParameter("ENCODING");
            vprop->addParameter("ENCODING", "B");
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

#ifdef SET_UI_SLOT
    // add missing slot parameters
    for (int index = 0;
         index < vobj->propertiesCount();
         index++) {
        VProperty *vprop = vobj->getProperty(index);
        string name = vprop->getName();
        if (name == "EMAIL" || name == "TEL") {
            const char *slot = vprop->getParameterValue("X-EVOLUTION-UI-SLOT");

            if (!slot) {
                string freeslot = usedSlots.assignFree(name);
                vprop->addParameter("X-EVOLUTION-UI-SLOT", freeslot.c_str());
            }
        }
    }
#endif
    
    vobj->setVersion("3.0");
    VProperty *vprop = vobj->getProperty("VERSION");
    vprop->setValue("3.0");
    vobj->fromNativeEncoding();
    arrayptr<char> voclstr(vobj->toString(), "VOCL string");
    data = (char *)voclstr;
    LOG.debug("after conversion to 3.0:");
    LOG.debug("%s", data.c_str());
    return data;
}

void EvolutionContactSource::setItemStatusThrow(const char *key, int status)
{
    switch (status) {
     case STC_CONFLICT_RESOLVED_WITH_SERVER_DATA: {
        // make a copy before allowing the server to overwrite it

        LOG.error("%s: contact %s: conflict, will be replaced by server contact - create copy",
                  getName(), key);
        
        EContact *contact;
        GError *gerror = NULL;
        if (! e_book_get_contact( m_addressbook,
                                  key,
                                  &contact,
                                  &gerror ) ) {
            LOG.error("%s: item %.80s: reading original for copy failed",
                      getName(), key);
            break;
        }
        EContact *copy = e_contact_duplicate(contact);
        if(!copy ||
           ! e_book_add_contact(m_addressbook,
                                copy,
                                &gerror)) {
            LOG.error("%s: item %.80s: making copy failed",
                      getName(), key);
            break;
        }
        break;
     }
     default:
        EvolutionSyncSource::setItemStatusThrow(key, status);
        break;
    }
}

int EvolutionContactSource::addItemThrow(SyncItem& item)
{
    int status = STC_OK;
    string data;
    if( strcmp(item.getDataType(), "raw" ) ) {
        data = preparseVCard(item);
    } else {
        data = (const char *)item.getData();
    }
    eptr<EContact, GObject> contact(e_contact_new_from_vcard(data.c_str()));
    if( contact ) {
        GError *gerror = NULL;
        e_contact_set(contact, E_CONTACT_UID, NULL);
        if (e_book_add_contact(m_addressbook, contact, &gerror)) {
            item.setKey( (const char *)e_contact_get_const( contact, E_CONTACT_UID ) );
        } else {
            throwError( "storing new contact", gerror );
        }
    } else {
        throwError( string( "parsing vcard " ) + data,
                    NULL );
    }
    return status;
}

int EvolutionContactSource::updateItemThrow(SyncItem& item)
{
    int status = STC_OK;
    string data = preparseVCard(item);
    eptr<EContact, GObject> contact(e_contact_new_from_vcard(data.c_str()));
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
        e_contact_set( contact, E_CONTACT_UID, (void *)item.getKey() );
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
                throwError( string( "reading refresh contact " ) + uid,
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
            throwError( string( "updating contact " ) + item.getKey(), gerror );
        }
    } else {
        throwError( string( "parsing vcard " ) + data,
                    NULL );
    }
    return status;
}

int EvolutionContactSource::deleteItemThrow(SyncItem& item)
{
    int status = STC_OK;
    GError *gerror = NULL;
    if (!e_book_remove_contact( m_addressbook, item.getKey(), &gerror ) ) {
        if (gerror->domain == E_BOOK_ERROR &&
            gerror->code == E_BOOK_ERROR_CONTACT_NOT_FOUND) {
            LOG.debug("%s: %s: request to delete non-existant contact ignored",
                      getName(), item.getKey());
            g_clear_error(&gerror);
        } else {
            throwError( string( "deleting contact " ) + item.getKey(),
                        gerror );
        }
    }
    return status;
}

const char *EvolutionContactSource::getMimeType()
{
    switch( m_vcardFormat ) {
     case EVC_FORMAT_VCARD_21:
        return "text/x-vcard";
        break;
     case EVC_FORMAT_VCARD_30:
     default:
        return "text/vcard";
        break;
    }
}

const char *EvolutionContactSource::getMimeVersion()
{
    switch( m_vcardFormat ) {
     case EVC_FORMAT_VCARD_21:
        return "2.1";
        break;
     case EVC_FORMAT_VCARD_30:
     default:
        return "3.0";
        break;
    }
}

void EvolutionContactSource::logItem(const string &uid, const string &info, bool debug)
{
    if (LOG.getLevel() >= (debug ? LOG_LEVEL_DEBUG : LOG_LEVEL_INFO)) {
        string line;
        EContact *contact;
        GError *gerror = NULL;

        if (e_book_get_contact( m_addressbook,
                                uid.c_str(),
                                &contact,
                                &gerror )) {
            const char *fileas = (const char *)e_contact_get_const( contact, E_CONTACT_FILE_AS );
            if (fileas) {
                line += fileas;
            } else {
                const char *name = (const char *)e_contact_get_const( contact, E_CONTACT_FULL_NAME );
                if (name) {
                    line += name;
                } else {
                    line += "<unnamed contact>";
                }
            }
        } else {
            line += "<name unavailable>";
        }
        line += " (";
        line += uid;
        line += "): ";
        line += info;
        
        (LOG.*(debug ? &Log::debug : &Log::info))( "%s: %s", getName(), line.c_str() );
    }
}

void EvolutionContactSource::logItem(SyncItem &item, const string &info, bool debug)
{
    if (LOG.getLevel() >= (debug ? LOG_LEVEL_DEBUG : LOG_LEVEL_INFO)) {
        string line;
        const char *data = (const char *)item.getData();
        int datasize = item.getDataSize();
        if (datasize <= 0) {
            data = "";
            datasize = 0;
        }
        string vcard( data, datasize );

        size_t offset = vcard.find( "FN:");
        if (offset != vcard.npos) {
            int len = vcard.find( "\r", offset ) - offset - 3;
            line += vcard.substr( offset + 3, len );
        } else {
            line += "<unnamed contact>";
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
                line += ", EV ";
                const char *fileas = (const char *)e_contact_get_const( contact, E_CONTACT_FILE_AS );
                if (fileas) {
                    line += fileas;
                } else {
                    const char *name = (const char *)e_contact_get_const( contact, E_CONTACT_FULL_NAME );
                    if (name) {
                        line += name;
                    } else {
                        line += "<unnamed contact>";
                    }
                }
            } else {
                line += ", not in Evolution";
            }
        }
        line += ": ";
        line += info;
        
        (LOG.*(debug ? &Log::debug : &Log::info))( "%s: %s", getName(), line.c_str() );
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

#endif /* ENABLE_EBOOK */
