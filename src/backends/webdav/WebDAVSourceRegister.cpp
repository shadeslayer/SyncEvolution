/*
 * Copyright (C) 2010 Intel Corporation
 */

#include "CalDAVSource.h"
#include <syncevo/SyncSource.h>
#include "test.h"

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
            class EnvSettings : public Neon::Settings {
                virtual std::string getURL()
                {
                    const char *var = getenv("WEBDAV_URL");
                    if (!var) {
                        SE_THROW("no WEBDAV_URL");
                    }
                    return var;
                }

                virtual bool verifySSLHost() { return !getenv("WEBDAV_NO_SSL_HOST"); }
                virtual bool verifySSLCertificate() { return !getenv("WEBDAV_NO_SSL_CERT"); }

                virtual void getCredentials(const std::string &realm,
                                            std::string &username,
                                            std::string &password)
                {
                    const char *var = getenv("WEBDAV_USERNAME");
                    if (!var) {
                        SE_THROW("no WEBDAV_USERNAME");
                    }
                    username = var;
                    var = getenv("WEBDAV_PASSWORD");
                    if (var) {
                        password = var;
                    }
                }

                virtual int logLevel()
                {
                    const char *var = getenv("WEBDAV_LOGLEVEL");
                    return var ? atoi(var) : 0;
                }
            };

            // settings now derived from source params context
            boost::shared_ptr<Neon::Settings> settings;
            boost::shared_ptr<SubSyncSource> sub(new CalDAVSource(params, settings));
            return new MapSyncSource(params, 0 /* seconds resolution */, sub);
#else
            return RegisterSyncSource::InactiveSource;
#endif
        }
    }

#if 0
    isMe = sourceType.m_backend == "CardDAV";
    if (isMe) {
        if (sourceType.m_format == "" ||
            sourceType.m_format == "text/x-vcard" ||
            sourceType.m_format == "text/vcard") {
#ifdef ENABLE_DAV
            return new CardDAVSource(params);
#else
            return RegisterSyncSource::InactiveSource;
#endif
        }
    }
#endif

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
                                     "   iCalendar 2.0 (default) = text/calendar\n"
                                     "   vCalendar 1.0 = text/x-vcalendar\n"
#if 0
                                     "CardDAV\n"
                                     "   vCard 2.1 (default) = text/x-vcard\n"
                                     "   vCard 3.0 = text/vcard\n"
#endif
                                     ,
                                     Values() +
                                     Aliases("CalDAV")
#if 0
                                     + Aliases("CardDAV")
#endif
                                     );

#ifdef ENABLE_DAV
#ifdef ENABLE_UNIT_TESTS

class WebDAVTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(WebDAVTest);
    CPPUNIT_TEST(testInstantiate);
    CPPUNIT_TEST_SUITE_END();

protected:
    void testInstantiate() {
        boost::shared_ptr<TestingSyncSource> source;
        source.reset((TestingSyncSource *)SyncSource::createTestingSource("CalDAV", "CalDAV", true));
        source.reset((TestingSyncSource *)SyncSource::createTestingSource("CalDAV", "CalDAV:text/calendar", true));
        source.reset((TestingSyncSource *)SyncSource::createTestingSource("CalDAV", "CalDAV:text/x-vcalendar", true));
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

}

#endif // ENABLE_DAV

SE_END_CXX
