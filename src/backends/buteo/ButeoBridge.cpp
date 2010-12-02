/*
 * Copyright (C) 2010 Intel Corporation
 */

#include "ButeoBridge.h"
#ifdef ENABLE_UNIT_TESTS
#include "test.h"
#endif
#include <syncevo/util.h>
#include <syncevo/Cmdline.h>
#include <syncevo/Logging.h>

#include <QDebug>

#include <stddef.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <iostream>

#include <libsyncprofile/SyncResults.h>
#include <syncevo/SyncSource.h>

SE_BEGIN_CXX

// this ensures that backends are initialized once inside the
// Buteo bridge
static std::string backends = SyncSource::backendsInfo();

ButeoBridge::ButeoBridge(const QString &pluginName,
                         const Buteo::SyncProfile &profile,
                         Buteo::PluginCbInterface *cbInterface) :
    ClientPlugin(pluginName, profile, cbInterface)
{
    m_username = profile.key("Username", "no username set").toUtf8().data();
    m_password = profile.key("Password", "no password set").toUtf8().data();
}

bool ButeoBridge::startSync()
{
    std::string explanation("internal error");
    try {
        if (m_config.empty()) {
            SE_THROW("init() not called");
        }

        // run sync
        Cmdline sync(std::cout, std::cerr,
                     "buteo-sync",
                     "--run",
                     "--sync-property", StringPrintf("username=%s", m_username.c_str()).c_str(),
                     "--sync-property", StringPrintf("password=%s", m_password.c_str()).c_str(),
                     "--sync-property", "preventSlowSync=0",
                     m_config.c_str(),
                     NULL);
        bool res = sync.parse() && sync.run();

        // analyze result
        const SyncReport &report = sync.getReport();
        SyncMLStatus status = report.getStatus();
        std::string explanation = Status2String(status);
        switch (status) {
        case STATUS_OK:
        case STATUS_HTTP_OK:
            if (res) {
                emit success(getProfileName(), "done");
            } else {
                emit error(getProfileName(), "internal error", Buteo::SyncResults::INTERNAL_ERROR);
            }
            break;
        case STATUS_UNAUTHORIZED:
        case STATUS_FORBIDDEN:
            emit error(getProfileName(), explanation.c_str(), Buteo::SyncResults::AUTHENTICATION_FAILURE);
            break;
        case STATUS_TRANSPORT_FAILURE:
            emit error(getProfileName(), explanation.c_str(), Buteo::SyncResults::CONNECTION_ERROR);
            break;
        default:
            emit error(getProfileName(), explanation.c_str(), Buteo::SyncResults::INTERNAL_ERROR);
            break;
        }

        // Client/Configuration errors 4xx
        // INTERNAL_ERROR = 401,
        // AUTHENTICATION_FAILURE,
        // DATABASE_FAILURE,

        // Server/Network errors 5xx
        // SUSPENDED = 501,
        // ABORTED,
        // CONNECTION_ERROR,
        // INVALID_SYNCML_MESSAGE,
        // UNSUPPORTED_SYNC_TYPE,
        // UNSUPPORTED_STORAGE_TYPE,
        //Upto here

        //Context Error Code
        // LOW_BATTERY_POWER = 601,
        // POWER_SAVING_MODE,
        // OFFLINE_MODE,
        // BACKUP_IN_PROGRESS,
        // LOW_MEMORY
        return true;
    } catch (...) {
        Exception::handle(explanation);
    }
    emit error(getProfileName(), explanation.c_str(), Buteo::SyncResults::INTERNAL_ERROR);
    return false;
}

bool ButeoBridge::init()
{
    try {
        if (getenv("SYNCEVOLUTION_DEBUG")) {
            LoggerBase::instance().setLevel(Logger::DEBUG);
        }

        // determine parameters for configuration
        std::string url;
        QString profile = getProfileName();
        if (profile == "google-calendar") {
            m_config = "google-calendar";
            url = "syncURL=https://www.google.com/calendar/dav/%u/user/?SyncEvolution=Google";
        } else if (profile == "yahoo") {
            m_config = "yahoo";
            url = "syncURL=https://caldav.calendar.yahoo.com/dav/%u/Calendar/";
        } else {
            return false;
        }

        // configure local sync of calendar with CalDAV
        std::string config = StringPrintf("source-config@%s", m_config.c_str());
        if (!SyncConfig(config).exists()) {
            Cmdline target(std::cout, std::cerr,
                           "buteo-sync",
                           "--template", "SyncEvolution",
                           "--sync-property", url.c_str(),
                           "--sync-property", "printChanges=0",
                           "--sync-property", "dumpData=0",
                           "--source-property", "type=CalDAV",
                           config.c_str(),
                           "calendar",
                           NULL);
            bool res = target.parse() && target.run();
            if (!res) {
                SE_THROW("client configuration failed");
            }
        }
        if (!SyncConfig(m_config).exists()) {
            Cmdline server(std::cout, std::cerr,
                           "buteo-sync",
                           "--template", "SyncEvolution",
                           "--sync-property", "peerIsClient=1",
                           "--sync-property", "printChanges=0",
                           "--sync-property", "dumpData=0",
                           "--sync-property", StringPrintf("syncURL=local://@%s", m_config.c_str()).c_str(),
                           m_config.c_str(),
                           "calendar",
                           NULL);
            bool res = server.parse() && server.run();
            if (!res) {
                SE_THROW("server configuration failed");
            }
        }
        return true;
    } catch (...) {
        Exception::handle();
    }
    return false;
}

bool ButeoBridge::uninit()
{
    // nothing to do
    return true;
}

void ButeoBridge::connectivityStateChanged(Sync::ConnectivityType type,
                                           bool state)
{
}

extern "C" ButeoBridge *createPlugin(const QString &pluginName,
                                     const Buteo::SyncProfile &profile,
                                     Buteo::PluginCbInterface *cbInterface)
{
   return new ButeoBridge(pluginName, profile, cbInterface);
}

extern "C" void destroyPlugin(ButeoBridge *client)
{
    delete client;
}

#ifdef ENABLE_UNIT_TESTS

/**
 * The library containing this test is not normally
 * linked into client-test. To use the test, compile
 * client-test manually without -Wl,-as-needed
 * and add libsyncevo-buteo.so.
 */
class ButeoTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(ButeoTest);
    CPPUNIT_TEST(init);
    CPPUNIT_TEST_SUITE_END();

    std::string m_testDir;

public:
    ButeoTest() :
        m_testDir("ButeoTest")
    {}

    void init() {
        ScopedEnvChange xdg("XDG_CONFIG_HOME", m_testDir);
        Buteo::SyncProfile profile("google-calendar");
        ButeoBridge client("google-calendar", profile, NULL);
        CPPUNIT_ASSERT(client.init());
    }
};

SYNCEVOLUTION_TEST_SUITE_REGISTRATION(ButeoTest);

#endif // ENABLE_UNIT_TESTS

SE_END_CXX
