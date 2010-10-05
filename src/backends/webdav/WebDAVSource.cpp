/*
 * Copyright (C) 2010 Intel Corporation
 */

#include "WebDAVSource.h"

#ifdef ENABLE_DAV

#include <boost/bind.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/predicate.hpp>

SE_BEGIN_CXX

WebDAVSource::WebDAVSource(const SyncSourceParams &params,
                           const boost::shared_ptr<Neon::Settings> &settings) :
    TrackingSyncSource(params),
    m_settings(settings)
{
}

void WebDAVSource::open()
{
    SE_LOG_DEBUG(NULL, NULL, "using libneon %s with %s",
                 ne_version_string(), Neon::features().c_str());
    m_session.reset(new Neon::Session(m_settings));

    // Start by checking server capabilities.
    // Verifies URL.
    int caps = m_session->options();
    static const Flag descr[] = {
        { NE_CAP_DAV_CLASS1, "Class 1 WebDAV (RFC 2518)" },
        { NE_CAP_DAV_CLASS2, "Class 2 WebDAV (RFC 2518)" },
        { NE_CAP_DAV_CLASS3, "Class 3 WebDAV (RFC 4918)" },
        { NE_CAP_MODDAV_EXEC, "mod_dav 'executable' property" },
        { NE_CAP_DAV_ACL, "WebDAV ACL (RFC 3744)" },
        { NE_CAP_VER_CONTROL, "DeltaV version-control" },
        { NE_CAP_CO_IN_PLACE, "DeltaV checkout-in-place" },
        { NE_CAP_VER_HISTORY, "DeltaV version-history" },
        { NE_CAP_WORKSPACE, "DeltaV workspace" },
        { NE_CAP_UPDATE, "DeltaV update" },
        { NE_CAP_LABEL, "DeltaV label" },
        { NE_CAP_WORK_RESOURCE, "DeltaV working-resouce" },
        { NE_CAP_MERGE, "DeltaV merge" },
        { NE_CAP_BASELINE, "DeltaV baseline" },
        { NE_CAP_ACTIVITY, "DeltaV activity" },
        { NE_CAP_VC_COLLECTION, "DeltaV version-controlled-collection" },
        { 0, NULL }
    };
    SE_LOG_DEBUG(NULL, NULL, "%s WebDAV capabilities: %s",
                 m_session->getURL().c_str(),
                 Flags2String(caps, descr).c_str());

    // Run a few property queries.
    //
    // This also checks credentials because typically the properties
    // are protected.
    // 
    // First dump WebDAV "allprops" properties (does not contain
    // properties which must be asked for explicitly!).
    Neon::Session::PropfindPropCallback_t callback =
        boost::bind(&WebDAVSource::openPropCallback,
                    this, _1, _2, _3, _4);
    m_session->propfindProp(m_session->getURI().m_path, 0, NULL, callback);

    // Now ask for some specific properties of interest for us.
    // Using CALDAV:allprop would be nice, but doesn't seem to
    // be possible with Neon.
    static const ne_propname caldav[] = {
        { "urn:ietf:params:xml:ns:caldav", "calendar-home-set" },
        { "urn:ietf:params:xml:ns:caldav", "calendar-description" },
        { "urn:ietf:params:xml:ns:caldav", "calendar-timezone" },
        { "urn:ietf:params:xml:ns:caldav", "supported-calendar-component-set" },
        { "urn:ietf:params:xml:ns:caldav", "supported-calendar-data" },
        { "urn:ietf:params:xml:ns:caldav", "max-resource-size" },
        { "urn:ietf:params:xml:ns:caldav", "min-date-time" },
        { "urn:ietf:params:xml:ns:caldav", "max-date-time" },
        { "urn:ietf:params:xml:ns:caldav", "max-instances" },
        { "urn:ietf:params:xml:ns:caldav", "max-attendees-per-instance" },
        { NULL, NULL }
    };
    m_session->propfindProp(m_session->getURI().m_path, 0, caldav, callback);

    // TODO: avoid hard-coded path to Google events
    m_calendar = m_session->getURI();
    if (boost::ends_with(m_calendar.m_path, "/user/") ||
        boost::ends_with(m_calendar.m_path, "/user")) {
        m_calendar = m_calendar.resolve("../events/");
    }
    // Sanitizing the path becomes redundant once we take the path from
    // the server's response. Right now it is necessary so that foo@google.com
    // in the URL matches the correct foo%40google.com in the server's
    // responses.
    boost::replace_all(m_calendar.m_path, "@", "%40");
    m_session->propfindProp(m_calendar.m_path, 0, caldav, callback);
    m_session->propfindProp(m_calendar.m_path, 1, NULL, callback);
}

void WebDAVSource::openPropCallback(const Neon::URI &uri,
                                    const ne_propname *prop,
                                    const char *value,
                                    const ne_status *status)
{
    // TODO: verify that DAV:resourcetype contains DAV:collection
    // TODO: recognize CALDAV:calendar-home-set and redirect to it
    // TODO: recognize CALDAV:calendar-timezone and use it for local time conversion of events
    SE_LOG_DEBUG(NULL, NULL,
                 "%s: %s:%s = %s (%s)",
                 uri.toURL().c_str(),
                 prop->nspace ? prop->nspace : "<no namespace>",
                 prop->name,
                 value ? value : "<no value>",
                 Neon::Status2String(status).c_str());
}

bool WebDAVSource::isEmpty()
{
    // listing all items is relatively efficient, let's use that
    RevisionMap_t revisions;
    listAllItems(revisions);
    return revisions.empty();
}

void WebDAVSource::close()
{
    m_session.reset();
}

WebDAVSource::Databases WebDAVSource::getDatabases()
{
    Databases result;

    // TODO: scan for right collections
    result.push_back(Database("select database via relative URI",
                              "<path>"));
    return result;
}

void WebDAVSource::listAllItems(RevisionMap_t &revisions)
{
    static const ne_propname props[] = {
        { "DAV:", "getetag" },
        { NULL, NULL }
    };

    bool failed = false;
    m_session->propfindURI(m_calendar.m_path, 1, props,
                           boost::bind(&WebDAVSource::listAllItemsCallback,
                                       this, _1, _2, boost::ref(revisions),
                                       boost::ref(failed)));
    if (failed) {
        SE_THROW("incomplete listing of all items");
    }
}

void WebDAVSource::listAllItemsCallback(const Neon::URI &uri,
                                        const ne_prop_result_set *results,
                                        RevisionMap_t &revisions,
                                        bool &failed)
{
    static const ne_propname prop = {
        "DAV:", "getetag"
    };
    const char *etag = ne_propset_value(results, &prop);
    std::string uid = uri.m_path;
    boost::replace_first(uid, m_calendar.m_path, "");
    if (uid.empty()) {
        // skip collection itself
        return;
    }
    if (etag) {
        revisions[uid] = etag;
    } else {
        failed = true;
        SE_LOG_ERROR(NULL, NULL,
                     "%s: %s",
                     uri.toURL().c_str(),
                     Neon::Status2String(ne_propset_status(results, &prop)).c_str());
    }
}

void WebDAVSource::readItem(const string &uid, std::string &item, bool raw)
{
    // TODO
}

TrackingSyncSource::InsertItemResult WebDAVSource::insertItem(const string &uid, const std::string &item, bool raw)
{
    // TODO
    return InsertItemResult("",
                            "",
                            false /* true if adding item was turned into update */);
}


void WebDAVSource::removeItem(const string &uid)
{
    // TODO
}


SE_END_CXX

#endif /* ENABLE_DAV */

#ifdef ENABLE_MODULES
# include "WebDAVSourceRegister.cpp"
#endif
