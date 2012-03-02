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

#include <syncevo/SyncContext.h>
#include "EvolutionContactSource.h"
#include <syncevo/util.h>

#include <syncevo/Logging.h>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/foreach.hpp>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

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

EvolutionContactSource::EvolutionContactSource(const SyncSourceParams &params,
                                               EVCardFormat vcardFormat) :
    EvolutionSyncSource(params),
    m_vcardFormat(vcardFormat)
{
    SyncSourceLogging::init(InitList<std::string>("N_FIRST") + "N_MIDDLE" + "N_LAST",
                            " ",
                            m_operations);
}

EvolutionSyncSource::Databases EvolutionContactSource::getDatabases()
{
    ESourceList *sources = NULL;

    if (!e_book_get_addressbooks(&sources, NULL)) {
        SyncContext::throwError("unable to access address books");
    }

    Databases secondary;
    Databases result;
    for (GSList *g = e_source_list_peek_groups (sources); g; g = g->next) {
        ESourceGroup *group = E_SOURCE_GROUP (g->data);
        for (GSList *s = e_source_group_peek_sources (group); s; s = s->next) {
            ESource *source = E_SOURCE (s->data);
            eptr<char> uri(e_source_get_uri(source));
            string uristr;
            if (uri) {
                uristr = uri.get();
            }
            Database entry(e_source_peek_name(source),
                           uristr,
                           false);
            if (boost::starts_with(uristr, "couchdb://")) {
                // Append CouchDB address books at the end of the list,
                // otherwise preserving the order of address books.
                //
                // The reason is Moblin Bugzilla #7877 (aka CouchDB
                // feature request #479110): the initial release of
                // evolution-couchdb in Ubuntu 9.10 is unusable because
                // it does not support the REV property.
                //
                // Reordering the entries ensures that the CouchDB
                // address book is not used as the default database by
                // SyncEvolution, as it happened in Ubuntu 9.10.
                // Users can still pick it intentionally via
                // "evolutionsource".
                secondary.push_back(entry);
            } else {
                result.push_back(entry);
            }
        }
    }
    result.insert(result.end(), secondary.begin(), secondary.end());

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
    } else {
        //  the first DB found is the default
        result[0].m_isDefault = true;
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
    bool onlyIfExists = false; // always try to create address book, because even if there is
                               // a source there's no guarantee that the actual database was
                               // created already; the original logic below for only setting
                               // this when explicitly requesting a new address book
                               // therefore failed in some cases
    bool created = false;
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
        created = true;
        onlyIfExists = false;
    } else {
        m_addressbook.set( e_book_new( source, &gerror ), "address book" );
    }
 
    if (!e_book_open( m_addressbook, onlyIfExists, &gerror) ) {
        if (created) {
            // opening newly created address books often fails, try again once more
            g_clear_error(&gerror);
            sleep(5);
            if (!e_book_open(m_addressbook, onlyIfExists, &gerror)) {
                throwError("opening address book", gerror);
            }
        } else {
            throwError("opening address book", gerror);
        }
    }

    // users are not expected to configure an authentication method,
    // so pick one automatically if the user indicated that he wants authentication
    // by setting user or password
    std::string user = getUser(),
        passwd = getPassword();
    if (!user.empty() || !passwd.empty()) {
        GList *authmethod;
        if (!e_book_get_supported_auth_methods(m_addressbook, &authmethod, &gerror)) {
            throwError("getting authentication methods", gerror );
        }
        while (authmethod) {
            const char *method = (const char *)authmethod->data;
            SE_LOG_DEBUG(this, NULL, "trying authentication method \"%s\", user %s, password %s",
                         method,
                         !user.empty() ? "configured" : "not configured",
                         !passwd.empty() ? "configured" : "not configured");
            if (e_book_authenticate_user(m_addressbook,
                                         user.c_str(),
                                         passwd.c_str(),
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
                           G_CALLBACK(SyncContext::fatalError),
                           (void *)"Evolution Data Server has died unexpectedly, contacts no longer available.");
}

bool EvolutionContactSource::isEmpty()
{
    // TODO: add more efficient implementation which does not
    // depend on actually pulling all items from EDS
    RevisionMap_t revisions;
    listAllItems(revisions);
    return revisions.empty();
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
    m_addressbook = NULL;
}

string EvolutionContactSource::getRevision(const string &luid)
{
    EContact *contact;
    GError *gerror = NULL;
    if (!e_book_get_contact(m_addressbook,
                            luid.c_str(),
                            &contact,
                            &gerror)) {
        if (gerror && gerror->domain == E_BOOK_ERROR && gerror->code == E_BOOK_ERROR_CONTACT_NOT_FOUND) {
            g_clear_error(&gerror);
            throwError(STATUS_NOT_FOUND, string("retrieving item: ") + luid);
        } else {
            throwError(string("reading contact ") + luid,
                       gerror);
        }
    }
    eptr<EContact, GObject> contactptr(contact, "contact");
    const char *rev = (const char *)e_contact_get_const(contact,
                                                        E_CONTACT_REV);
    if (!rev || !rev[0]) {
        throwError(string("contact entry without REV: ") + luid);
    }
    return rev;
}

void EvolutionContactSource::readItem(const string &luid, std::string &item, bool raw)
{
    EContact *contact;
    GError *gerror = NULL;
    if (!e_book_get_contact(m_addressbook,
                            luid.c_str(),
                            &contact,
                            &gerror)) {
        if (gerror &&
            gerror->domain == E_BOOK_ERROR &&
            gerror->code == E_BOOK_ERROR_CONTACT_NOT_FOUND) {
            g_clear_error(&gerror);
            throwError(STATUS_NOT_FOUND, string("reading contact: ") + luid);
        } else {
            throwError(string("reading contact ") + luid,
                       gerror);
        }
    }
    eptr<EContact, GObject> contactptr(contact, "contact");

    // Inline PHOTO data if exporting, leave VALUE=uri references unchanged
    // when processing inside engine (will be inlined by engine as needed).
    // The function for doing the inlining was added in EDS 3.4.
    // In compatibility mode, we must check the function pointer for non-NULL.
    // In direct call mode, the existence check is done by configure.
    if (raw
#ifdef EVOLUTION_COMPATIBILITY
        && e_contact_inline_local_photos
#endif
        ) {
#if defined(EVOLUTION_COMPATIBILITY) || defined(HAVE_E_CONTACT_INLINE_LOCAL_PHOTOS)
        if (!e_contact_inline_local_photos(contactptr, &gerror)) {
            throwError(string("inlining PHOTO file data in ") + luid, gerror);
        }
#endif
    }

    eptr<char> vcardstr(e_vcard_to_string(&contactptr->parent,
                                          EVC_FORMAT_VCARD_30));
    if (!vcardstr) {
        throwError(string("failure extracting contact from Evolution " ) + luid);
    }

    item = vcardstr.get();
}

TrackingSyncSource::InsertItemResult
EvolutionContactSource::insertItem(const string &uid, const std::string &item, bool raw)
{
    eptr<EContact, GObject> contact(e_contact_new_from_vcard(item.c_str()));
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
            return InsertItemResult(newuid, newrev, ITEM_OKAY);
        } else {
            throwError(uid.empty() ?
                       "storing new contact" :
                       string("updating contact ") + uid,
                       gerror);
        }
    } else {
        throwError(string("failure parsing vcard " ) + item);
    }
    // not reached!
    return InsertItemResult("", "", ITEM_OKAY);
}

void EvolutionContactSource::removeItem(const string &uid)
{
    GError *gerror = NULL;
    if (!e_book_remove_contact(m_addressbook, uid.c_str(), &gerror)) {
        if (gerror &&
            gerror->domain == E_BOOK_ERROR &&
            gerror->code == E_BOOK_ERROR_CONTACT_NOT_FOUND) {
            g_clear_error(&gerror);
            throwError(STATUS_NOT_FOUND, string("deleting contact: ") + uid);
        } else {
            throwError( string( "deleting contact " ) + uid,
                        gerror );
        }
    }
}

std::string EvolutionContactSource::getDescription(const string &luid)
{
    try {
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
        const char *name = (const char *)e_contact_get_const(contact, E_CONTACT_FULL_NAME);
        if (name) {
            return name;
        }
        const char *fileas = (const char *)e_contact_get_const(contact, E_CONTACT_FILE_AS);
        if (fileas) {
            return fileas;
        }
        EContactName *names =
            (EContactName *)e_contact_get(contact, E_CONTACT_NAME);
        std::list<std::string> buffer;
        if (names) {
            try {
                if (names->given && names->given[0]) {
                    buffer.push_back(names->given);
                }
                if (names->additional && names->additional[0]) {
                    buffer.push_back(names->additional);
                }
                if (names->family && names->family[0]) {
                    buffer.push_back(names->family);
                }
            } catch (...) {
            }
            e_contact_name_free(names);
        }
        return boost::join(buffer, " ");
    } catch (...) {
        // Instead of failing we log the error and ask
        // the caller to log the UID. That way transient
        // errors or errors in the logging code don't
        // prevent syncs.
        handleException();
        return "";
    }
}

std::string EvolutionContactSource::getMimeType() const
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

std::string EvolutionContactSource::getMimeVersion() const
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

SE_END_CXX

#endif /* ENABLE_EBOOK */

#ifdef ENABLE_MODULES
# include "EvolutionContactSourceRegister.cpp"
#endif
