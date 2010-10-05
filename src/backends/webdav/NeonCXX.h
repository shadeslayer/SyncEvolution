/*
 * Copyright (C) 2010 Patrick Ohly <patrick.ohly@gmx.de>
 */

/**
 * Simplifies usage of neon in C++ by wrapping some calls in C++
 * classes. Includes all neon header files relevant for the backend.
 */

#ifndef INCL_NEONCXX
#define INCL_NEONCXX

#include <ne_session.h>
#include <ne_utils.h>
#include <ne_basic.h>
#include <ne_props.h>

#include <string>

#include <boost/shared_ptr.hpp>
#include <boost/function.hpp>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

namespace Neon {
#if 0
}
#endif

/** comma separated list of features supported by libneon in use */
std::string features();

class Settings {
 public:
    /**
     * base URL for WebDAV service
     */
    virtual std::string getURL() = 0;

    /**
     * host name must match for SSL?
     */
    virtual bool verifySSLHost() = 0;

    /**
     * SSL certificate must be valid?
     */
    virtual bool verifySSLCertificate() = 0;

    /**
     * fill in username and password for specified realm (URL?),
     * throw error if not available
     */
    virtual void getCredentials(const std::string &realm,
                                std::string &username,
                                std::string &password) = 0;

    /**
     * standard SyncEvolution log level, see
     * Session::Session() how that is mapped to neon debugging
     */
    virtual int logLevel() = 0;

    /**
     * use this to create a boost_shared pointer for a
     * Settings instance which needs to be freed differently
     */
    struct NullDeleter {
        void operator()(Settings *) const {}
    };
};

struct URI {
    std::string m_scheme;
    std::string m_host;
    std::string m_userinfo;
    unsigned int m_port;
    std::string m_path;
    std::string m_query;
    std::string m_fragment;

    /**
     * Split URL into parts. Throws TransportAgentException on
     * invalid url.  Port will be set to default for scheme if not set
     * in URL.
     */
    static URI parse(const std::string &url);

    static URI fromNeon(const ne_uri &other);

    /**
     * produce new URI from current path and new one (may be absolute
     * and relative)
     */
    URI resolve(const std::string &path) const;

    /** compose URL from parts */
    std::string toURL() const;
};

/** produce debug string for status, which may be NULL */
std::string Status2String(const ne_status *status);

/**
 * Wraps all session related activities.
 * Throws transport errors for fatal problems.
 */
class Session {
 public:
    /**
     * @param settings    must provide information about settings on demand
     */
    Session(const boost::shared_ptr<Settings> &settings);
    ~Session();

    /** ne_options2() */
    unsigned int options();

    /**
     * called with URI and complete result set; exceptions are logged, but ignored
     */
    typedef boost::function<void (const URI &, const ne_prop_result_set *)> PropfindURICallback_t;

    /**
     * called with URI and specific property, value string may be NULL (error case);
     * exceptions are logged and abort iterating over properties (but not URIs)
     */
    typedef boost::function<void (const URI &, const ne_propname *, const char *, const ne_status *)> PropfindPropCallback_t;

    /** ne_simple_propfind(): invoke callback for each URI */
    void propfindURI(const std::string &path, int depth,
                  const ne_propname *props,
                  const PropfindURICallback_t &callback);

    /** ne_simple_propfind(): invoke callback for each property of each URI */
    void propfindProp(const std::string &path, int depth,
                      const ne_propname *props,
                      const PropfindPropCallback_t &callback);

    /** URL which is in use */
    std::string getURL() const { return m_uri.toURL(); }

    /** same as getURL() split into parts */
    const URI &getURI() const { return m_uri; }

 private:
    boost::shared_ptr<Settings> m_settings;
    ne_session *m_session;
    URI m_uri;

    /** ne_set_server_auth() callback */
    static int getCredentials(void *userdata, const char *realm, int attempt, char *username, char *password) throw();

    /** ne_ssl_set_verify() callback */
    static int sslVerify(void *userdata, int failures, const ne_ssl_certificate *cert) throw();

    /** ne_props_result callback which invokes a PropfindURICallback_t as user data */
    static void propsResult(void *userdata, const ne_uri *uri,
                            const ne_prop_result_set *results) throw();

    /** iterate over properties in result set */
    static void propsIterate(const URI &uri, const ne_prop_result_set *results,
                             const PropfindPropCallback_t &callback);

    /** ne_propset_iterator callback which invokes pair<URI, PropfindPropCallback_t> */
    static int propIterator(void *userdata,
                            const ne_propname *pname,
                            const char *value,
                            const ne_status *status) throw();

    typedef std::pair<const URI &, const PropfindPropCallback_t &> PropIteratorUserdata_t;


    /** throw error if error code indicates failure */
    void check(int error);
};

}

SE_END_CXX

#endif // INCL_NEONCXX
