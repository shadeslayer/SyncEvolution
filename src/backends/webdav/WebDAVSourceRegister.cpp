/*
 * Copyright (C) 2010 Intel Corporation
 */

#include "WebDAVSource.h"
#include "CalDAVSource.h"
#include "CalDAVVxxSource.h"
#include "CardDAVSource.h"
#include <syncevo/SyncSource.h>
#ifdef ENABLE_UNIT_TESTS
#include "test.h"
#endif
#ifdef NEON_COMPATIBILITY
#include <dlfcn.h>
#endif

#include <boost/bind.hpp>
#include <boost/tokenizer.hpp>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

static SyncSource *createSource(const SyncSourceParams &params)
{
    SourceType sourceType = SyncSource::getSourceType(params.m_nodes);
    bool isMe;

    // Backend is enabled if a suitable libneon can be found. In
    // binary compatibility mode, libneon was not linked against.
    // Instead we dlopen() it and don't care whether that is
    // libneon.so.27 or libneon-gnutls.so.27. Debian Testing only has
    // the later.
#ifdef NEON_COMPATIBILITY
    static bool enabled;
    if (!enabled) {
        // try libneon.so.27 first because it seems to be a bit more
        // common and upstream seems to use OpenSSL
        void *dl = dlopen("libneon.so.27", RTLD_LAZY|RTLD_GLOBAL);
        if (!dl) {
            dl = dlopen("libneon-gnutls.so.27", RTLD_LAZY|RTLD_GLOBAL);
        }
        if (dl) {
            enabled = true;
        }
    }
#elif defined(ENABLE_DAV)
    static bool enabled = true;
#endif

    isMe = sourceType.m_backend == "CalDAV" ||
        sourceType.m_backend == "CalDAVTodo" ||
        sourceType.m_backend == "CalDAVJournal";
    if (isMe) {
        if (sourceType.m_format == "" ||
            sourceType.m_format == "text/calendar" ||
            sourceType.m_format == "text/x-calendar" ||
            sourceType.m_format == "text/x-vcalendar") {
#ifdef ENABLE_DAV
            if (enabled) {
                boost::shared_ptr<Neon::Settings> settings;
                if (sourceType.m_backend == "CalDAV") {
                    boost::shared_ptr<SubSyncSource> sub(new CalDAVSource(params, settings));
                    return new MapSyncSource(params, sub);
                } else {
                    return new CalDAVVxxSource(sourceType.m_backend == "CalDAVTodo" ? "VTODO" : "VJOURNAL",
                                               params, settings);
                }
            }
#endif
            return RegisterSyncSource::InactiveSource(params);
        }
    }

    isMe = sourceType.m_backend == "CardDAV";
    if (isMe) {
        if (sourceType.m_format == "" ||
            sourceType.m_format == "text/x-vcard" ||
            sourceType.m_format == "text/vcard") {
#ifdef ENABLE_DAV
            if (enabled) {
                boost::shared_ptr<Neon::Settings> settings;
                return new CardDAVSource(params, settings);
            }
#endif
            return RegisterSyncSource::InactiveSource(params);
        }
    }

    return NULL;
}

static class RegisterWebDAVSyncSource : public RegisterSyncSource
{
public:
    RegisterWebDAVSyncSource() :
        RegisterSyncSource("DAV",
#ifdef ENABLE_DAV
                           true,
#else
                           false,
#endif
                           createSource,
                           "CalDAV\n"
                           "   calendar events\n"
                           "CalDAVTodo\n"
                           "   tasks\n"
                           "CalDAVJournal\n"
                           "   memos\n"
                           "CardDAV\n"
                           "   contacts\n"
                           ,
                           Values() +
                           Aliases("CalDAV")
                           + Aliases("CalDAVTodo")
                           + Aliases("CalDAVJournal")
                           + Aliases("CardDAV")
                           )
    {
        // configure and register our own property;
        // do this regardless whether the backend is enabled,
        // so that config migration always includes this property
        WebDAVCredentialsOkay().setHidden(true);
        SyncConfig::getRegistry().push_back(&WebDAVCredentialsOkay());
    }
} registerMe;

#ifdef ENABLE_DAV
#ifdef ENABLE_UNIT_TESTS

class WebDAVTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(WebDAVTest);
    CPPUNIT_TEST(testInstantiate);
    CPPUNIT_TEST(testHTMLEntities);
    CPPUNIT_TEST_SUITE_END();

protected:
    void testInstantiate() {
        boost::shared_ptr<TestingSyncSource> source;
        source.reset((TestingSyncSource *)SyncSource::createTestingSource("CalDAV", "CalDAV", true));
        source.reset((TestingSyncSource *)SyncSource::createTestingSource("CalDAV", "CalDAV:text/calendar", true));
        source.reset((TestingSyncSource *)SyncSource::createTestingSource("CalDAV", "CalDAV:text/x-vcalendar", true));
        source.reset((TestingSyncSource *)SyncSource::createTestingSource("CardDAV", "CardDAV", true));
        source.reset((TestingSyncSource *)SyncSource::createTestingSource("CardDAV", "CardDAV:text/vcard", true));
        source.reset((TestingSyncSource *)SyncSource::createTestingSource("CardDAV", "CardDAV:text/x-vcard", true));
    }

    std::string decode(const char *item) {
        std::string buffer = item;
        CardDAVSource::replaceHTMLEntities(buffer);
        return buffer;
    }

    void testHTMLEntities() {
        // named entries
        CPPUNIT_ASSERT_EQUAL(std::string("\" & ' < >"),
                             decode("&quot; &amp; &apos; &lt; &gt;"));
        // decimal and hex, encoded in different ways
        CPPUNIT_ASSERT_EQUAL(std::string("\" & ' < >"),
                             decode("&#x22; &#0038; &#x0027; &#x3C; &#x3e;"));
        // no translation needed
        CPPUNIT_ASSERT_EQUAL(std::string("hello world"),
                             decode("hello world"));
        // entity at start
        CPPUNIT_ASSERT_EQUAL(std::string("< "),
                             decode("&lt; "));
        // entity at end
        CPPUNIT_ASSERT_EQUAL(std::string(" <"),
                             decode(" &lt;"));
        // double quotation
        CPPUNIT_ASSERT_EQUAL(std::string("\\"),
                             decode("&amp;#92;"));
        CPPUNIT_ASSERT_EQUAL(std::string("ampersand entity & less-than entity <"),
                             decode("ampersand entity &amp; less-than entity &amp;lt;"));

        // invalid entities
        CPPUNIT_ASSERT_EQUAL(std::string(" &"),
                             decode(" &"));
        CPPUNIT_ASSERT_EQUAL(std::string("&"),
                             decode("&"));
        CPPUNIT_ASSERT_EQUAL(std::string("& "),
                             decode("& "));
        CPPUNIT_ASSERT_EQUAL(std::string("&;"),
                             decode("&;"));
        CPPUNIT_ASSERT_EQUAL(std::string("&; "),
                             decode("&; "));
        CPPUNIT_ASSERT_EQUAL(std::string(" &; "),
                             decode(" &; "));
        CPPUNIT_ASSERT_EQUAL(std::string(" &;"),
                             decode(" &;"));
        CPPUNIT_ASSERT_EQUAL(std::string("&xyz;"),
                             decode("&xyz;"));
        CPPUNIT_ASSERT_EQUAL(std::string("&#1f;"),
                             decode("&#1f;"));
        CPPUNIT_ASSERT_EQUAL(std::string("&#1f;"),
                             decode("&#1f;"));
        CPPUNIT_ASSERT_EQUAL(std::string("&#x1f ;"),
                             decode("&#x1f ;"));
        CPPUNIT_ASSERT_EQUAL(std::string("&#quot ;"),
                             decode("&#quot ;"));
    }
};

SYNCEVOLUTION_TEST_SUITE_REGISTRATION(WebDAVTest);

#endif // ENABLE_UNIT_TESTS

namespace {
#if 0
}
#endif



/**
 * implements one specific source for local testing;
 * creates "target-config@client-test-<server>" peer config
 * and <type> source inside it before instantiating the
 * source
 */
class WebDAVTest : public RegisterSyncSourceTest {
    std::string m_server;
    std::string m_type;
    std::string m_database;
    ConfigProps m_props;

public:
    /**
     * @param server      for example, "yahoo", "google"
     * @param type        "caldav", "caldavtodo", "caldavjournal" or "carddav"
     * @param props       sync properties (username, password, syncURL, ...)
     *                    or key/value parameters for the testing (testcases)
     */
    WebDAVTest(const std::string &server,
               const std::string &type,
               const ConfigProps &props) :
        RegisterSyncSourceTest(server + "_" + type, // for example, google_caldav
                               props.get(type + "/testconfig",
                                         props.get("testconfig",
                                                   type == "caldav" ? "eds_event" :
                                                   type == "caldavtodo" ? "eds_task" :
                                                   type == "caldavjournal" ? "eds_memo" :
                                                   type == "carddav" ? "eds_contact" :
                                                   type))),
        m_server(server),
        m_type(type),
        m_props(props)
    {}

    std::string getDatabase() const { return m_database; }
    void setDatabase(const std::string &database) { m_database = database; }

    virtual void updateConfig(ClientTestConfig &config) const
    {
        config.m_type = m_type.c_str();
        if (m_type == "caldav") {
            config.m_supportsReccurenceEXDates = true;
        }
        config.m_createSourceA = boost::bind(&WebDAVTest::createSource, this, _2, _4);
        config.m_createSourceB = boost::bind(&WebDAVTest::createSource, this, _2, _4);
        ConfigProps::const_iterator it = m_props.find(m_type + "/testcases");
        if (it != m_props.end() ||
            (it = m_props.find("testcases")) != m_props.end()) {
            config.m_testcases = it->second.c_str();
        }
    }

    // This is very similar to client-test-app.cpp. TODO: refactor?!
    TestingSyncSource *createSource(const std::string &clientID, bool isSourceA) const
    {
        std::string name = m_server + "_" + m_type;
        const char *server = getenv("CLIENT_TEST_SERVER");
        std::string config = "target-config@client-test";
        if (server) {
            config += "-";
            config += server;
        }
        std::string tracking =
            string("_") + clientID +
            string("_") + (isSourceA ? "A" : "B");

        SE_LOG_DEBUG(NULL, NULL, "instantiating testing source %s in config %s, with tracking name %s",
                     name.c_str(),
                     config.c_str(),
                     tracking.c_str());
        boost::shared_ptr<SyncConfig> context(new SyncConfig(config));
        SyncSourceNodes nodes = context->getSyncSourceNodes(name, tracking);

        // Copy properties from the Client::Sync
        // @<CLIENT_TEST_SERVER>_<clientID>/<name> config, to ensure
        // that a testing source used as part of Client::Sync uses the
        // same settings.
        std::string peerName = std::string(server ? server : "no-such-server")  + "_" + clientID;
        boost::shared_ptr<SyncConfig> peer(new SyncConfig(peerName));
        SyncSourceNodes peerNodes = peer->getSyncSourceNodes(name);
        SE_LOG_DEBUG(NULL, NULL, "overriding testing source %s properties with the ones from config %s = %s",
                     name.c_str(),
                     peerName.c_str(),
                     peer->getRootPath().c_str());
        BOOST_FOREACH(const ConfigProperty *prop, SyncSourceConfig::getRegistry()) {
            if (prop->isHidden()) {
                continue;
            }
            boost::shared_ptr<FilterConfigNode> node = peerNodes.getNode(*prop);
            InitStateString value = prop->getProperty(*node);
            SE_LOG_DEBUG(NULL, NULL, "   %s = %s (%s)",
                         prop->getMainName().c_str(),
                         value.c_str(),
                         value.wasSet() ? "set" : "default");
            node = nodes.getNode(*prop);
            node->setProperty(prop->getMainName(), value);
        }
        // Also copy loglevel.
        context->setLogLevel(peer->getLogLevel());
        context->flush();


        // Always set properties taken from the environment.
        nodes.getProperties()->setProperty("backend", InitStateString(m_type, true));
        SE_LOG_DEBUG(NULL, NULL, "   additional property backend = %s (from CLIENT_TEST_WEBDAV)",
                     m_type.c_str());
        BOOST_FOREACH(const StringPair &propval, m_props) {
            boost::shared_ptr<FilterConfigNode> node = context->getNode(propval.first);
            if (node) {
                SE_LOG_DEBUG(NULL, NULL, "   additional property %s = %s (from CLIENT_TEST_WEBDAV)",
                             propval.first.c_str(), propval.second.c_str());
                node->setProperty(propval.first, InitStateString(propval.second, true));
            } else if (!boost::ends_with(propval.first, "testconfig") &&
                       !boost::ends_with(propval.first, "testcases")) {
                SE_THROW(StringPrintf("invalid property %s=%s set in CLIENT_TEST_WEBDAV for %s %s",
                                      propval.first.c_str(), propval.second.c_str(),
                                      m_server.c_str(), m_type.c_str()));
            }
        }
        context->flush();

        SyncSourceParams params(m_type,
                                nodes,
                                context);
        SyncSource *ss = SyncSource::createSource(params);
        ss->setDisplayName(ss->getDisplayName() +
                           (isSourceA ? " #A" : " #B"));
        return static_cast<TestingSyncSource *>(ss);
    }
};


/**
 * creates WebDAV sources by parsing
 * CLIENT_TEST_WEBDAV=<server> [caldav] [carddav] <prop>=<val> ...; ...
 */
static class WebDAVTestSingleton : RegisterSyncSourceTest {
    /**
     * It could be that different sources are configured to use
     * the same resource (= database property). Get the database
     * property of each source by instantiating it. Check against
     * already added entries and if a match is found, record the
     * link. This enables the Client::Source::xxx::testLinkedSources
     * test of that previous entry.
     */
    class WebDAVList
    {
        list< boost::shared_ptr<WebDAVTest> >m_sources;

    public:
        void push_back(const boost::shared_ptr<WebDAVTest> &source)
        {
            boost::scoped_ptr<TestingSyncSource> instance(source->createSource("1", true));
            std::string database = instance->getDatabaseID();
            source->setDatabase(database);

            BOOST_FOREACH (const boost::shared_ptr<WebDAVTest> &other,
                           m_sources) {
                if (other->getDatabase() == database) {
                    other->m_linkedSources.push_back(source->m_configName);
                    break;
                }
            }
            m_sources.push_back(source);
        }
    };
    mutable WebDAVList m_sources;

public:
    WebDAVTestSingleton() :
        RegisterSyncSourceTest("", "") // empty, only purpose is to get init() called
    {}

    virtual void updateConfig(ClientTestConfig &config) const {}
    virtual void init() const
    {
        static bool initialized;
        if (initialized) {
            return;
        }
        initialized = true;

        const char *env = getenv("CLIENT_TEST_WEBDAV");
        if (!env) {
            return;
        }

        std::string settings(env);
        boost::char_separator<char> sep1(";");
        boost::char_separator<char> sep2("\t ");
        BOOST_FOREACH(const std::string &entry,
                      boost::tokenizer< boost::char_separator<char> >(settings, boost::char_separator<char>(";"))) {
            std::string server;
            bool caldav = false,
                caldavtodo = false,
                caldavjournal = false,
                carddav = false;
            ConfigProps props;
            BOOST_FOREACH(const std::string &token,
                          boost::tokenizer< boost::char_separator<char> >(entry, boost::char_separator<char>("\t "))) {
                if (server.empty()) {
                    server = token;
                } else if (token == "caldav") {
                    caldav = true;
                } else if (token == "caldavtodo") {
                    caldavtodo = true;
                } else if (token == "caldavjournal") {
                    caldavjournal = true;
                } else if (token == "carddav") {
                    carddav = true;
                } else {
                    size_t pos = token.find('=');
                    if (pos == token.npos) {
                        SE_THROW(StringPrintf("CLIENT_TEST_WEBDAV: unknown keyword %s", token.c_str()));
                    }
                    props[token.substr(0,pos)] = token.substr(pos + 1);
                }
            }

            if (caldav) {
                boost::shared_ptr<WebDAVTest> ptr(new WebDAVTest(server, "caldav", props));
                m_sources.push_back(ptr);
            }
            if (caldavtodo) {
                boost::shared_ptr<WebDAVTest> ptr(new WebDAVTest(server, "caldavtodo", props));
                m_sources.push_back(ptr);
            }
            if (caldavjournal) {
                boost::shared_ptr<WebDAVTest> ptr(new WebDAVTest(server, "caldavjournal", props));
                m_sources.push_back(ptr);
            }
            if (carddav) {
                boost::shared_ptr<WebDAVTest> ptr(new WebDAVTest(server, "carddav", props));
                m_sources.push_back(ptr);
            }
        }
    }
} WebDAVTestSingleton;

}

#endif // ENABLE_DAV

SE_END_CXX
