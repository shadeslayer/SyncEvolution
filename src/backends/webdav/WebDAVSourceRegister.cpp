/*
 * Copyright (C) 2010 Intel Corporation
 */

#include "CalDAVSource.h"
#include "CardDAVSource.h"
#include <syncevo/SyncSource.h>
#ifdef ENABLE_UNIT_TESTS
#include "test.h"
#endif

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

static class CalDAVTest : public RegisterSyncSourceTest {
public:
    CalDAVTest() : RegisterSyncSourceTest("caldav_ical20", "ical20") {}

    virtual void updateConfig(ClientTestConfig &config) const
    {
        config.type = "CalDAV";
    }
} CalDAVTest;

static class CardDAVTest : public RegisterSyncSourceTest {
public:
    CardDAVTest() : RegisterSyncSourceTest("carddav_vcard30", "vcard30") {}

    virtual void updateConfig(ClientTestConfig &config) const
    {
        config.type = "CardDAV";
    }
} CardDAVTest;

}

#endif // ENABLE_DAV

SE_END_CXX
