/*
 * Copyright (C) 2005-2009 Patrick Ohly <patrick.ohly@gmx.de>
 * Copyright (C) 2009 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) version 3.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301  USA
 */

#include <memory>
#include <map>
#include <sstream>
#include <list>
using namespace std;

#include "config.h"
#include "EvolutionSyncSource.h"

#ifdef ENABLE_EBOOK

#include "EvolutionSyncClient.h"
#include "EvolutionContactSource.h"
#include "SyncEvolutionUtil.h"

#include "Logging.h"

#include <boost/algorithm/string.hpp>
#include <boost/foreach.hpp>

class unrefEBookChanges {
 public:
    /** free list of EBookChange instances */
    static void unref(GList *pointer) {
        if (pointer) {
            GList *next = pointer;
            do {
                EBookChange *ebc = (EBookChange *)next->data;
                g_object_unref(ebc->contact);
                g_free(next->data);
                next = next->next;
            } while (next);
            g_list_free(pointer);
        }
    }
};


const EvolutionContactSource::extensions EvolutionContactSource::m_vcardExtensions;
const EvolutionContactSource::unique EvolutionContactSource::m_uniqueProperties;

EvolutionContactSource::EvolutionContactSource(const EvolutionSyncSourceParams &params,
                                               EVCardFormat vcardFormat) :
    TrackingSyncSource(params),
    m_vcardFormat(vcardFormat)
{
}

EvolutionContactSource::EvolutionContactSource( const EvolutionContactSource &other ) :
        TrackingSyncSource( other ),
        m_vcardFormat( other.m_vcardFormat )
{
}

EvolutionSyncSource::Databases EvolutionContactSource::getDatabases()
{
    ESourceList *sources = NULL;

    if (!e_book_get_addressbooks(&sources, NULL)) {
        EvolutionSyncClient::throwError("unable to access address books");
    }

    Databases result;
    bool first = true;
    for (GSList *g = e_source_list_peek_groups (sources); g; g = g->next) {
        ESourceGroup *group = E_SOURCE_GROUP (g->data);
        for (GSList *s = e_source_group_peek_sources (group); s; s = s->next) {
            ESource *source = E_SOURCE (s->data);
            eptr<char> uri(e_source_get_uri(source));
            result.push_back(Database(e_source_peek_name(source),
                                                         uri ? uri.get() : "",
                                                         first));
            first = false;
        }
    }

    // No results? Try system address book (workaround for embedded Evolution Dataserver).
    if (!result.size()) {
        eptr<EBook, GObject> book;
        GError *gerror = NULL;
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
            result.push_back(Database(name, uri, true));
        }
    }
    
    return result;
}

void EvolutionContactSource::open()
{
    ESourceList *sources;
    if (!e_book_get_addressbooks(&sources, NULL)) {
        throwError("unable to access address books");
    }
    
    GError *gerror = NULL;
    string id = getDatabaseID();
    ESource *source = findSource(sources, id);
    bool onlyIfExists = true;
    if (!source) {
        // might have been special "<<system>>" or "<<default>>", try that and
        // creating address book from file:// URI before giving up
        if (id.empty() || id == "<<system>>") {
            m_addressbook.set( e_book_new_system_addressbook (&gerror), "system address book" );
        } else if (id.empty() || id == "<<default>>") {
            m_addressbook.set( e_book_new_default_addressbook (&gerror), "default address book" );
        } else if (boost::starts_with(id, "file://")) {
            m_addressbook.set(e_book_new_from_uri(id.c_str(), &gerror), "creating address book");
        } else {
            throwError(string(getName()) + ": no such address book: '" + id + "'");
        }
        onlyIfExists = false;
    } else {
        m_addressbook.set( e_book_new( source, &gerror ), "address book" );
    }
 
    if (!e_book_open( m_addressbook, onlyIfExists, &gerror) ) {
        // opening newly created address books often fails, try again once more
        g_clear_error(&gerror);
        sleep(5);
        if (!e_book_open( m_addressbook, onlyIfExists, &gerror) ) {
            throwError( "opening address book", gerror );
        }
    }

    // users are not expected to configure an authentication method,
    // so pick one automatically if the user indicated that he wants authentication
    // by setting user or password
    const char *user = getUser(),
        *passwd = getPassword();
    if ((user && user[0]) || (passwd && passwd[0])) {
        GList *authmethod;
        if (!e_book_get_supported_auth_methods(m_addressbook, &authmethod, &gerror)) {
            throwError("getting authentication methods", gerror );
        }
        while (authmethod) {
            const char *method = (const char *)authmethod->data;
            SE_LOG_DEBUG(this, NULL, "%s: trying authentication method \"%s\", user %s, password %s",
                      getName(), method,
                      user && user[0] ? "configured" : "not configured",
                      passwd && passwd[0] ? "configured" : "not configured");
            if (e_book_authenticate_user(m_addressbook,
                                         user ? user : "",
                                         passwd ? passwd : "",
                                         method,
                                         &gerror)) {
                SE_LOG_DEBUG(this, NULL, "authentication succeeded");
                break;
            } else {
                SE_LOG_ERROR(this, NULL, "authentication failed: %s", gerror->message);
                g_clear_error(&gerror);
            }
            authmethod = authmethod->next;
        }
    }

    g_signal_connect_after(m_addressbook,
                           "backend-died",
                           G_CALLBACK(EvolutionSyncClient::fatalError),
                           (void *)"Evolution Data Server has died unexpectedly, contacts no longer available.");
}


void EvolutionContactSource::listAllItems(RevisionMap_t &revisions)
{
    GError *gerror = NULL;
    eptr<EBookQuery> allItemsQuery(e_book_query_any_field_contains(""), "query");
    GList *nextItem;
    if (!e_book_get_contacts(m_addressbook, allItemsQuery, &nextItem, &gerror)) {
        throwError( "reading all items", gerror );
    }
    eptr<GList> listptr(nextItem);
    while (nextItem) {
        EContact *contact = E_CONTACT(nextItem->data);
        if (!contact) {
            throwError("contact entry without data");
        }
        pair<string, string> revmapping;
        const char *uid = (const char *)e_contact_get_const(contact,
                                                            E_CONTACT_UID);
        if (!uid || !uid[0]) {
            throwError("contact entry without UID");
        }
        revmapping.first = uid;
        const char *rev = (const char *)e_contact_get_const(contact,
                                                            E_CONTACT_REV);
        if (!rev || !rev[0]) {
            throwError(string("contact entry without REV: ") + revmapping.first);
        }
        revmapping.second = rev;
        revisions.insert(revmapping);
        nextItem = nextItem->next;
    }
}

void EvolutionContactSource::close()
{
    // Our change tracking is time based.
    // Don't let caller proceed without waiting for
    // one second to prevent being called again before
    // the modification time stamp is larger than it
    // is now.
    sleepSinceModification(1);

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
    eptr<GList> listptr(nextItem);
    while (nextItem) {
        eptr<char> vcardstr(e_vcard_to_string(&E_CONTACT(nextItem->data)->parent,
                                              EVC_FORMAT_VCARD_30));

        if (!(const char *)vcardstr) {
            throwError("could not convert contact into string");
        }
        out << (const char *)vcardstr << "\r\n\r\n";
        nextItem = nextItem->next;
    }
}

string EvolutionContactSource::getRevision(const string &luid)
{
    EContact *contact;
    GError *gerror = NULL;
    if (!e_book_get_contact(m_addressbook,
                            luid.c_str(),
                            &contact,
                            &gerror)) {
        throwError(string("reading contact ") + luid,
                   gerror);
    }
    eptr<EContact, GObject> contactptr(contact, "contact");
    const char *rev = (const char *)e_contact_get_const(contact,
                                                        E_CONTACT_REV);
    if (!rev || !rev[0]) {
        throwError(string("contact entry without REV: ") + luid);
    }
    return rev;
}

SyncItem *EvolutionContactSource::createItem(const string &luid)
{
    logItem(luid, "extracting from EV", true);

    EContact *contact;
    GError *gerror = NULL;
    if (!e_book_get_contact(m_addressbook,
                            luid.c_str(),
                            &contact,
                            &gerror)) {
        throwError(string("reading contact ") + luid,
                   gerror);
    }
    eptr<EContact, GObject> contactptr(contact, "contact");
    eptr<char> vcardstr(e_vcard_to_string(&contactptr->parent,
                                          EVC_FORMAT_VCARD_30));
    if (!vcardstr) {
        throwError(string("failure extracting contact from Evolution " ) + luid);
    }
    SE_LOG_DEBUG(this, NULL, "%s", vcardstr.get());

#if 0
    // @TODO reimplement Evolution hacks via Synthesis datatype conversions
    std::auto_ptr<VObject> vobj(VConverter::parse(vcardstr));
    if (vobj.get() == 0) {
        throwError(string("failure parsing contact " ) + uid);
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
        SE_LOG_DEBUG(this, NULL, "convert to 2.1");

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

            // Workaround for Funambol 3.0 parser bug:
            // trailing = are interpreted as soft line break
            // even if the property doesn't use QUOTED-PRINTABLE encoding.
            // Avoid that situation by enabling QUOTED-PRINTABLE for
            // such properties.
            if (!encoding) {
                char *value = vprop->getValue();

                if (value &&
                    value[0] &&
                    value[strlen(value)-1] == '=') {
                    vprop->addParameter("ENCODING", "QUOTED-PRINTABLE");
                }
            }

            // Split TYPE=foo,bar into TYPE=foo;TYPE=bar because the comma-separated
            // list is an extension of 3.0.
            list<string> types;
            while (true) {
                char *type = vprop->getParameterValue("TYPE");
                if (!type) {
                    break;
                }
                StringBuffer buff(type);

                char *token = strtok((char *)buff.c_str(), ",");
                while (token != NULL) {
                    types.push_back(token);
                    token = strtok(NULL, ",");
                }
                vprop->removeParameter("TYPE");
            }
            BOOST_FOREACH(const string &type, types) {
                vprop->addParameter("TYPE", type.c_str());
            }

            // Also make all strings uppercase because 3.0 is
            // case-insensitive whereas 2.1 requires uppercase.
            list< pair<string, string> > parameters;
            while (vprop->parameterCount() > 0) {
                const char *param = vprop->getParameter(0);
                const char *value = vprop->getParameterValue(0);
                if (!param || !value) {
                    break;
                }
                parameters.push_back(pair<string, string>(boost::to_upper_copy(string(param)), boost::to_upper_copy(string(value))));
                vprop->removeParameter(0);
            }
            while (parameters.size() > 0) {
                pair<string, string> param_value = parameters.front();
                vprop->addParameter(param_value.first.c_str(), param_value.second.c_str());
                parameters.pop_front();
            }
        }

        vobj->setVersion("2.1");
        VProperty *vprop = vobj->getProperty("VERSION");
        vprop->setValue("2.1");
    }

    vobj->fromNativeEncoding();

    arrayptr<char> finalstr(vobj->toString(), "VOCL string");
    SE_LOG_DEBUG(this, NULL, "after conversion:");
    SE_LOG_DEBUG(this, NULL, "%s", (char *)finalstr);
#endif

    cxxptr<SyncItem> item(new SyncItem(), "SyncItem");
    item->setKey(luid);
    item->setData(vcardstr.get(), strlen(vcardstr.get()));
    return item.release();
}

string EvolutionContactSource::preparseVCard(SyncItem& item)
{
    return item.getData();

#if 0
    // @TODO Synthesis data conversion
    string data = (const char *)item.getData();
    // convert to 3.0 to get rid of quoted-printable encoded
    // non-ASCII chars, because Evolution does not support
    // decoding them
    SE_LOG_DEBUG(this, NULL, "%s", data.c_str());
    std::auto_ptr<VObject> vobj(VConverter::parse((char *)data.c_str()));
    if (vobj.get() == 0) {
        throwError(string("failure parsing contact " ) + item.getKey());
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
            bool isOther = false;
            if (type) {
                if (!strcasecmp(type, "PARCEL")) {
                    // remove unsupported TYPE=PARCEL that was
                    // added in createItem() and replace with "OTHER" to be symetric
                    vprop->removeParameter("TYPE");
                    isOther = true;
                } else if (!strcasecmp(type, "PREF,VOICE")) {
                    // this is not mapped by Evolution to "Primary Phone",
                    // help a little bit
                    vprop->removeParameter("TYPE");
                    vprop->addParameter("TYPE", "PREF");
                } else if (strchr(type, ',')) {
                    // Evolution cannot handle e.g. "WORK,VOICE". Split into
                    // different parts.
                    string buffer = type, value;
                    size_t start = 0, end;
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

                /* TEL */
                !vprop->containsParameter("CELL") &&
                !vprop->containsParameter("CAR") &&
                !vprop->containsParameter("PREF") &&
                !vprop->containsParameter("FAX") &&
                !vprop->containsParameter("VOICE") &&
                !vprop->containsParameter("MSG") &&
                !vprop->containsParameter("BBS") &&
                !vprop->containsParameter("MODEM") &&
                !vprop->containsParameter("ISDN") &&
                !vprop->containsParameter("VIDEO") &&
                !vprop->containsParameter("PAGER") &&

                /* ADR */
                !vprop->containsParameter("DOM") &&
                !vprop->containsParameter("INTL") &&
                !vprop->containsParameter("POSTAL") &&
                !vprop->containsParameter("PARCEL") &&

                /* EMAIL */
                !vprop->containsParameter("AOL") &&
                !vprop->containsParameter("AppleLink") &&
                !vprop->containsParameter("ATTMail") &&
                !vprop->containsParameter("CIS") &&
                !vprop->containsParameter("eWorld") &&
                !vprop->containsParameter("INTERNET") &&
                !vprop->containsParameter("IBMMail") &&
                !vprop->containsParameter("MCIMail") &&
                !vprop->containsParameter("POWERSHARE") &&
                !vprop->containsParameter("PRODIGY") &&
                !vprop->containsParameter("TLX") &&
                !vprop->containsParameter("X400") &&

                /* all of them */
                !vprop->containsParameter("HOME") &&
                !vprop->containsParameter("WORK")) {
                vprop->addParameter("TYPE", isOther ? "OTHER" : "HOME");
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
    SE_LOG_DEBUG(this, NULL, "after conversion to 3.0:");
    SE_LOG_DEBUG(this, NULL, "%s", data.c_str());
    return data;
#endif
}

TrackingSyncSource::InsertItemResult
EvolutionContactSource::insertItem(const string &uid, const SyncItem& item)
{
    eptr<EContact, GObject> contact(e_contact_new_from_vcard(item.getData()));
    if (contact) {
        GError *gerror = NULL;
        e_contact_set(contact, E_CONTACT_UID,
                      uid.empty() ?
                      NULL :
                      const_cast<char *>(uid.c_str()));
        if (uid.empty() ?
            e_book_add_contact(m_addressbook, contact, &gerror) :
            e_book_commit_contact(m_addressbook, contact, &gerror)) {
            const char *newuid = (const char *)e_contact_get_const(contact, E_CONTACT_UID);
            if (!newuid) {
                throwError("no UID for contact");
            }
            string newrev = getRevision(newuid);
            return InsertItemResult(newuid, newrev, false);
        } else {
            throwError(uid.empty() ?
                       "storing new contact" :
                       string("updating contact ") + uid,
                       gerror);
        }
    } else {
        throwError(string("failure parsing vcard " ) + item.getData());
    }
    // not reached!
    return InsertItemResult("", "", false);
}

void EvolutionContactSource::deleteItem(const string &uid)
{
    GError *gerror = NULL;
    if (!e_book_remove_contact(m_addressbook, uid.c_str(), &gerror)) {
        if (gerror->domain == E_BOOK_ERROR &&
            gerror->code == E_BOOK_ERROR_CONTACT_NOT_FOUND) {
            SE_LOG_DEBUG(this, NULL, "%s: %s: request to delete non-existant contact ignored",
                         getName(), uid.c_str());
            g_clear_error(&gerror);
        } else {
            throwError( string( "deleting contact " ) + uid,
                        gerror );
        }
    }
}

const char *EvolutionContactSource::getMimeType() const
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

const char *EvolutionContactSource::getMimeVersion() const
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
    if (getLevel() >= (debug ? Logger::DEBUG : Logger::INFO)) {
        string line;
        EContact *contact;
        GError *gerror = NULL;

        if (e_book_get_contact( m_addressbook,
                                uid.c_str(),
                                &contact,
                                &gerror )) {
            eptr<EContact, GObject> contactptr(contact);

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
            g_clear_error(&gerror);
            line += "<name unavailable>";
        }
        line += " (";
        line += uid;
        line += "): ";
        line += info;
        
        SE_LOG(debug ? Logger::DEBUG : Logger::INFO, this, NULL, "%s", line.c_str() );
    }
}

void EvolutionContactSource::logItem(const SyncItem &item, const string &info, bool debug)
{
    if (getLevel() >= (debug ? Logger::DEBUG : Logger::INFO)) {
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
            // accept both "\r\n" and "\n" as line termination:
            // "\r\n" is the standard, but MemoToo does not follow that
            int len = vcard.find_first_of("\r\n", offset) - offset - 3;
            line += vcard.substr( offset + 3, len );
        } else {
            line += "<unnamed contact>";
        }

        if (item.getKey().empty()) {
            line += ", empty UID";
        } else {
            line += ", ";
            line += item.getKey();
        
            EContact *contact;
            GError *gerror = NULL;
            if (e_book_get_contact(m_addressbook,
                                   item.getKey().c_str(),
                                   &contact,
                                   &gerror)) {
                eptr<EContact, GObject> contactptr( contact, "contact" );

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
        
        SE_LOG(debug ? Logger::DEBUG : Logger::INFO, this, NULL, "%s", line.c_str() );
    }
}

#endif /* ENABLE_EBOOK */

#ifdef ENABLE_MODULES
# include "EvolutionContactSourceRegister.cpp"
#endif
