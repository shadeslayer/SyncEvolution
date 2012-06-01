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
using namespace std;

#include "config.h"

#ifdef ENABLE_ECAL

// include first, it sets HANDLE_LIBICAL_MEMORY for us
#include <syncevo/icalstrdup.h>

#include <syncevo/SyncContext.h>
#include <syncevo/SmartPtr.h>
#include <syncevo/Logging.h>

#include "EvolutionCalendarSource.h"
#include "EvolutionMemoSource.h"
#include "e-cal-check-timezones.h"


#include <boost/foreach.hpp>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

static const string
EVOLUTION_CALENDAR_PRODID("PRODID:-//ACME//NONSGML SyncEvolution//EN"),
EVOLUTION_CALENDAR_VERSION("VERSION:2.0");

bool EvolutionCalendarSource::LUIDs::containsLUID(const ItemID &id) const
{
    const_iterator it = findUID(id.m_uid);
    return it != end() &&
        it->second.find(id.m_rid) != it->second.end();
}

void EvolutionCalendarSource::LUIDs::insertLUID(const ItemID &id)
{
    (*this)[id.m_uid].insert(id.m_rid);
}

void EvolutionCalendarSource::LUIDs::eraseLUID(const ItemID &id)
{
    iterator it = find(id.m_uid);
    if (it != end()) {
        set<string>::iterator it2 = it->second.find(id.m_rid);
        if (it2 != it->second.end()) {
            it->second.erase(it2);
            if (it->second.empty()) {
                erase(it);
            }
        }
    }
}

static int granularity()
{
    // This long delay is necessary in combination
    // with Evolution Exchange Connector: when updating
    // a child event, it seems to take a while until
    // the change really is effective.
    static int secs = 5;
    static bool checked = false;
    if (!checked) {
        // allow setting the delay (used during testing to shorten runtime)
        const char *delay = getenv("SYNC_EVOLUTION_EVO_CALENDAR_DELAY");
        if (delay) {
            secs = atoi(delay);
        }
        checked = true;
    }
    return secs;
}

EvolutionCalendarSource::EvolutionCalendarSource(EvolutionCalendarSourceType type,
                                                 const SyncSourceParams &params) :
    EvolutionSyncSource(params, granularity()),
    m_type(type)
{
    switch (m_type) {
     case EVOLUTION_CAL_SOURCE_TYPE_EVENTS:
        SyncSourceLogging::init(InitList<std::string>("SUMMARY") + "LOCATION",
                                ", ",
                                m_operations);
        m_typeName = "calendar";
#ifndef USE_ECAL_CLIENT
        m_newSystem = e_cal_new_system_calendar;
#endif
        break;
     case EVOLUTION_CAL_SOURCE_TYPE_TASKS:
        SyncSourceLogging::init(InitList<std::string>("SUMMARY"),
                                ", ",
                                m_operations);
        m_typeName = "task list";
#ifndef USE_ECAL_CLIENT
        m_newSystem = e_cal_new_system_tasks;
#endif
        break;
     case EVOLUTION_CAL_SOURCE_TYPE_MEMOS:
        SyncSourceLogging::init(InitList<std::string>("SUBJECT"),
                                ", ",
                                m_operations);
        m_typeName = "memo list";
#ifndef USE_ECAL_CLIENT
        // This is not available in older Evolution versions.
        // A configure check could detect that, but as this isn't
        // important the functionality is simply disabled.
        m_newSystem = NULL /* e_cal_new_system_memos */;
#endif
        break;
     default:
        SyncContext::throwError("internal error, invalid calendar type");
        break;
    }
}

SyncSource::Databases EvolutionCalendarSource::getDatabases()
{
    ESourceList *tmp = NULL;
    GErrorCXX gerror;
    Databases result;

    if (
#ifdef USE_ECAL_CLIENT
        !e_cal_client_get_sources(&tmp, sourceType(), gerror)
#else
        !e_cal_get_sources(&tmp, sourceType(), gerror)
#endif
        ) {
        // ignore unspecific errors (like on Maemo with no support for memos)
        // and continue with empty list (perhaps defaults work)
        if (!gerror) {
            tmp = NULL;
        } else {
            throwError("unable to access backend databases", gerror);
        }
    }
    ESourceListCXX sources(tmp, false);

    bool first = true;
    for (GSList *g = sources ? e_source_list_peek_groups (sources) : NULL;
         g;
         g = g->next) {
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

#ifdef USE_ECAL_CLIENT
    if (result.empty()) {
        ECalClientCXX calendar = ECalClientCXX::steal(e_cal_client_new_system(sourceType(), NULL));
        if (calendar) {
          const char *uri = e_client_get_uri (E_CLIENT ((ECalClient*)calendar));
          result.push_back(Database("<<system>>", uri ? uri : "<<unknown uri>>"));
        }
    }
#else
    if (result.empty() && m_newSystem) {
        eptr<ECal, GObject> calendar(m_newSystem());
        if (calendar.get()) {
            // okay, default system database exists
            const char *uri = e_cal_get_uri(calendar.get());
            result.push_back(Database("<<system>>", uri ? uri : "<<unknown uri>>"));
        }
    }
#endif

    return result;
}

#ifdef USE_ECAL_CLIENT
static void
handle_error_cb (EClient */*client*/, const gchar *error_msg, gpointer user_data)
{
    EvolutionCalendarSource *that = static_cast<EvolutionCalendarSource *>(user_data);
    SE_LOG_ERROR(that, NULL, "%s", error_msg);
}

static gboolean
handle_authentication_cb (EClient */*client*/, ECredentials *credentials, gpointer user_data)
{
    EvolutionCalendarSource *that = static_cast<EvolutionCalendarSource *>(user_data);
    std::string passwd = that->getPassword();
    std::string prompt = e_credentials_peek(credentials, E_CREDENTIALS_KEY_PROMPT_TEXT);
    std::string key = e_credentials_peek(credentials, E_CREDENTIALS_KEY_PROMPT_KEY);

    SE_LOG_DEBUG(that, NULL, "authentication requested, prompt \"%s\", key \"%s\" => %s",
                 prompt.c_str(), key.c_str(),
                 !passwd.empty() ? "returning configured password" : "no password configured");

    if (!passwd.empty()) {
        e_credentials_set (credentials, E_CREDENTIALS_KEY_PASSWORD, passwd.c_str());
        return true;
    } else {
        return false;
    }
}

#else

char *EvolutionCalendarSource::authenticate(const char *prompt,
                                            const char *key)
{
    std::string passwd = getPassword();

    SE_LOG_DEBUG(this, NULL, "authentication requested, prompt \"%s\", key \"%s\" => %s",
                 prompt, key,
                 !passwd.empty() ? "returning configured password" : "no password configured");
    return !passwd.empty() ? strdup(passwd.c_str()) : NULL;
}

#endif

void EvolutionCalendarSource::open()
{
    ESourceList *tmp;
    GErrorCXX gerror;
    bool onlyIfExists = false; // always try to create address book, because even if there is
                               // a source there's no guarantee that the actual database was
                               // created already; the original logic below for only setting
                               // this when explicitly requesting a new database
                               // therefore failed in some cases

#ifdef USE_ECAL_CLIENT
    if (!e_cal_client_get_sources (&tmp, sourceType(), gerror)) {
        throwError("unable to access backend databases", gerror);
    }
    ESourceListCXX sources(tmp, false);

    string id = getDatabaseID();    
    ESource *source = findSource(sources, id);
    bool created = false;

    // Open twice. This solves an issue where Evolution's CalDAV
    // backend only updates its local cache *after* a sync (= while
    // closing the calendar?), instead of doing it *before* a sync (in
    // e_cal_open()).
    //
    // This workaround is applied to *all* backends because there might
    // be others with similar problems and for local storage it is
    // a reasonably cheap operation (so no harm there).
    for (int retries = 0; retries < 2; retries++) {
        if (!source) {
            // might have been special "<<system>>" or "<<default>>", try that and
            // creating address book from file:// URI before giving up
            if ((id.empty() || id == "<<system>>")) {
                m_calendar = ECalClientCXX::steal(e_cal_client_new_system(sourceType(), gerror));
            } else if (!id.compare(0, 7, "file://")) {
                m_calendar = ECalClientCXX::steal(e_cal_client_new_from_uri(id.c_str(), sourceType(), gerror));
            } else {
                throwError(string("not found: '") + id + "'");
            }
            created = true;
        } else {
            m_calendar = ECalClientCXX::steal(e_cal_client_new(source, sourceType(), gerror));
        }

        if (gerror) {
            throwError("create calendar", gerror);
        }

        // Listen for errors
        g_signal_connect (m_calendar, "backend-error", G_CALLBACK (handle_error_cb), this); 

        // Handle authentication requests from the backend
        g_signal_connect (m_calendar, "authenticate", G_CALLBACK (handle_authentication_cb), this);
    
        if (!e_client_open_sync(E_CLIENT ((ECalClient*)m_calendar), onlyIfExists, NULL, gerror)) {
            if (created) {
                // opening newly created address books often failed, perhaps that also applies to calendars - try again
                gerror.clear();
                sleep(5);
                if (!e_client_open_sync(E_CLIENT ((ECalClient*)m_calendar), onlyIfExists, NULL, gerror)) {
                    throwError(string("opening ") + m_typeName , gerror);
                }
            } else {
                throwError(string("opening ") + m_typeName , gerror);
            }
        }
    }
#else
    if (!e_cal_get_sources(&tmp, sourceType(), gerror)) {
        throwError("unable to access backend databases", gerror);
    }
    ESourceListCXX sources(tmp, false);

    string id = getDatabaseID();    
    ESource *source = findSource(sources, id);
    bool created = false;

    // Open twice. This solves an issue where Evolution's CalDAV
    // backend only updates its local cache *after* a sync (= while
    // closing the calendar?), instead of doing it *before* a sync (in
    // e_cal_open()).
    //
    // This workaround is applied to *all* backends because there might
    // be others with similar problems and for local storage it is
    // a reasonably cheap operation (so no harm there).
    for (int retries = 0; retries < 2; retries++) {
        if (!source) {
            // might have been special "<<system>>" or "<<default>>", try that and
            // creating address book from file:// URI before giving up
            if ((id.empty() || id == "<<system>>") && m_newSystem) {
                m_calendar.set(m_newSystem(), (string("system ") + m_typeName).c_str());
            } else if (!id.compare(0, 7, "file://")) {
                m_calendar.set(e_cal_new_from_uri(id.c_str(), sourceType()), (string("creating ") + m_typeName).c_str());
            } else {
                throwError(string("not found: '") + id + "'");
            }
            created = true;
            onlyIfExists = false;
        } else {
            m_calendar.set(e_cal_new(source, sourceType()), m_typeName.c_str());
        }

        e_cal_set_auth_func(m_calendar, eCalAuthFunc, this);
    
        if (!e_cal_open(m_calendar, onlyIfExists, gerror)) {
            if (created) {
                // opening newly created address books often failed, perhaps that also applies to calendars - try again
                gerror.clear();
                sleep(5);
                if (!e_cal_open(m_calendar, onlyIfExists, gerror)) {
                    throwError(string("opening ") + m_typeName, gerror);
                }
            } else {
                throwError(string("opening ") + m_typeName, gerror);
            }
        }
    }

#endif

    g_signal_connect_after(m_calendar,
                           "backend-died",
                           G_CALLBACK(SyncContext::fatalError),
                           (void *)"Evolution Data Server has died unexpectedly, database no longer available.");
}

bool EvolutionCalendarSource::isEmpty()
{
    // TODO: add more efficient implementation which does not
    // depend on actually pulling all items from EDS
    RevisionMap_t revisions;
    listAllItems(revisions);
    return revisions.empty();
}

#ifdef USE_ECAL_CLIENT
class ECalClientViewSyncHandler {
  public:
    ECalClientViewSyncHandler(ECalClientView *view, void (*processList)(const GSList *list, void *user_data), void *user_data): 
        m_processList(processList), m_userData(user_data), m_view(view)
    {}

    bool processSync(GErrorCXX &gerror)
    {
        // Listen for view signals
        g_signal_connect(m_view, "objects-added", G_CALLBACK(objectsAdded), this);
        g_signal_connect(m_view, "complete", G_CALLBACK(completed), this);

        // Start the view
        e_cal_client_view_start (m_view, m_error);
        if (m_error) {
            std::swap(gerror, m_error);
            return false;
        }

        // Async -> Sync
        m_loop.run();
        e_cal_client_view_stop (m_view, NULL);

        if (m_error) {
            std::swap(gerror, m_error);
            return false;
        } else {
            return true;
        }
    }
 
    static void objectsAdded(ECalClientView *ebookview,
                             const GSList *objects,
                             gpointer user_data) {
        ECalClientViewSyncHandler *that = (ECalClientViewSyncHandler *)user_data;
        that->m_processList(objects, that->m_userData);
    }
 
    static void completed(ECalClientView *ebookview,
                          const GError *error,
                          gpointer user_data) {
        ECalClientViewSyncHandler *that = (ECalClientViewSyncHandler *)user_data;
        that->m_error = error;
        that->m_loop.quit();
    }
 
    public:
      // Process list callback
      void (*m_processList)(const GSList *list, void *user_data);
      void *m_userData;
      // Event loop for Async -> Sync
      EvolutionAsync m_loop;

    private:
      // View watched
      ECalClientView *m_view;

      // Possible error while watching the view
      GErrorCXX m_error;
};

static void list_revisions(const GSList *objects, void *user_data)
{
    EvolutionCalendarSource::RevisionMap_t *revisions = 
        static_cast<EvolutionCalendarSource::RevisionMap_t *>(user_data);
    const GSList *l;

    for (l = objects; l; l = l->next) {
        icalcomponent *icomp = (icalcomponent*)l->data;
        EvolutionCalendarSource::ItemID id = EvolutionCalendarSource::getItemID(icomp);
        string luid = id.getLUID();
        string modTime = EvolutionCalendarSource::getItemModTime(icomp);

        (*revisions)[luid] = modTime;
    }
}
#endif

void EvolutionCalendarSource::listAllItems(RevisionMap_t &revisions)
{
    GErrorCXX gerror;
#ifdef USE_ECAL_CLIENT
    ECalClientView *view;

    if (!e_cal_client_get_view_sync (m_calendar, "#t", &view, NULL, gerror)) {
        throwError( "getting the view" , gerror);
    }
    ECalClientViewCXX viewPtr = ECalClientViewCXX::steal(view);

    // TODO: Optimization: use set fields_of_interest (UID / REV / LAST-MODIFIED)

    ECalClientViewSyncHandler handler(viewPtr, list_revisions, &revisions);
    if (!handler.processSync(gerror)) {
        throwError("watching view", gerror);
    }

    // Update m_allLUIDs
    m_allLUIDs.clear();
    RevisionMap_t::iterator it;
    for(it = revisions.begin(); it != revisions.end(); it++) {
        m_allLUIDs.insertLUID(it->first);
    }
#else
    GList *nextItem;

    m_allLUIDs.clear();
    if (!e_cal_get_object_list_as_comp(m_calendar,
                                       "#t",
                                       &nextItem,
                                       gerror)) {
        throwError("reading all items", gerror);
    }
    eptr<GList> listptr(nextItem);
    while (nextItem) {
        ECalComponent *ecomp = E_CAL_COMPONENT(nextItem->data);
        ItemID id = getItemID(ecomp);
        string luid = id.getLUID();
        string modTime = getItemModTime(ecomp);

        m_allLUIDs.insertLUID(id);
        revisions[luid] = modTime;
        nextItem = nextItem->next;
    }
#endif
}

void EvolutionCalendarSource::close()
{
    m_calendar = NULL;
}

void EvolutionCalendarSource::readItem(const string &luid, std::string &item, bool raw)
{
    ItemID id(luid);
    item = retrieveItemAsString(id);
}

#ifdef USE_ECAL_CLIENT
icaltimezone *
my_tzlookup(const gchar *tzid,
            gconstpointer ecalclient,
            GCancellable *cancellable,
            GError **error)
{
    icaltimezone *zone = NULL;
    GError *local_error = NULL;

    if (e_cal_client_get_timezone_sync((ECalClient *)ecalclient, tzid, &zone, cancellable, &local_error)) {
        return zone;
    } else if (local_error && local_error->domain == E_CAL_CLIENT_ERROR) {
        // Ignore *all* E_CAL_CLIENT_ERROR errors, e_cal_client_get_timezone_sync() does
        // not reliably return a specific code like E_CAL_CLIENT_ERROR_OBJECT_NOT_FOUND.
        // See the 'e_cal_client_check_timezones() + e_cal_client_tzlookup() + Could not retrieve calendar time zone: Invalid object'
        // mail thread.
        g_clear_error (&local_error);
    } else if (local_error) {
        g_propagate_error (error, local_error);
    }

    return NULL;
}
#endif

EvolutionCalendarSource::InsertItemResult EvolutionCalendarSource::insertItem(const string &luid, const std::string &item, bool raw)
{
    bool update = !luid.empty();
    InsertItemResultState state = ITEM_OKAY;
    bool detached = false;
    string newluid = luid;
    string data = item;
    string modTime;

    /*
     * Evolution/libical can only deal with \, as separator.
     * Replace plain , in incoming event CATEGORIES with \, -
     * based on simple text search/replace and thus will not work
     * in all cases...
     *
     * Inverse operation in extractItemAsString().
     */
    size_t propstart = data.find("\nCATEGORIES");
    bool modified = false;
    while (propstart != data.npos) {
        size_t eol = data.find('\n', propstart + 1);
        size_t comma = data.find(',', propstart);

        while (eol != data.npos &&
               comma != data.npos &&
               comma < eol) {
            if (data[comma-1] != '\\') {
                data.insert(comma, "\\");
                comma++;
                modified = true;
            }
            comma = data.find(',', comma + 1);
        }
        propstart = data.find("\nCATEGORIES", propstart + 1);
    }
    if (modified) {
        SE_LOG_DEBUG(this, NULL, "after replacing , with \\, in CATEGORIES:\n%s", data.c_str());
    }

    eptr<icalcomponent> icomp(icalcomponent_new_from_string((char *)data.c_str()));

    if( !icomp ) {
        throwError(string("failure parsing ical") + data);
    }

    GErrorCXX gerror;

    // fix up TZIDs
    if (
#ifdef USE_ECAL_CLIENT
        !e_cal_client_check_timezones(icomp,
                                      NULL,
                                      my_tzlookup,
                                      (const void *)m_calendar.get(),
                                      NULL,
                                      gerror)
#else
        !e_cal_check_timezones(icomp,
                               NULL,
                               e_cal_tzlookup_ecal,
                               (const void *)m_calendar.get(),
                               gerror)
#endif
        ) {
        throwError(string("fixing timezones") + data,
                   gerror);
    }

    // insert before adding/updating the event so that the new VTIMEZONE is
    // immediately available should anyone want it
    for (icalcomponent *tcomp = icalcomponent_get_first_component(icomp, ICAL_VTIMEZONE_COMPONENT);
         tcomp;
         tcomp = icalcomponent_get_next_component(icomp, ICAL_VTIMEZONE_COMPONENT)) {
        eptr<icaltimezone> zone(icaltimezone_new(), "icaltimezone");
        icaltimezone_set_component(zone, tcomp);

        GErrorCXX gerror;
        const char *tzid = icaltimezone_get_tzid(zone);
        if (!tzid || !tzid[0]) {
            // cannot add a VTIMEZONE without TZID
            SE_LOG_DEBUG(this, NULL, "skipping VTIMEZONE without TZID");
        } else {
            gboolean success =
#ifdef USE_ECAL_CLIENT
                e_cal_client_add_timezone_sync(m_calendar, zone, NULL, gerror)
#else
                e_cal_add_timezone(m_calendar, zone, gerror)
#endif
                ;
            if (!success) {
                throwError(string("error adding VTIMEZONE ") + tzid,
                           gerror);
            }
        }
    }

    // the component to update/add must be the
    // ICAL_VEVENT/VTODO_COMPONENT of the item,
    // e_cal_create/modify_object() fail otherwise
    icalcomponent *subcomp = icalcomponent_get_first_component(icomp,
                                                               getCompType());
    if (!subcomp) {
        throwError("extracting event");
    }

    // Remove LAST-MODIFIED: the Evolution Exchange Connector does not
    // properly update this property if it is already present in the
    // incoming data.
    icalproperty *modprop;
    while ((modprop = icalcomponent_get_first_property(subcomp, ICAL_LASTMODIFIED_PROPERTY)) != NULL) {
        icalcomponent_remove_property(subcomp, modprop);
        icalproperty_free(modprop);
        modprop = NULL;
    }

    if (!update) {
        ItemID id = getItemID(subcomp);
        const char *uid = NULL;

        // Trying to add a normal event which already exists leads to a
        // gerror->domain == E_CALENDAR_ERROR
        // gerror->code == E_CALENDAR_STATUS_OBJECT_ID_ALREADY_EXISTS
        // error. Depending on the Evolution version, the subcomp
        // UID gets removed (>= 2.12) or remains unchanged.
        //
        // Existing detached recurrences are silently updated when
        // trying to add them. This breaks our return code and change
        // tracking.
        //
        // Escape this madness by checking the existence ourselve first
        // based on our list of existing LUIDs. Note that this list is
        // not updated during a sync. This is correct as long as no LUID
        // gets used twice during a sync (examples: add + add, delete + add),
        // which should never happen.
        newluid = id.getLUID();
        if (m_allLUIDs.containsLUID(id)) {
            state = ITEM_NEEDS_MERGE;
        } else {
            // if this is a detached recurrence, then we
            // must use e_cal_modify_object() below if
            // the parent or any other child already exists
            if (!id.m_rid.empty() &&
                m_allLUIDs.containsUID(id.m_uid)) {
                detached = true;
            } else {
                // Creating the parent while children are already in
                // the calendar confuses EDS (at least 2.12): the
                // parent is stored in the .ics with the old UID, but
                // the uid returned to the caller is a different
                // one. Retrieving the item then fails. Avoid this
                // problem by removing the children from the calendar,
                // adding the parent, then updating it with the
                // saved children.
                //
                // TODO: still necessary with e_cal_client API?
                ICalComps_t children;
                if (id.m_rid.empty()) {
                    children = removeEvents(id.m_uid, true);
                }

                // creating new objects works for normal events and detached occurrences alike
                if (
#ifdef USE_ECAL_CLIENT
                    e_cal_client_create_object_sync(m_calendar, subcomp, (gchar **)&uid, 
                                                    NULL, gerror)
#else
                    e_cal_create_object(m_calendar, subcomp, (gchar **)&uid, gerror)
#endif
                    ) {
#ifdef USE_ECAL_CLIENT
                    PlainGStr owner((gchar *)uid);
#endif
                    // Evolution workaround: don't rely on uid being set if we already had
                    // one. In Evolution 2.12.1 it was set to garbage. The recurrence ID
                    // shouldn't have changed either.
                    ItemID newid(!id.m_uid.empty() ? id.m_uid : uid, id.m_rid);
                    newluid = newid.getLUID();
                    modTime = getItemModTime(newid);
                    m_allLUIDs.insertLUID(newid);
                } else {
                    throwError("storing new item", gerror);
                }

                // Recreate any children removed earlier: when we get here,
                // the parent exists and we must update it.
                BOOST_FOREACH(boost::shared_ptr< eptr<icalcomponent> > &icalcomp, children) {
                    if (
#ifdef USE_ECAL_CLIENT
                        !e_cal_client_modify_object_sync(m_calendar, *icalcomp,
                                                         CALOBJ_MOD_THIS, NULL,
                                                         gerror)
#else
                        !e_cal_modify_object(m_calendar, *icalcomp,
                                             CALOBJ_MOD_THIS,
                                             gerror)
#endif
                        ) {
                        throwError(string("recreating item ") + newluid, gerror);
                    }
                }
            }
        }
    }

    if (update ||
        (state != ITEM_OKAY && state != ITEM_NEEDS_MERGE) ||
        detached) {
        ItemID id(newluid);
        bool isParent = id.m_rid.empty();

        // ensure that the component has the right UID and
        // RECURRENCE-ID
        if (update) {
            if (!id.m_uid.empty()) {
                icalcomponent_set_uid(subcomp, id.m_uid.c_str());
            }
            if (!id.m_rid.empty()) {
                // Reconstructing the RECURRENCE-ID is non-trivial,
                // because our luid only contains the date-time, but
                // not the time zone. Only do the work if the event
                // really doesn't have a RECURRENCE-ID.
                struct icaltimetype rid;
                rid = icalcomponent_get_recurrenceid(subcomp);
                if (icaltime_is_null_time(rid)) {
                    // Preserve the original RECURRENCE-ID, including
                    // timezone, no matter what the update contains
                    // (might have wrong timezone or UTC).
                    eptr<icalcomponent> orig(retrieveItem(id));
                    icalproperty *orig_rid = icalcomponent_get_first_property(orig, ICAL_RECURRENCEID_PROPERTY);
                    if (orig_rid) {
                        icalcomponent_add_property(subcomp, icalproperty_new_clone(orig_rid));
                    }
                }
            }
        }

        if (isParent) {
            // CALOBJ_MOD_THIS for parent items (UID set, no RECURRENCE-ID)
            // is not supported by all backends: the Exchange Connector
            // fails with it. It might be an incorrect usage of the API.
            // Therefore we have to use CALOBJ_MOD_ALL, but that removes
            // children.
            bool hasChildren = false;
            LUIDs::const_iterator it = m_allLUIDs.find(id.m_uid);
            if (it != m_allLUIDs.end()) {
                BOOST_FOREACH(const string &rid, it->second) {
                    if (!rid.empty()) {
                        hasChildren = true;
                        break;
                    }
                }
            }

            if (hasChildren) {
                // Use CALOBJ_MOD_ALL and temporarily remove
                // the children, then add them again. Otherwise they would
                // get deleted.
                ICalComps_t children = removeEvents(id.m_uid, true);

                // Parent is gone, too, and needs to be recreated.
                const char *uid = NULL;
                if (
#ifdef USE_ECAL_CLIENT
                    !e_cal_client_create_object_sync(m_calendar, subcomp, (char **)&uid, 
                                                     NULL, gerror)
#else
                    !e_cal_create_object(m_calendar, subcomp, (char **)&uid, gerror)
#endif
                    ) {
                    throwError(string("creating updated item ") + luid, gerror);
                }
#ifdef USE_ECAL_CLIENT
                PlainGStr owner((gchar *)uid);
#endif

                // Recreate any children removed earlier: when we get here,
                // the parent exists and we must update it.
                BOOST_FOREACH(boost::shared_ptr< eptr<icalcomponent> > &icalcomp, children) {
                    if (
#ifdef USE_ECAL_CLIENT
                        !e_cal_client_modify_object_sync(m_calendar, *icalcomp,
                                                         CALOBJ_MOD_THIS, NULL,
                                                         gerror)
#else
                        !e_cal_modify_object(m_calendar, *icalcomp,
                                             CALOBJ_MOD_THIS,
                                             gerror)
#endif
                        ) {
                        throwError(string("recreating item ") + luid, gerror);
                    }
                }
            } else {
                // no children, updating is simple
                if (
#ifdef USE_ECAL_CLIENT
                    !e_cal_client_modify_object_sync(m_calendar, subcomp,
                                                     CALOBJ_MOD_ALL, NULL,
                                                     gerror)
#else
                    !e_cal_modify_object(m_calendar, subcomp,
                                         CALOBJ_MOD_ALL,
                                         gerror)
#endif
                    ) {
                    throwError(string("updating item ") + luid, gerror);
                }
            }
        } else {
            // child event
            if (
#ifdef USE_ECAL_CLIENT
                !e_cal_client_modify_object_sync(m_calendar, subcomp,
                                                 CALOBJ_MOD_THIS, NULL,
                                                 gerror)
#else
                !e_cal_modify_object(m_calendar, subcomp,
                                     CALOBJ_MOD_THIS,
                                     gerror)
#endif
                ) {
                throwError(string("updating item ") + luid, gerror);
            }
        }

        ItemID newid = getItemID(subcomp);
        newluid = newid.getLUID();
        modTime = getItemModTime(newid);
    }

    return InsertItemResult(newluid, modTime, state);
}

EvolutionCalendarSource::ICalComps_t EvolutionCalendarSource::removeEvents(const string &uid, bool returnOnlyChildren, bool ignoreNotFound)
{
    ICalComps_t events;

    LUIDs::const_iterator it = m_allLUIDs.find(uid);
    if (it != m_allLUIDs.end()) {
        BOOST_FOREACH(const string &rid, it->second) {
            ItemID id(uid, rid);
            icalcomponent *icomp = retrieveItem(id);
            if (icomp) {
                if (id.m_rid.empty() && returnOnlyChildren) {
                    icalcomponent_free(icomp);
                } else {
                    events.push_back(ICalComps_t::value_type(new eptr<icalcomponent>(icomp)));
                }
            }
        }
    }

    // removes all events with that UID, including children
    GErrorCXX gerror;
    if (
#ifdef USE_ECAL_CLIENT
        !e_cal_client_remove_object_sync(m_calendar,
                                         uid.c_str(), NULL, CALOBJ_MOD_ALL,
                                         NULL, gerror)

#else
        !e_cal_remove_object(m_calendar,
                             uid.c_str(),
                             gerror)
#endif
        ) {
        if (IsCalObjNotFound(gerror)) {
            SE_LOG_DEBUG(this, NULL, "%s: request to delete non-existant item ignored",
                         uid.c_str());
            if (!ignoreNotFound) {
                throwError(STATUS_NOT_FOUND, string("delete item: ") + uid);
            }
        } else {
            throwError(string("deleting item " ) + uid, gerror);
        }
    }

    return events;
}

void EvolutionCalendarSource::removeItem(const string &luid)
{
    GErrorCXX gerror;
    ItemID id(luid);

    if (id.m_rid.empty()) {
        /*
         * Removing the parent item also removes all children. Evolution
         * does that automatically. Calling e_cal_remove_object_with_mod()
         * without valid rid confuses Evolution, don't do it. As a workaround
         * remove all items with the given uid and if we only wanted to
         * delete the parent, then recreate the children.
         */
        ICalComps_t children = removeEvents(id.m_uid, true, false);

        // recreate children
        bool first = true;
        BOOST_FOREACH(boost::shared_ptr< eptr<icalcomponent> > &icalcomp, children) {
            if (first) {
                char *uid;

                if (
#ifdef USE_ECAL_CLIENT
                    !e_cal_client_create_object_sync(m_calendar, *icalcomp, &uid, 
                                                     NULL, gerror)
#else
                    !e_cal_create_object(m_calendar, *icalcomp, &uid, gerror)
#endif
                    ) {
                    throwError(string("recreating first item ") + luid, gerror);
                }
#ifdef USE_ECAL_CLIENT
                PlainGStr owner((gchar *)uid);
#endif
                first = false;
            } else {
                if (
#ifdef USE_ECAL_CLIENT
                    !e_cal_client_modify_object_sync(m_calendar, *icalcomp,
                                                     CALOBJ_MOD_THIS, NULL,
                                                     gerror)
#else
                    !e_cal_modify_object(m_calendar, *icalcomp,
                                         CALOBJ_MOD_THIS,
                                         gerror)
#endif
                    ) {
                    throwError(string("recreating following item ") + luid, gerror);
                }
            }
        }
    } else {
        // workaround for EDS 2.32 API semantic: succeeds even if
        // detached recurrence doesn't exist and adds EXDATE,
        // therefore we have to check for existence first
        eptr<icalcomponent> item(retrieveItem(id));
        gboolean success = !item ? false :
#ifdef USE_ECAL_CLIENT
            // TODO: is this necessary?
            e_cal_client_remove_object_sync(m_calendar,
                                            id.m_uid.c_str(),
                                            id.m_rid.c_str(),
                                            CALOBJ_MOD_ONLY_THIS,
                                            NULL,
                                            gerror)
#else
            e_cal_remove_object_with_mod(m_calendar,
                                         id.m_uid.c_str(),
                                         id.m_rid.c_str(),
                                         CALOBJ_MOD_THIS,
                                         gerror)
#endif
            ;
        if (!item ||
            (!success && IsCalObjNotFound(gerror))) {
            SE_LOG_DEBUG(this, NULL, "%s: request to delete non-existant item",
                         luid.c_str());
            throwError(STATUS_NOT_FOUND, string("delete item: ") + id.getLUID());
        } else if (!success) {
            throwError(string("deleting item " ) + luid, gerror);
        }
    }
    m_allLUIDs.eraseLUID(id);

    if (!id.m_rid.empty()) {
        // Removing the child may have modified the parent.
        // We must record the new LAST-MODIFIED string,
        // otherwise it might be reported as modified during
        // the next sync (timing dependent: if the parent
        // was updated before removing the child *and* the
        // update and remove fall into the same second,
        // then the modTime does not change again during the
        // removal).
        try {
            ItemID parent(id.m_uid, "");
            string modTime = getItemModTime(parent);
            string parentLUID = parent.getLUID();
            updateRevision(getTrackingNode(), parentLUID, parentLUID, modTime);
        } catch (...) {
            // There's no guarantee that the parent still exists.
            // Instead of checking that, ignore errors (a bit hacky,
            // but better than breaking the removal).
        }
    }
}

icalcomponent *EvolutionCalendarSource::retrieveItem(const ItemID &id)
{
    GErrorCXX gerror;
    icalcomponent *comp = NULL;

    if (
#ifdef USE_ECAL_CLIENT
        !e_cal_client_get_object_sync(m_calendar,
                                      id.m_uid.c_str(),
                                      !id.m_rid.empty() ? id.m_rid.c_str() : NULL,
                                      &comp,
                                      NULL,
                                      gerror)
#else
        !e_cal_get_object(m_calendar,
                          id.m_uid.c_str(),
                          !id.m_rid.empty() ? id.m_rid.c_str() : NULL,
                          &comp,
                          gerror)
#endif
        ) {
        if (IsCalObjNotFound(gerror)) {
            throwError(STATUS_NOT_FOUND, string("retrieving item: ") + id.getLUID());
        } else {
            throwError(string("retrieving item: ") + id.getLUID(), gerror);
        }
    }
    if (!comp) {
        throwError(string("retrieving item: ") + id.getLUID());
    }
    eptr<icalcomponent> ptr(comp);

    /*
     * EDS bug: if a parent doesn't exist while a child does, and we ask
     * for the parent, we are sent the (first?) child. Detect this and
     * turn it into a "not found" error.
     */
    if (id.m_rid.empty()) {
        struct icaltimetype rid = icalcomponent_get_recurrenceid(comp);
        if (!icaltime_is_null_time(rid)) {
            throwError(string("retrieving item: got child instead of parent: ") + id.m_uid);
        }
    }

    return ptr.release();
}

string EvolutionCalendarSource::retrieveItemAsString(const ItemID &id)
{
    eptr<icalcomponent> comp(retrieveItem(id));
    eptr<char> icalstr;

#ifdef USE_ECAL_CLIENT
    icalstr = e_cal_client_get_component_as_string(m_calendar, comp);
#else
    icalstr = e_cal_get_component_as_string(m_calendar, comp);
#endif

    if (!icalstr) {
        // One reason why e_cal_get_component_as_string() can fail is
        // that it uses a TZID which has no corresponding VTIMEZONE
        // definition. Evolution GUI ignores the TZID and interprets
        // the times as local time. Do the same when exporting the
        // event by removing the bogus TZID.
        icalproperty *prop = icalcomponent_get_first_property (comp,
                                                               ICAL_ANY_PROPERTY);

        while (prop) {
            // removes only the *first* TZID - but there shouldn't be more than one
            icalproperty_remove_parameter_by_kind(prop, ICAL_TZID_PARAMETER);
            prop = icalcomponent_get_next_property (comp,
                                                    ICAL_ANY_PROPERTY);
        }

        // now try again
#ifdef USE_ECAL_CLIENT
        icalstr = e_cal_client_get_component_as_string(m_calendar, comp);
#else
        icalstr = e_cal_get_component_as_string(m_calendar, comp);
#endif
        if (!icalstr) {
            throwError(string("could not encode item as iCalendar: ") + id.getLUID());
        } else {
            SE_LOG_DEBUG(this, NULL, "had to remove TZIDs because e_cal_get_component_as_string() failed for:\n%s", icalstr.get());
	}
    }

    /*
     * Evolution/libical can only deal with \, as separator.
     * Replace plain \, in outgoing event CATEGORIES with , -
     * based on simple text search/replace and thus will not work
     * in all cases...
     *
     * Inverse operation in insertItem().
     */
    string data = string(icalstr);
    size_t propstart = data.find("\nCATEGORIES");
    bool modified = false;
    while (propstart != data.npos) {
        size_t eol = data.find('\n', propstart + 1);
        size_t comma = data.find(',', propstart);

        while (eol != data.npos &&
               comma != data.npos &&
               comma < eol) {
            if (data[comma-1] == '\\') {
                data.erase(comma - 1, 1);
                comma--;
                modified = true;
            }
            comma = data.find(',', comma + 1);
        }
        propstart = data.find("\nCATEGORIES", propstart + 1);
    }
    if (modified) {
        SE_LOG_DEBUG(this, NULL, "after replacing \\, with , in CATEGORIES:\n%s", data.c_str());
    }
    
    return data;
}

std::string EvolutionCalendarSource::getDescription(const string &luid)
{
    try {
        eptr<icalcomponent> comp(retrieveItem(ItemID(luid)));
        std::string descr;

        const char *summary = icalcomponent_get_summary(comp);
        if (summary && summary[0]) {
            descr += summary;
        }
        
        if (m_type == EVOLUTION_CAL_SOURCE_TYPE_EVENTS) {
            const char *location = icalcomponent_get_location(comp);
            if (location && location[0]) {
                if (!descr.empty()) {
                    descr += ", ";
                }
                descr += location;
            }
        }

        if (m_type == EVOLUTION_CAL_SOURCE_TYPE_MEMOS &&
            descr.empty()) {
            // fallback to first line of body text
            icalproperty *desc = icalcomponent_get_first_property(comp, ICAL_DESCRIPTION_PROPERTY);
            if (desc) {
                const char *text = icalproperty_get_description(desc);
                if (text) {
                    const char *eol = strchr(text, '\n');
                    if (eol) {
                        descr.assign(text, eol - text);
                    } else {
                        descr = text;
                    }
                }
            }
        }

        return descr;
    } catch (...) {
        // Instead of failing we log the error and ask
        // the caller to log the UID. That way transient
        // errors or errors in the logging code don't
        // prevent syncs.
        handleException();
        return "";
    }
}

string EvolutionCalendarSource::ItemID::getLUID() const
{
    return getLUID(m_uid, m_rid);
}

string EvolutionCalendarSource::ItemID::getLUID(const string &uid, const string &rid)
{
    return uid + "-rid" + rid;
}

EvolutionCalendarSource::ItemID::ItemID(const string &luid)
{
    size_t ridoff = luid.rfind("-rid");
    if (ridoff != luid.npos) {
        const_cast<string &>(m_uid) = luid.substr(0, ridoff);
        const_cast<string &>(m_rid) = luid.substr(ridoff + strlen("-rid"));
    } else {
        const_cast<string &>(m_uid) = luid;
    }
}

EvolutionCalendarSource::ItemID EvolutionCalendarSource::getItemID(ECalComponent *ecomp)
{
    icalcomponent *icomp = e_cal_component_get_icalcomponent(ecomp);
    if (!icomp) {
        SE_THROW("internal error in getItemID(): ECalComponent without icalcomp");
    }
    return getItemID(icomp);
}

EvolutionCalendarSource::ItemID EvolutionCalendarSource::getItemID(icalcomponent *icomp)
{
    const char *uid;
    struct icaltimetype rid;

    uid = icalcomponent_get_uid(icomp);
    rid = icalcomponent_get_recurrenceid(icomp);
    return ItemID(uid ? uid : "",
                  icalTime2Str(rid));
}

string EvolutionCalendarSource::getItemModTime(ECalComponent *ecomp)
{
    struct icaltimetype *modTime;
    e_cal_component_get_last_modified(ecomp, &modTime);
    eptr<struct icaltimetype, struct icaltimetype, UnrefFree<struct icaltimetype> > modTimePtr(modTime);
    if (!modTimePtr) {
        return "";
    } else {
        return icalTime2Str(*modTimePtr.get());
    }
}

string EvolutionCalendarSource::getItemModTime(const ItemID &id)
{
    eptr<icalcomponent> icomp(retrieveItem(id));
    return getItemModTime(icomp);
}

string EvolutionCalendarSource::getItemModTime(icalcomponent *icomp)
{
    icalproperty *modprop = icalcomponent_get_first_property(icomp, ICAL_LASTMODIFIED_PROPERTY);
    if (!modprop) {
        return "";
    }
    struct icaltimetype modTime = icalproperty_get_lastmodified(modprop);

    return icalTime2Str(modTime);
}

string EvolutionCalendarSource::icalTime2Str(const icaltimetype &tt)
{
    static const struct icaltimetype null = { 0 };
    if (!memcmp(&tt, &null, sizeof(null))) {
        return "";
    } else {
        eptr<char> timestr(ical_strdup(icaltime_as_ical_string(tt)));
        if (!timestr) {
            SE_THROW("cannot convert to time string");
        }
        return timestr.get();
    }
}

SE_END_CXX

#endif /* ENABLE_ECAL */

#ifdef ENABLE_MODULES
# include "EvolutionCalendarSourceRegister.cpp"
#endif
