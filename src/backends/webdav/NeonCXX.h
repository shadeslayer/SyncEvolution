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

#include <string>

#include <boost/shared_ptr.hpp>

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

    /** compose URL from parts */
    std::string toURL() const;
};

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

    /** URL which is in use */
    std::string getURL() const { return m_uri.toURL(); }

 private:
    boost::shared_ptr<Settings> m_settings;
    ne_session *m_session;
    URI m_uri;

    /** ne_set_server_auth() callback */
    static int getCredentials(void *userdata, const char *realm, int attempt, char *username, char *password) throw();

    /** ne_ssl_set_verify() callback */
    static int sslVerify(void *userdata, int failures, const ne_ssl_certificate *cert) throw();

    /** throw error if error code indicates failure */
    void check(int error);
};

}

SE_END_CXX

#endif // INCL_NEONCXX
