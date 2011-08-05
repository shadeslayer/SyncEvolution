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

#ifdef USE_EBOOK_CLIENT
    if (!e_book_client_get_sources(&sources, NULL)) {
#else
    if (!e_book_get_addressbooks(&sources, NULL)) {
#endif
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
#ifdef USE_EBOOK_CLIENT
        EBookClientCXX book;
        const char *name;

        name = "<<system>>";
        book = EBookClientCXX::steal(e_book_client_new_system (NULL));
        if (!book) {
            name = "<<default>>";
            book = EBookClientCXX::steal(e_book_client_new_default (NULL));
        }

        if (book) {
            const char *uri = e_client_get_uri (E_CLIENT ((EBookClient*)book));
            result.push_back(Database(name, uri, true));
        }
#else
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
#endif
    } else {
        //  the first DB found is the default
        result[0].m_isDefault = true;
    }
    
    return result;
}

#ifdef USE_EBOOK_CLIENT
static void
handle_error_cb (EClient */*client*/, const gchar *error_msg, gpointer user_data)
{
    EvolutionContactSource *that = static_cast<EvolutionContactSource *>(user_data);
    SE_LOG_ERROR(that, NULL, error_msg);
}

static gboolean
handle_authentication_cb (EClient */*client*/, ECredentials *credentials, gpointer user_data)
{
    EvolutionContactSource *that = static_cast<EvolutionContactSource *>(user_data);
    std::string user = that->getUser();
    std::string passwd = that->getPassword();

    if (!user.empty() || !passwd.empty()) {
        e_credentials_set (credentials, E_CREDENTIALS_KEY_USERNAME, user.c_str());
        e_credentials_set (credentials, E_CREDENTIALS_KEY_PASSWORD, passwd.c_str());
        return true;
    } else {
        return false;
    }
}
#endif

void EvolutionContactSource::open()
{
#ifdef USE_EBOOK_CLIENT
    ESourceList *sources;

    if (!e_book_client_get_sources(&sources, NULL)) {
        throwError("unable to access address books");
    }
    
    string id = getDatabaseID();
    ESource *source = findSource(sources, id);
    bool onlyIfExists = true;
    bool created = false;
    GErrorCXX gerror;
    if (!source) {
        // might have been special "<<system>>" or "<<default>>", try that and
        // creating address book from file:// URI before giving up
        if (id.empty() || id == "<<system>>") {
            m_addressbook = EBookClientCXX::steal(e_book_client_new_system (gerror));
        } else if (id.empty() || id == "<<default>>") {
            m_addressbook = EBookClientCXX::steal(e_book_client_new_default (gerror));
        } else if (boost::starts_with(id, "file://")) {
            m_addressbook = EBookClientCXX::steal(e_book_client_new_from_uri(id.c_str(), gerror));
        } else {
            throwError(string(getName()) + ": no such address book: '" + id + "'");
        }
        created = true;
        onlyIfExists = false;
    } else {
        m_addressbook = EBookClientCXX::steal(e_book_client_new( source, gerror ));
    }

    if (!gerror.isNull()) {
        gerror.throwError("create addressbook");
    }

    // Listen for errors
    g_signal_connect (m_addressbook, "backend-error", G_CALLBACK (handle_error_cb), this); 

    // Handle authentication requests from the backend
    g_signal_connect (m_addressbook, "authenticate", G_CALLBACK (handle_authentication_cb), this);
 
    // Open the address book
    if (!e_client_open_sync( E_CLIENT ((EBookClient*)m_addressbook), onlyIfExists, NULL, gerror) ) {
        if (created) {
            // opening newly created address books often fails, try again once more
            gerror.clear();
            sleep(5);
            if (!e_client_open_sync( E_CLIENT ((EBookClient*)m_addressbook), onlyIfExists, NULL, gerror)) {
                gerror.throwError("opening address book");
            }
        } else {
            gerror.throwError("opening address book");
        }
    }

    g_signal_connect_after(m_addressbook,
                           "backend-died",
                           G_CALLBACK(SyncContext::fatalError),
                           (void *)"Evolution Data Server has died unexpectedly, contacts no longer available.");
#else
    ESourceList *sources;
    if (!e_book_get_addressbooks(&sources, NULL)) {
        throwError("unable to access address books");
    }
    
    GError *gerror = NULL;
    string id = getDatabaseID();
    ESource *source = findSource(sources, id);
    bool onlyIfExists = true;
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
#endif
}

bool EvolutionContactSource::isEmpty()
{
    // TODO: add more efficient implementation which does not
    // depend on actually pulling all items from EDS
    RevisionMap_t revisions;
    listAllItems(revisions);
    return revisions.empty();
}

#ifdef USE_EBOOK_CLIENT
class EBookClientViewSyncHandler {
    public:
        EBookClientViewSyncHandler(EBookClientView *view, 
                                   void (*processList)(const GSList *list, void *user_data), 
                                   void *user_data): 
            m_view(view), m_processList(processList), m_userData(user_data) {}

        bool process(GError **gerror) {
                 // Listen for view signals
            g_signal_connect(m_view, "objects-added", G_CALLBACK(contactsAdded), this);
            g_signal_connect(m_view, "complete", G_CALLBACK(completed), this);

            // Start the view
            e_book_client_view_start (m_view, m_error);
            if (!m_error.isNull()) {
                g_propagate_error(gerror, m_error);
                return false;
            }

            // Async -> Sync
            m_loop.run();
            e_book_client_view_stop (m_view, NULL); 

            if (!m_error.isNull()) {
                g_propagate_error(gerror, m_error);
                return false;
            } else {
                return true;
            }
        }
     
        static void contactsAdded(EBookClientView *ebookview,
                                  const GSList *contacts,
                                  gpointer user_data) {
            EBookClientViewSyncHandler *that = (EBookClientViewSyncHandler *)user_data;
            that->m_processList(contacts, that->m_userData);
        }
 
        static void completed(EBookClientView *ebookview,
                              const GError *error,
                              gpointer user_data) {
            EBookClientViewSyncHandler *that = (EBookClientViewSyncHandler *)user_data;
            that->m_error = error;
            that->m_loop.quit();
        }

    public:
         // Process list callback
         void (*m_processList)(const GSList *contacts, void *user_data);
         void *m_userData;
        // Event loop for Async -> Sync
        EvolutionAsync m_loop;

    private:
        // View watched
        EBookClientView *m_view;
        // Possible error while watching the view
        GErrorCXX m_error;
};

static void list_revisions(const GSList *contacts, void *user_data)
{
    EvolutionContactSource::RevisionMap_t *revisions = 
        static_cast<EvolutionContactSource::RevisionMap_t *>(user_data);
    const GSList *l;

    for (l = contacts; l; l = l->next) {
        EContact *contact = E_CONTACT(l->data);
        if (!contact) {
            SE_THROW("contact entry without data");
        }
        pair<string, string> revmapping;
        const char *uid = (const char *)e_contact_get_const(contact,
                                                            E_CONTACT_UID);
        if (!uid || !uid[0]) {
            SE_THROW("contact entry without UID");
        }
        revmapping.first = uid;
        const char *rev = (const char *)e_contact_get_const(contact,
                                                            E_CONTACT_REV);
        if (!rev || !rev[0]) {
            SE_THROW(string("contact entry without REV: ") + revmapping.first);
        }
        revmapping.second = rev;
        revisions->insert(revmapping);
    }
}

#endif

void EvolutionContactSource::listAllItems(RevisionMap_t &revisions)
{
#ifdef USE_EBOOK_CLIENT
    GErrorCXX gerror;
    EBookClientView *view;

    EBookQueryCXX allItemsQuery = EBookQueryCXX::steal(e_book_query_any_field_contains(""));
    PlainGStr sexp(e_book_query_to_string (allItemsQuery));
    
    if (!e_book_client_get_view_sync(m_addressbook, sexp, &view, NULL, gerror)) {
        gerror.throwError( "getting the view" );
    }
    EBookClientViewCXX viewPtr = EBookClientViewCXX::steal(view);

    // Optimization: set fields_of_interest (UID / REV)
    GListCXX<const char, GSList> interesting_field_list;
    interesting_field_list.push_back(e_contact_field_name (E_CONTACT_UID));
    interesting_field_list.push_back(e_contact_field_name (E_CONTACT_REV));
    e_book_client_view_set_fields_of_interest (viewPtr, interesting_field_list, gerror);
    if (!gerror.isNull()) {
        SE_LOG_ERROR(this, NULL, "e_book_client_view_set_fields_of_interest: %s", (const char*)gerror);
        gerror.clear();
    }

    EBookClientViewSyncHandler handler(viewPtr, list_revisions, &revisions);
    if (!handler.process(gerror)) {
        gerror.throwError("watching view");
    }
#else
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
#endif
}

void EvolutionContactSource::close()
{
    m_addressbook = NULL;
}

string EvolutionContactSource::getRevision(const string &luid)
{
    EContact *contact;
#ifdef USE_EBOOK_CLIENT
    GErrorCXX gerror;
    if (!e_book_client_get_contact_sync(m_addressbook,
                                        luid.c_str(),
                                        &contact,
                                        NULL,
                                        gerror)) {
        gerror.throwError(string("reading contact ") + luid);
    }
#else
    GError *gerror = NULL;
    if (!e_book_get_contact(m_addressbook,
                            luid.c_str(),
                            &contact,
                            &gerror)) {
        throwError(string("reading contact ") + luid,
                   gerror);
    }
#endif
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
#ifdef USE_EBOOK_CLIENT
    GErrorCXX gerror;
    if (!e_book_client_get_contact_sync(m_addressbook,
                                        luid.c_str(),
                                        &contact,
                                        NULL,
                                        gerror)) {
        gerror.throwError(string("reading contact ") + luid);
    }
#else
    GError *gerror = NULL;
    if (!e_book_get_contact(m_addressbook,
                            luid.c_str(),
                            &contact,
                            &gerror)) {
        throwError(string("reading contact ") + luid,
                   gerror);
    }
#endif
    eptr<EContact, GObject> contactptr(contact, "contact");
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
        e_contact_set(contact, E_CONTACT_UID,
                      uid.empty() ?
                      NULL :
                      const_cast<char *>(uid.c_str()));
#ifdef USE_EBOOK_CLIENT
        GErrorCXX gerror;
        if (uid.empty()) {
            gchar* newuid;
            if (!e_book_client_add_contact_sync(m_addressbook, contact, &newuid, NULL, gerror)) {
                gerror.throwError("add new contact");
            }
            PlainGStr newuidPtr(newuid);
            string newrev = getRevision(newuid);
            return InsertItemResult(newuid, newrev, false);
        } else {
            if (!e_book_client_modify_contact_sync(m_addressbook, contact, NULL, gerror)) {
                gerror.throwError("modify existing contact: "+ uid);
            }
            string newrev = getRevision(uid);
            return InsertItemResult(uid, newrev, false);
        }
#else
        GError *gerror = NULL;
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
#endif
    } else {
        throwError(string("failure parsing vcard " ) + item);
    }
    // not reached!
    return InsertItemResult("", "", false);
}

void EvolutionContactSource::removeItem(const string &uid)
{
#ifdef USE_EBOOK_CLIENT
    GErrorCXX gerror;
    if (!e_book_client_remove_contact_by_uid_sync(m_addressbook, uid.c_str(), NULL, gerror)) {
        if (gerror->domain == E_BOOK_CLIENT_ERROR &&
            gerror->code == E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND) {
            SE_LOG_DEBUG(this, NULL, "%s: request to delete non-existant contact ignored",
                         uid.c_str());
        } else {
            gerror.throwError( string( "deleting contact " ) + uid );
        }
    }
#else
    GError *gerror = NULL;
    if (!e_book_remove_contact(m_addressbook, uid.c_str(), &gerror)) {
        if (gerror->domain == E_BOOK_ERROR &&
            gerror->code == E_BOOK_ERROR_CONTACT_NOT_FOUND) {
            SE_LOG_DEBUG(this, NULL, "%s: request to delete non-existant contact ignored",
                         uid.c_str());
            g_clear_error(&gerror);
        } else {
            throwError( string( "deleting contact " ) + uid,
                        gerror );
        }
    }
#endif
}

std::string EvolutionContactSource::getDescription(const string &luid)
{
    try {
        EContact *contact;
#ifdef USE_EBOOK_CLIENT
        GErrorCXX gerror;          
        if (!e_book_client_get_contact_sync(m_addressbook,
                                        luid.c_str(),
                                        &contact,
                                        NULL,
                                        gerror)) {
            gerror.throwError(string("reading contact ") + luid);
        }
#else
        GError *gerror = NULL;
        if (!e_book_get_contact(m_addressbook,
                                luid.c_str(),
                                &contact,
                                &gerror)) {
            throwError(string("reading contact ") + luid,
                       gerror);
        }
#endif
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
