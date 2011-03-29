/*
 * Copyright (C) 2010 Patrick Ohly <patrick.ohly@gmx.de>
 */

#include <config.h>

#ifdef ENABLE_DAV

#include "NeonCXX.h"
#include <ne_socket.h>
#include <ne_auth.h>
#include <ne_xmlreq.h>
#include <ne_string.h>

#include <list>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/bind.hpp>

#include <syncevo/util.h>
#include <syncevo/Logging.h>
#include <syncevo/LogRedirect.h>
#include <syncevo/SmartPtr.h>

#include <dlfcn.h>

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
    if (uri.path) { res.m_path = normalizePath(uri.path, false); }
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

std::string URI::escape(const std::string &text)
{
    SmartPtr<char *> tmp(ne_path_escape(text.c_str()));
    return tmp.get();
}

std::string URI::unescape(const std::string &text)
{
    SmartPtr<char *> tmp(ne_path_unescape(text.c_str()));
    return tmp.get();
}

std::string URI::normalizePath(const std::string &path, bool collection)
{
    std::string res;
    res.reserve(path.size() * 150 / 100);

    typedef boost::split_iterator<string::const_iterator> string_split_iterator;
    string_split_iterator it =
        boost::make_split_iterator(path, boost::first_finder("/", boost::is_iequal()));
    while (!it.eof()) {
        res += escape(unescape(std::string(it->begin(), it->end())));
        ++it;
        if (!it.eof()) {
            res += '/';
        }
    }
    if (collection && !boost::ends_with(res, "/")) {
        res += '/';
    }
    return res;
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
    m_forceAuthorizationOnce(false),
    m_settings(settings),
    m_debugging(false),
    m_session(NULL),
    m_lastRequestEnd(0)
{
    int logLevel = m_settings->logLevel();
    if (logLevel >= 3) {
        ne_debug_init(stderr,
                      NE_DBG_FLUSH|NE_DBG_HTTP|NE_DBG_HTTPAUTH|
                      (logLevel >= 4 ? NE_DBG_HTTPBODY : 0) |
                      (logLevel >= 5 ? (NE_DBG_LOCKS|NE_DBG_SSL) : 0)|
                      (logLevel >= 6 ? (NE_DBG_XML|NE_DBG_XMLPARSE) : 0)|
                      (logLevel >= 11 ? (NE_DBG_HTTPPLAIN) : 0));
        m_debugging = true;
    } else {
        ne_debug_init(NULL, 0);
    }

    ne_sock_init();
    m_uri = URI::parse(settings->getURL());
    m_session = ne_session_create(m_uri.m_scheme.c_str(),
                                  m_uri.m_host.c_str(),
                                  m_uri.m_port);
    ne_set_server_auth(m_session, getCredentials, this);
    if (m_uri.m_scheme == "https") {
        // neon only initializes session->ssl_context if
        // using https and segfaults in ne_ssl_trust_default_ca()
        // of ne_gnutls.c if ne_ssl_trust_default_ca()
        // is called for non-https. So better call these
        // functions only when needed.
        ne_ssl_set_verify(m_session, sslVerify, this);
        ne_ssl_trust_default_ca(m_session);

        // hack for Yahoo: need a client certificate
        ne_ssl_client_cert *cert = ne_ssl_clicert_read("client.p12");
        SE_LOG_DEBUG(NULL, NULL, "client cert is %s", !cert ? "missing" : ne_ssl_clicert_encrypted(cert) ? "encrypted" : "unencrypted");
        if (cert) {
            if (ne_ssl_clicert_encrypted(cert)) {
                if (ne_ssl_clicert_decrypt(cert, "meego")) {
                    SE_LOG_DEBUG(NULL, NULL, "decryption failed");
                }
            }
            ne_ssl_set_clicert(m_session, cert);
        }
    }

    m_proxyURL = settings->proxy();
    if (m_proxyURL.empty()) {
#ifdef HAVE_LIBNEON_SYSTEM_PROXY
        // hard compile-time dependency
        ne_session_system_proxy(m_session, 0);
#else
        // compiled against older libneon, but might run with more recent neon
        typedef void (*session_system_proxy_t)(ne_session *sess, unsigned int flags);
        session_system_proxy_t session_system_proxy =
            (session_system_proxy_t)dlsym(RTLD_DEFAULT, "ne_session_system_proxy");
        if (session_system_proxy) {
            session_system_proxy(m_session, 0);
        }
#endif
    } else {
        URI proxyuri = URI::parse(m_proxyURL);
        ne_session_proxy(m_session, proxyuri.m_host.c_str(), proxyuri.m_port);
    }

    int seconds = settings->timeoutSeconds();
    if (seconds < 0) {
        seconds = 5 * 60;
    }
    ne_set_read_timeout(m_session, seconds);
    ne_set_connect_timeout(m_session, seconds);
    ne_hook_pre_send(m_session, preSendHook, this);
}

Session::~Session()
{
    if (m_session) {
        ne_session_destroy(m_session);
    }
    ne_sock_exit();
}

boost::shared_ptr<Session> Session::m_cachedSession;

boost::shared_ptr<Session> Session::create(const boost::shared_ptr<Settings> &settings)
{
    URI uri = URI::parse(settings->getURL());
    if (m_cachedSession &&
        m_cachedSession->m_uri == uri &&
        m_cachedSession->m_proxyURL == settings->proxy()) {
        // reuse existing session with new settings pointer
        m_cachedSession->m_settings = settings;
        return m_cachedSession;
    }
    // create new session
    m_cachedSession.reset(new Session(settings));
    return m_cachedSession;
}


int Session::getCredentials(void *userdata, const char *realm, int attempt, char *username, char *password) throw()
{
    try {
        Session *session = static_cast<Session *>(userdata);
        std::string user, pw;
        session->m_settings->getCredentials(realm, user, pw);
        SyncEvo::Strncpy(username, user.c_str(), NE_ABUFSIZ);
        SyncEvo::Strncpy(password, pw.c_str(), NE_ABUFSIZ);

        if (attempt) {
            // Already sent credentials once, still rejected:
            // observed with Google Calendar (to throttle request rate?).
            time_t last = session->getLastRequestEnd();
            if (!last) {
                // first request immediately failed, prevent further retries
                SE_LOG_DEBUG(NULL, NULL, "credential error, abort request");
                return 1;
            } else {
                // repeat request after exponentially increasing
                // delays since the last successful request (5
                // seconds, 10 seconds, 20 seconds, ...) until it
                // succeeds, but not for more than 1 minute
                time_t delay = 5 * (1<<attempt);
                if (delay > 60) {
                    SE_LOG_DEBUG(NULL, NULL, "credential error, abort request after %ld seconds",
                                 (long)(time(NULL) - last));
                    return 1;
                } else {
                    time_t now = time(NULL);
                    if (now < last + delay) {
                        SE_LOG_DEBUG(NULL, NULL, "credential error due to throttling (?), retry #%d in %ld seconds",
                                     attempt,
                                     (long)(last + delay - now));
                        sleep(last + delay - now);
                    }
                }
            }
        }

        // try again with credentials
        return 0;
    } catch (...) {
        Exception::handle();
        SE_LOG_ERROR(NULL, NULL, "no credentials for %s", realm);
        return 1;
    }
}

void Session::forceAuthorization(const std::string &username, const std::string &password)
{
    m_forceAuthorizationOnce = true;
    m_forceUsername = username;
    m_forcePassword = password;
}

void Session::preSendHook(ne_request *req, void *userdata, ne_buffer *header) throw()
{
    try {
        static_cast<Session *>(userdata)->preSend(req, header);
    } catch (...) {
        Exception::handle();
    }
}

void Session::preSend(ne_request *req, ne_buffer *header)
{
    if (m_forceAuthorizationOnce) {
        // only do this once
        m_forceAuthorizationOnce = false;

        // append "Authorization: Basic" header if not present already
        if (!boost::starts_with(header->data, "Authorization:") &&
            !strstr(header->data, "\nAuthorization:")) {
            std::string credentials = m_forceUsername + ":" + m_forcePassword;
            SmartPtr<char *> blob(ne_base64((const unsigned char *)credentials.c_str(), credentials.size()));
            ne_buffer_concat(header, "Authorization: Basic ", blob.get(), "\r\n", NULL);
        }
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

#ifdef HAVE_LIBNEON_OPTIONS
unsigned int Session::options(const std::string &path)
{
    unsigned int caps;
    check(ne_options2(m_session, path.c_str(), &caps));
    return caps;
}
#endif // HAVE_LIBNEON_OPTIONS

void Session::propfindURI(const std::string &path, int depth,
                          const ne_propname *props,
                          const PropfindURICallback_t &callback)
{
    ne_propfind_handler *handler;
    int error;

    handler = ne_propfind_create(m_session, path.c_str(), depth);
    if (props != NULL) {
	error = ne_propfind_named(handler, props,
                                  propsResult, const_cast<void *>(static_cast<const void *>(&callback)));
    } else {
	error = ne_propfind_allprop(handler,
                                    propsResult, const_cast<void *>(static_cast<const void *>(&callback)));
    }

    // remember details before destroying request, needed for 301
    ne_request *req = ne_propfind_get_request(handler);
    const ne_status *status = ne_get_status(req);
    int code = status->code;
    int klass = status->klass;
    const char *tmp = ne_get_response_header(req, "Location");
    std::string location(tmp ? tmp : "");

    ne_propfind_destroy(handler);
    
    if (error == NE_ERROR && klass == 3) {
        SE_THROW_EXCEPTION_2(RedirectException,
                             StringPrintf("%d status: redirected to %s", code, location.c_str()),
                             code, location);
    } else {
        check(error, code);
    }
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
    PropIteratorUserdata_t data(&uri, &callback);
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
        (*data->second)(*data->first, pname, value, status);
        return 0;
    } catch (...) {
        Exception::handle();
        return 1; // abort iterating
    }
}

void Session::flush()
{
    if (m_debugging &&
        LogRedirect::redirectingStderr()) {
        // flush stderr and wait a bit: this might help to get
        // the redirected output via LogRedirect
        fflush(stderr);
        Sleep(0.001);
    }
}

void Session::check(int error, int code)
{
    flush();

    switch (error) {
    case NE_AUTH:
        SE_THROW_EXCEPTION_STATUS(TransportStatusException,
                                  StringPrintf("Neon error code %d: %s",
                                               error,
                                               ne_get_error(m_session)),
                                  STATUS_UNAUTHORIZED);
        break;
    case NE_OK:
        break;
    case NE_ERROR:
        if (code) {
            // copy error code into exception
            SE_THROW_EXCEPTION_STATUS(TransportStatusException,
                                      StringPrintf("Neon error code %d: %s",
                                                   error,
                                                   ne_get_error(m_session)),
                                      SyncMLStatus(code));
        }
        // no break
    default:
        SE_THROW_EXCEPTION(TransportException,
                           StringPrintf("Neon error code %d: %s",
                                        error,
                                        ne_get_error(m_session)));
        break;
    }
}

XMLParser::XMLParser()
{
    m_parser = ne_xml_create();
}

XMLParser::~XMLParser()
{
    ne_xml_destroy(m_parser);
}

XMLParser &XMLParser::pushHandler(const StartCB_t &start,
                                  const DataCB_t &data,
                                  const EndCB_t &end)
{
    m_stack.push_back(Callbacks(start, data, end));
    Callbacks &cb = m_stack.back();
    ne_xml_push_handler(m_parser,
                        startCB, dataCB, endCB,
                        &cb);
    return *this;
}

int XMLParser::startCB(void *userdata, int parent,
                       const char *nspace, const char *name,
                       const char **atts)
{
    Callbacks *cb = static_cast<Callbacks *>(userdata);
    try {
        return cb->m_start(parent, nspace, name, atts);
    } catch (...) {
        Exception::handle();
        SE_LOG_ERROR(NULL, NULL, "startCB %s %s failed", nspace, name);
        return -1;
    }
}

int XMLParser::dataCB(void *userdata, int state,
                      const char *cdata, size_t len)
{
    Callbacks *cb = static_cast<Callbacks *>(userdata);
    try {
        return cb->m_data ?
            cb->m_data(state, cdata, len) :
            0;
    } catch (...) {
        Exception::handle();
        SE_LOG_ERROR(NULL, NULL, "dataCB failed");
        return -1;
    }
}

int XMLParser::endCB(void *userdata, int state, 
                     const char *nspace, const char *name)
{
    Callbacks *cb = static_cast<Callbacks *>(userdata);
    try {
        return cb->m_end ?
            cb->m_end(state, nspace, name) :
            0;
    } catch (...) {
        Exception::handle();
        SE_LOG_ERROR(NULL, NULL, "endCB %s %s failed", nspace, name);
        return -1;
    }
}


int XMLParser::accept(const std::string &nspaceExpected,
                      const std::string &nameExpected,
                      const char *nspace,
                      const char *name)
{
    if (nspace && nspaceExpected == nspace &&
        name && nameExpected == name) {
        return 1;
    } else {
        return 0;
    }
}

int XMLParser::append(std::string &buffer,
                      const char *data,
                      size_t len)
{
    buffer.append(data, len);
    return 0;
}

int XMLParser::reset(std::string &buffer)
{
    buffer.clear();
    return 0;
}

void XMLParser::initReportParser(std::string &href,
                                 std::string &etag)
{
    pushHandler(boost::bind(Neon::XMLParser::accept, "DAV:", "multistatus", _2, _3));
    pushHandler(boost::bind(Neon::XMLParser::accept, "DAV:", "response", _2, _3));
    pushHandler(boost::bind(Neon::XMLParser::accept, "DAV:", "href", _2, _3),
                boost::bind(Neon::XMLParser::append, boost::ref(href), _2, _3));
    pushHandler(boost::bind(Neon::XMLParser::accept, "DAV:", "propstat", _2, _3));
    pushHandler(boost::bind(Neon::XMLParser::accept, "DAV:", "status", _2, _3) /* check status? */);
    pushHandler(boost::bind(Neon::XMLParser::accept, "DAV:", "prop", _2, _3));
    pushHandler(boost::bind(Neon::XMLParser::accept, "DAV:", "getetag", _2, _3),
                boost::bind(Neon::XMLParser::append, boost::ref(etag), _2, _3));
}

Request::Request(Session &session,
                 const std::string &method,
                 const std::string &path,
                 const std::string &body,
                 std::string &result) :
    m_method(method),
    m_session(session),
    m_result(&result),
    m_parser(NULL)
{
    m_req = ne_request_create(session.getSession(), m_method.c_str(), path.c_str());
    ne_set_request_body_buffer(m_req, body.c_str(), body.size());
}

Request::Request(Session &session,
                 const std::string &method,
                 const std::string &path,
                 const std::string &body,
                 XMLParser &parser) :
    m_method(method),
    m_session(session),
    m_result(NULL),
    m_parser(&parser)
{
    m_req = ne_request_create(session.getSession(), m_method.c_str(), path.c_str());
    ne_set_request_body_buffer(m_req, body.c_str(), body.size());
}

Request::~Request()
{
    ne_request_destroy(m_req);
}

void Request::run()
{
    int error;
    int attempt = 0;

 retry:
    if (m_result) {
        m_result->clear();
        ne_add_response_body_reader(m_req, ne_accept_2xx,
                                    addResultData, this);
        error = ne_request_dispatch(m_req);
    } else {
        error = ne_xml_dispatch_request(m_req, m_parser->get());
    }

    m_session.flush();

    if (false && error == NE_OK && getStatus()->code == 500) {
        // Internal server error: seems to be Yahoo! Contacts way of
        // throttling requests. Try again later. A similar loop
        // exists *inside* neon for the credentials error seen
        // with Google Calendar, see Session::getCredentials().
        time_t last = m_session.getLastRequestEnd();
        if (last) {
            // repeat request after exponentially increasing
            // delays since the last successful request (5
            // seconds, 10 seconds, 20 seconds, ...) until it
            // succeeds, but not for more than 1 minute
            time_t delay = 5 * (1<<attempt);
            if (delay <= 60) {
                time_t now = time(NULL);
                if (now < last + delay) {
                    SE_LOG_DEBUG(NULL, NULL, "500 internal server error due to throttling (?), retry #%d in %ld seconds",
                                 attempt,
                                 (long)(last + delay - now));
                    sleep(last + delay - now);
                }
                attempt++;
                goto retry;
            }
        }
    }

    check(error);
    m_session.setLastRequestEnd(time(NULL));
}

int Request::addResultData(void *userdata, const char *buf, size_t len)
{
    Request *me = static_cast<Request *>(userdata);
    me->m_result->append(buf, len);
    return 0;
}

void Request::check(int error)
{
    if (error == NE_ERROR &&
        getStatus()->klass == 3) {
        std::string location = getResponseHeader("Location");
        SE_THROW_EXCEPTION_2(RedirectException,
                             StringPrintf("%d status: redirected to %s", getStatus()->klass, location.c_str()),
                             getStatus()->klass,
                             location);
    }
    m_session.check(error, getStatus()->code);
    if (getStatus()->klass != 2) {
        SE_THROW_EXCEPTION_STATUS(TransportStatusException,
                                  std::string("bad status: ") + Status2String(getStatus()),
                                  SyncMLStatus(getStatus()->code));
    }
}

}

SE_END_CXX

#endif // ENABLE_DAV
