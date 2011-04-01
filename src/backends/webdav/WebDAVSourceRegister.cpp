/*
 * Copyright (C) 2010 Intel Corporation
 */

#include "CalDAVSource.h"
#include "CardDAVSource.h"
#include <syncevo/SyncSource.h>
#ifdef ENABLE_UNIT_TESTS
#include "test.h"
#endif

#include <boost/bind.hpp>
#include <boost/tokenizer.hpp>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

static SyncSource *createSource(const SyncSourceParams &params)
{
    SourceType sourceType = SyncSource::getSourceType(params.m_nodes);
    bool isMe;

    isMe = sourceType.m_backend == "CalDAV";
    if (isMe) {
        if (sourceType.m_format == "" ||
            sourceType.m_format == "text/calendar" ||
            sourceType.m_format == "text/x-calendar" ||
            sourceType.m_format == "text/x-vcalendar") {
#ifdef ENABLE_DAV
            boost::shared_ptr<Neon::Settings> settings;
            boost::shared_ptr<SubSyncSource> sub(new CalDAVSource(params, settings));
            return new MapSyncSource(params, 0 /* seconds resolution */, sub);
#else
            return RegisterSyncSource::InactiveSource;
#endif
        }
    }

    isMe = sourceType.m_backend == "CardDAV";
    if (isMe) {
        if (sourceType.m_format == "" ||
            sourceType.m_format == "text/x-vcard" ||
            sourceType.m_format == "text/vcard") {
#ifdef ENABLE_DAV
            boost::shared_ptr<Neon::Settings> settings;
            return new CardDAVSource(params, settings);
#else
            return RegisterSyncSource::InactiveSource;
#endif
        }
    }

    return NULL;
}

static RegisterSyncSource registerMe("DAV",
#ifdef ENABLE_DAV
                                     true,
#else
                                     false,
#endif
                                     createSource,
                                     "CalDAV\n"
                                     "CardDAV\n"
                                     ,
                                     Values() +
                                     Aliases("CalDAV")
                                     + Aliases("CardDAV")
                                     );

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
 * creates "source-config@client-test-<server>" peer config
 * and <type> source inside it before instantiating the
 * source
 */
class WebDAVTest : public RegisterSyncSourceTest {
    std::string m_server;
    std::string m_type;
    ConfigProps m_syncProps;

public:
    /**
     * @param server      for example, "yahoo", "google"
     * @param type        "caldav" or "carddav"
     * @param syncProps   sync properties (username, password, syncURL, ...)
     */
    WebDAVTest(const std::string &server,
               const std::string &type,
               const ConfigProps &syncProps) :
        RegisterSyncSourceTest(server + "_" + type, // for example, google_caldav
                               type == "caldav" ? "ical20" :
                               type == "carddav" ? "vcard30" :
                               type),
        m_server(server),
        m_type(type),
        m_syncProps(syncProps)
    {}

    virtual void updateConfig(ClientTestConfig &config) const
    {
        config.type = m_type.c_str();
        config.createSourceA = boost::bind(&WebDAVTest::createSource, this, _3);
        config.createSourceB = boost::bind(&WebDAVTest::createSource, this, _3);
    }

    TestingSyncSource *createSource(bool isSourceA) const
    {
        boost::shared_ptr<SyncConfig> context(new SyncConfig(string("source-config@client-test-") + m_server));
        SyncSourceNodes nodes = context->getSyncSourceNodes(m_type,
                                                            /* string("_") m_clientID + */
                                                            string("_") + (isSourceA ? "A" : "B"));

        // always set properties taken from the environment;
        // TODO: "database" property (currently always uses the default)
        nodes.getProperties()->setProperty("backend", m_type);
        BOOST_FOREACH(const StringPair &propval, m_syncProps) {
            boost::shared_ptr<FilterConfigNode> node = context->getNode(propval.first);
            if (!node) {
                SE_THROW(StringPrintf("invalid property %s=%s set in CLIENT_TEST_WEBDAV for %s %s",
                                      propval.first.c_str(), propval.second.c_str(),
                                      m_server.c_str(), m_type.c_str()));
            }
            node->setProperty(propval.first, propval.second);
        }
        context->flush();

        SyncSourceParams params(m_type,
                                nodes,
                                context);
        SyncSource *ss = SyncSource::createSource(params);
        return static_cast<TestingSyncSource *>(ss);
    }
};


/**
 * creates WebDAV sources by parsing
 * CLIENT_TEST_WEBDAV=<server> [caldav] [carddav] <prop>=<val> ...; ...
 */
static class WebDAVTestSingleton {
    list< boost::shared_ptr<WebDAVTest> > m_sources;

public:
    WebDAVTestSingleton()
    {
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
            bool caldav = false, carddav = false;
            ConfigProps props;
            BOOST_FOREACH(const std::string &token,
                          boost::tokenizer< boost::char_separator<char> >(entry, boost::char_separator<char>("\t "))) {
                if (server.empty()) {
                    server = token;
                } else if (token == "caldav") {
                    caldav = true;
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
