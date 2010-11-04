/*
 * Copyright (C) 2010 Intel Corporation
 */

#include "WebDAVSource.h"

#ifdef ENABLE_DAV

#include <boost/bind.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <syncevo/TransportAgent.h>

SE_BEGIN_CXX

/**
 * Retrieve settings from SyncConfig.
 * NULL pointer for config is allowed.
 */
class ContextSettings : public Neon::Settings {
    boost::shared_ptr<const SyncConfig> m_context;
public:
    ContextSettings(const boost::shared_ptr<const SyncConfig> &context) :
        m_context(context)
    {}

    virtual std::string getURL()
    {
        std::string url;
        if (m_context) {
            vector<string> urls = m_context->getSyncURL();
            if (!urls.empty()) {
                url = urls.front();
                std::string username = m_context->getUsername();
                boost::replace_all(url, "%u", username);
            }
        }
        return url;
    }

    virtual bool verifySSLHost()
    {
        return !m_context || m_context->getSSLVerifyHost();
    }

    virtual bool verifySSLCertificate()
    {
        return !m_context || m_context->getSSLVerifyServer();
    }

    virtual void getCredentials(const std::string &realm,
                                std::string &username,
                                std::string &password)
    {
        if (m_context) {
            username = m_context->getUsername();
            password = m_context->getPassword();
        }
    }

    virtual int logLevel()
    {
        return m_context ?
            m_context->getLogLevel() :
            0;
    }
};

WebDAVSource::WebDAVSource(const SyncSourceParams &params,
                           const boost::shared_ptr<Neon::Settings> &settings) :
    TrackingSyncSource(params),
    m_settings(settings)
{
    if (!m_settings) {
        m_settings.reset(new ContextSettings(params.m_context));
    }
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

static const ne_propname getetag[] = {
    { "DAV:", "getetag" },
    { NULL, NULL }
};

void WebDAVSource::listAllItems(RevisionMap_t &revisions)
{
    bool failed = false;
    m_session->propfindURI(m_calendar.m_path, 1, getetag,
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
        std::string rev = ETag2Rev(etag);
        SE_LOG_DEBUG(NULL, NULL, "item %s = rev %s",
                     uid.c_str(), rev.c_str());
        revisions[uid] = rev;
    } else {
        failed = true;
        SE_LOG_ERROR(NULL, NULL,
                     "%s: %s",
                     uri.toURL().c_str(),
                     Neon::Status2String(ne_propset_status(results, &prop)).c_str());
    }
}

std::string WebDAVSource::path2luid(const std::string &path)
{
    if (boost::starts_with(path, m_calendar.m_path)) {
        return path.substr(m_calendar.m_path.size());
    } else {
        return path;
    }
}

std::string WebDAVSource::luid2path(const std::string &luid)
{
    return m_calendar.resolve(luid).m_path;
}

void WebDAVSource::readItem(const string &uid, std::string &item, bool raw)
{
    item.clear();
    Neon::Request req(*m_session, "GET", luid2path(uid),
                      "", item);
    req.run();
}

TrackingSyncSource::InsertItemResult WebDAVSource::insertItem(const string &uid, const std::string &item, bool raw)
{
    std::string new_uid;
    std::string rev;
    bool update = false;  /* true if adding item was turned into update */

    std::string result;
    if (uid.empty()) {
        // Pick a random resource name,
        // catch unexpected conflicts via If-None-Match: *.
        new_uid = UUID();
        Neon::Request req(*m_session, "PUT", luid2path(new_uid),
                          item, result);
        req.setFlag(NE_REQFLAG_IDEMPOTENT, 0);
        req.addHeader("If-None-Match", "*");
        req.run();
        SE_LOG_DEBUG(NULL, NULL, "add item status: %s",
                     Neon::Status2String(req.getStatus()).c_str());
        switch (req.getStatusCode()) {
        case 204:
            // stored, potentially in a different resource than requested
            // when the UID was recognized
            break;
        case 201:
            // created
            break;
        default:
            SE_THROW_EXCEPTION_STATUS(TransportStatusException,
                                      std::string("unexpected status for insert: ") +
                                      Neon::Status2String(req.getStatus()),
                                      SyncMLStatus(req.getStatus()->code));
            break;
        }
        rev = getETag(req);
        std::string real_luid = getLUID(req);
        if (!real_luid.empty()) {
            // Google renames the resource automatically to something of the form
            // <UID>.ics. Interestingly enough, our 1234567890!@#$%^&*()<>@dummy UID
            // test case leads to a resource path which Google then cannot find
            // via CalDAV. client-test must run with CLIENT_TEST_SIMPLE_UID=1...
            SE_LOG_DEBUG(NULL, NULL, "new item mapped to %s", real_luid.c_str());
            new_uid = real_luid;
            // TODO: find a better way of detecting unexpected updates.
            // update = true;
        }
    } else {
        // TODO: preserve original UID and RECURRENCE-ID,
        // update just one VEVENT in a meeting series (which is
        // one resource in CalDAV)
        new_uid = uid;
        Neon::Request req(*m_session, "PUT", luid2path(new_uid),
                          item, result);
        req.setFlag(NE_REQFLAG_IDEMPOTENT, 0);
        // TODO: match exactly the expected revision, aka ETag,
        // or implement locking. Note that the ETag might not be
        // known, for example in this case:
        // - PUT succeeds
        // - PROPGET does not
        // - insertItem() fails
        // - Is retried? Might need slow sync in this case!
        //
        // req.addHeader("If-Match", etag);
        req.run();
        SE_LOG_DEBUG(NULL, NULL, "update item status: %s",
                     Neon::Status2String(req.getStatus()).c_str());
        switch (req.getStatusCode()) {
        case 204:
            // the expected outcome, as we were asking for an overwrite
            break;
        case 201:
            // Huh? Shouldn't happen, but Google sometimes reports it
            // even when updating an item. Accept it.
            // SE_THROW("unexpected creation instead of update");
            break;
        default:
            SE_THROW_EXCEPTION_STATUS(TransportStatusException,
                                      std::string("unexpected status for update: ") +
                                      Neon::Status2String(req.getStatus()),
                                      SyncMLStatus(req.getStatus()->code));
            break;
        }
        rev = getETag(req);
        std::string real_luid = getLUID(req);
        if (!real_luid.empty() && real_luid != new_uid) {
            SE_THROW(StringPrintf("updating item: real luid %s does not match old luid %s",
                                  real_luid.c_str(), new_uid.c_str()));
        }
    }

    if (rev.empty()) {
        // Server did not include etag header. Must request it
        // explicitly (leads to race condition!). Google Calendar
        // assigns a new ETag even if the body has not changed,
        // so any kind of caching of ETag would not work either.
        bool failed = false;
        RevisionMap_t revisions;
        m_session->propfindURI(luid2path(new_uid), 0, getetag,
                               boost::bind(&WebDAVSource::listAllItemsCallback,
                                           this, _1, _2, boost::ref(revisions),
                                           boost::ref(failed)));
        rev = revisions[new_uid];
        if (failed || rev.empty()) {
            SE_THROW("could not retrieve ETag");
        }
    }

    return InsertItemResult(new_uid, rev, update);
}

std::string WebDAVSource::ETag2Rev(const std::string &etag)
{
    std::string res = etag;
    if (boost::starts_with(res, "W/")) {
        res.erase(0, 2);
    }
    if (res.size() >= 2) {
        res = res.substr(1, res.size() - 2);
    }
    return res;
}

std::string WebDAVSource::getLUID(Neon::Request &req)
{
    std::string location = req.getResponseHeader("Location");
    if (location.empty()) {
        return location;
    } else {
        return path2luid(Neon::URI::parse(location).m_path);
    }
}

void WebDAVSource::removeItem(const string &uid)
{
    std::string item, result;
    Neon::Request req(*m_session, "DELETE", luid2path(uid),
                      item, result);
    // TODO: match exactly the expected revision, aka ETag,
    // or implement locking.
    // req.addHeader("If-Match", etag);
    req.run();
    SE_LOG_DEBUG(NULL, NULL, "remove item status: %s",
                 Neon::Status2String(req.getStatus()).c_str());
    switch (req.getStatusCode()) {
    case 204:
        // the expected outcome
        break;
    default:
        SE_THROW_EXCEPTION_STATUS(TransportStatusException,
                                  std::string("unexpected status for removal: ") +
                                  Neon::Status2String(req.getStatus()),
                                  SyncMLStatus(req.getStatus()->code));
        break;
    }
}

SE_END_CXX

#endif /* ENABLE_DAV */

#ifdef ENABLE_MODULES
# include "WebDAVSourceRegister.cpp"
#endif
