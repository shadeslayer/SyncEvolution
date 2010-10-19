/*
 * Copyright (C) 2010 Patrick Ohly <patrick.ohly@gmx.de>
 */

#include <config.h>

#ifdef ENABLE_DAV

#include "NeonCXX.h"
#include <ne_socket.h>
#include <ne_auth.h>

#include <list>
#include <boost/algorithm/string/join.hpp>
#include <boost/bind.hpp>

#include <syncevo/TransportAgent.h>
#include <syncevo/util.h>
#include <syncevo/Logging.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

namespace Neon {
#if 0
}
#endif

std::string features()
{
    std::list<std::string> res;

    if (ne_has_support(NE_FEATURE_SSL)) { res.push_back("SSL"); }
    if (ne_has_support(NE_FEATURE_ZLIB)) { res.push_back("ZLIB"); }
    if (ne_has_support(NE_FEATURE_IPV6)) { res.push_back("IPV6"); }
    if (ne_has_support(NE_FEATURE_LFS)) { res.push_back("LFS"); }
    if (ne_has_support(NE_FEATURE_SOCKS)) { res.push_back("SOCKS"); }
    if (ne_has_support(NE_FEATURE_TS_SSL)) { res.push_back("TS_SSL"); }
    if (ne_has_support(NE_FEATURE_I18N)) { res.push_back("I18N"); }
    return boost::join(res, ", ");
}

URI URI::parse(const std::string &url)
{
    ne_uri uri;
    int error = ne_uri_parse(url.c_str(), &uri);
    URI res = fromNeon(uri);
    if (!res.m_port) {
        res.m_port = ne_uri_defaultport(res.m_scheme.c_str());
    }
    ne_uri_free(&uri);
    if (error) {
        SE_THROW_EXCEPTION(TransportException,
                           StringPrintf("invalid URL '%s' (parsed as '%s')",
                                        url.c_str(),
                                        res.toURL().c_str()));
    }
    return res;
}

URI URI::fromNeon(const ne_uri &uri)
{
    URI res;

    if (uri.scheme) { res.m_scheme = uri.scheme; }
    if (uri.host) { res.m_host = uri.host; }
    if (uri.userinfo) { res.m_userinfo = uri.userinfo; }
    if (uri.path) { res.m_path = uri.path; }
    if (uri.query) { res.m_query = uri.query; }
    if (uri.fragment) { res.m_fragment = uri.fragment; }
    res.m_port = uri.port;

    return res;
}

URI URI::resolve(const std::string &path) const
{
    ne_uri tmp[2];
    ne_uri full;
    memset(tmp, 0, sizeof(tmp));
    tmp[0].path = const_cast<char *>(m_path.c_str());
    tmp[1].path = const_cast<char *>(path.c_str());
    ne_uri_resolve(tmp + 0, tmp + 1, &full);
    URI res(*this);
    res.m_path = full.path;
    ne_uri_free(&full);
    return res;
}

std::string URI::toURL() const
{
    return StringPrintf("%s://%s@%s:%u/%s#%s",
                        m_scheme.c_str(),
                        m_userinfo.c_str(),
                        m_host.c_str(),
                        m_port,
                        m_path.c_str(),
                        m_fragment.c_str());
}

std::string Status2String(const ne_status *status)
{
    if (!status) {
        return "<NULL status>";
    }
    return StringPrintf("<status %d.%d, code %d, class %d, %s>",
                        status->major_version,
                        status->minor_version,
                        status->code,
                        status->klass,
                        status->reason_phrase ? status->reason_phrase : "\"\"");
}

Session::Session(const boost::shared_ptr<Settings> &settings) :
    m_settings(settings),
    m_session(NULL)
    
{
    int logLevel = m_settings->logLevel();
    if (logLevel >= 3) {
        ne_debug_init(stderr,
                      NE_DBG_FLUSH|NE_DBG_HTTP|NE_DBG_HTTPAUTH|
                      (logLevel >= 4 ? NE_DBG_HTTPBODY : 0) |
                      (logLevel >= 5 ? (NE_DBG_LOCKS|NE_DBG_SSL) : 0)|
                      (logLevel >= 6 ? (NE_DBG_XML|NE_DBG_XMLPARSE) : 0)|
                      (logLevel >= 11 ? (NE_DBG_HTTPPLAIN) : 0));
    } else {
        ne_debug_init(NULL, 0);
    }

    ne_sock_init();
    m_uri = URI::parse(settings->getURL());
    m_session = ne_session_create(m_uri.m_scheme.c_str(),
                                  m_uri.m_host.c_str(),
                                  m_uri.m_port);
    ne_set_server_auth(m_session, getCredentials, this);
    ne_ssl_set_verify(m_session, sslVerify, this);
}

Session::~Session()
{
    if (m_session) {
        ne_session_destroy(m_session);
    }
    ne_sock_exit();
}

int Session::getCredentials(void *userdata, const char *realm, int attempt, char *username, char *password) throw()
{
    try {
        Session *session = static_cast<Session *>(userdata);
        std::string user, pw;
        session->m_settings->getCredentials(realm, user, pw);
        SyncEvo::Strncpy(username, user.c_str(), NE_ABUFSIZ);
        SyncEvo::Strncpy(password, pw.c_str(), NE_ABUFSIZ);
        // allow only one attempt, credentials are not expected to change in most cases
        return attempt;
    } catch (...) {
        Exception::handle();
        SE_LOG_ERROR(NULL, NULL, "no credentials for %s", realm);
        return 1;
    }
}

int Session::sslVerify(void *userdata, int failures, const ne_ssl_certificate *cert) throw()
{
    try {
        Session *session = static_cast<Session *>(userdata);

        static const Flag descr[] = {
            { NE_SSL_NOTYETVALID, "certificate not yet valid" },
            { NE_SSL_EXPIRED, "certificate has expired" },
            { NE_SSL_IDMISMATCH, "hostname mismatch" },
            { NE_SSL_UNTRUSTED, "untrusted certificate" },
            { 0, NULL }
        };

        SE_LOG_DEBUG(NULL, NULL,
                     "%s: SSL verification problem: %s",
                     session->getURL().c_str(),
                     Flags2String(failures, descr).c_str());
        if (!session->m_settings->verifySSLCertificate()) {
            SE_LOG_DEBUG(NULL, NULL, "ignoring bad certificate");
            return 0;
        }
        if (failures == NE_SSL_IDMISMATCH &&
            !session->m_settings->verifySSLHost()) {
            SE_LOG_DEBUG(NULL, NULL, "ignoring hostname mismatch");
            return 0;
        }
        return 1;
    } catch (...) {
        Exception::handle();
        return 1;
    }
}

unsigned int Session::options()
{
    unsigned int caps;
    check(ne_options2(m_session, m_uri.m_path.c_str(), &caps));
    return caps;
}

void Session::propfindURI(const std::string &path, int depth,
                          const ne_propname *props,
                          const PropfindURICallback_t &callback)
{
    check(ne_simple_propfind(m_session, path.c_str(), depth,
                             props, propsResult, const_cast<void *>(static_cast<const void *>(&callback))));
}

void Session::propsResult(void *userdata, const ne_uri *uri,
                          const ne_prop_result_set *results) throw()
{
    try {
        PropfindURICallback_t *callback = static_cast<PropfindURICallback_t *>(userdata);
        (*callback)(URI::fromNeon(*uri), results);
    } catch (...) {
        Exception::handle();
    }
}

void Session::propfindProp(const std::string &path, int depth,
                           const ne_propname *props,
                           const PropfindPropCallback_t &callback)
{
    propfindURI(path, depth, props,
                boost::bind(&Session::propsIterate, _1, _2, boost::cref(callback)));
}

void Session::propsIterate(const URI &uri, const ne_prop_result_set *results,
                           const PropfindPropCallback_t &callback)
{
    PropIteratorUserdata_t data(uri, callback);
    ne_propset_iterate(results,
                       propIterator,
                       &data);
}

int Session::propIterator(void *userdata,
                           const ne_propname *pname,
                           const char *value,
                           const ne_status *status) throw()
{
    try {
        const PropIteratorUserdata_t *data = static_cast<const PropIteratorUserdata_t *>(userdata);
        data->second(data->first, pname, value, status);
        return 0;
    } catch (...) {
        Exception::handle();
        return 1; // abort iterating
    }
}

void Session::check(int error)
{
    if (error) {
        SE_THROW_EXCEPTION(TransportException,
                           StringPrintf("Neon error code %d: %s",
                                        error,
                                        ne_get_error(m_session)));
    }
}

Request::Request(Session &session,
                 const std::string &method,
                 const std::string &path,
                 const std::string &body,
                 std::string &result) :
    m_session(session),
    m_result(result)
{
    m_req = ne_request_create(session.getSession(), method.c_str(), path.c_str());
    ne_set_request_body_buffer(m_req, body.c_str(), body.size());
    ne_add_response_body_reader(m_req, ne_accept_2xx,
                                addResultData, this);
}

Request::~Request()
{
    ne_request_destroy(m_req);
}

int Request::addResultData(void *userdata, const char *buf, size_t len)
{
    Request *me = static_cast<Request *>(userdata);
    me->m_result.append(buf, len);
    return 0;
}

void Request::check(int error)
{
    m_session.check(error);
    if (getStatus()->klass != 2) {
        SE_THROW(std::string("bad status: ") + Status2String(getStatus()));
    }
}

}

SE_END_CXX

#endif // ENABLE_DAV
