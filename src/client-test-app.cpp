/*
 * Copyright (C) 2005-2006 Patrick Ohly
 * Copyright (C) 2007 Funambol
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <config.h>

#include <base/test.h>
#include <test/ClientTest.h>
#include <EvolutionClientConfig.h>

#include <cppunit/extensions/HelperMacros.h>
#include <exception>
#include <iostream>

#include "EvolutionSyncClient.h"
#include "EvolutionCalendarSource.h"
#include "EvolutionContactSource.h"

/** a wrapper class which automatically does an open() in the constructor and a close() in the destructor */
template<class T> class TestEvolutionSyncSource : public T {
public:
    TestEvolutionSyncSource(ECalSourceType type, string changeID, string database) :
        T(type, "dummy", NULL, changeID, database) {}
    TestEvolutionSyncSource(string changeID, string database) :
        T("dummy", NULL, changeID, database) {}

    virtual int beginSync() {
        CPPUNIT_ASSERT_NO_THROW(T::open());
        CPPUNIT_ASSERT(!T::hasFailed());
        return T::beginSync();
    }

    virtual int endSync() {
        int res = T::endSync();
        CPPUNIT_ASSERT_NO_THROW(T::close());
        CPPUNIT_ASSERT(!T::hasFailed());
        return res;
    }
};

class TestEvolution : public ClientTest {
public:
    /**
     * can be instantiated as client A with id == "1" and client B with id == "2"
     */
    TestEvolution(const string &id) :
        ClientTest(getenv("CLIENT_TEST_DELAY") ? atoi(getenv("CLIENT_TEST_DELAY")) : 0,
                   getenv("CLIENT_TEST_LOG") ? getenv("CLIENT_TEST_LOG") : ""),
        clientID(id) {
        const char *server = getenv("CLIENT_TEST_SERVER");

        if (id == "1") {
            clientB.reset(new TestEvolution("2"));
        }

        /* check server */
        if (!server) {
            server = "funambol";
            setenv("CLIENT_TEST_SERVER", "funambol", 1);
        }

        /* override Evolution database names? */
        const char *evoprefix = getenv("CLIENT_TEST_EVOLUTION_PREFIX");
        evoPrefix = evoprefix ? evoprefix :  "SyncEvolution_Test_";

        /* check sources */
        const char *sourcelist = getenv("CLIENT_TEST_SOURCES");
        if (!sourcelist) {
            sourcelist = "vcard21,vcard30,ical20,imemo20,itodo20";
        }
        numSources = 0;
        for (SourceType sourceType = (SourceType)0; sourceType < TEST_MAX_SOURCE; sourceType = (SourceType)((int)sourceType + 1) ) {
            string name = getSourceName(sourceType);

#ifndef ENABLE_EBOOK
            if (sourceType == TEST_CONTACT21_SOURCE || sourceType == TEST_CONTACT30_SOURCE) {
                continue;
            }
#endif
#ifndef ENABLE_ECAL
            if (sourceType == TEST_CALENDAR_SOURCE || sourceType == TEST_TASK_SOURCE) {
                continue;
            }
#endif
            if (strstr(sourcelist, name.c_str())) {
                enabledSources[numSources++] = sourceType;
            }
        }

        // get configuration and set obligatory fields
        LOG.setLevel(LOG_LEVEL_DEBUG);
        std::string root = std::string("evolution/") + server + "_" + id;
        std::auto_ptr<DMTClientConfig> config(new EvolutionClientConfig(root.c_str(), true));
        config->read();
        config->open();
        DeviceConfig &dc(config->getDeviceConfig());
        if (!strlen(dc.getDevID())) {
            // no configuration yet
            config->setClientDefaults();
            dc.setDevID(id == "1" ? "sc-api-nat" : "sc-pim-ppc");
        }
        for (SourceType sourceType = (SourceType)0; sourceType < TEST_MAX_SOURCE; sourceType = (SourceType)((int)sourceType + 1) ) {
            ClientTest::Config testconfig;
            getSourceConfig(sourceType, testconfig);
            CPPUNIT_ASSERT(testconfig.type);

            SyncSourceConfig* sc = config->getSyncSourceConfig(testconfig.sourceName);
            if (!sc) {
                // no configuration yet
                config->setSourceDefaults(testconfig.sourceName);
                sc = config->getSyncSourceConfig(testconfig.sourceName);
                CPPUNIT_ASSERT(sc);
                sc->setURI(testconfig.uri);
                sc->setType(testconfig.type);
                // ensure that config has a ManagementNode for the new source
                config->save();
                config->read();
                config->open();
            }
            ManagementNode *node = config->getSyncSourceNode(testconfig.sourceName);
            CPPUNIT_ASSERT(node);
            string database = getDatabaseName(sourceType);
            node->setPropertyValue("evolutionsource", database.c_str());

            // flush config to disk
            config.reset(new EvolutionClientConfig(root.c_str(), true));
            config->read();
            config->open();
            sc = config->getSyncSourceConfig(testconfig.sourceName);
            CPPUNIT_ASSERT(sc);

            sc->setType(testconfig.type);
        }
        config->save();
    }

    enum SourceType {
        TEST_CONTACT21_SOURCE,
        TEST_CONTACT30_SOURCE,
        TEST_CALENDAR_SOURCE,
        TEST_TASK_SOURCE,
        // TEST_MEMO_SOURCE,
        TEST_MAX_SOURCE
    };

    virtual int getNumSources() {
        return numSources;
    }

    virtual void getSourceConfig(SourceType sourceType, Config &config) {
        memset(&config, 0, sizeof(config));
        
        switch (sourceType) {
         case TEST_CONTACT21_SOURCE:
            getTestData("vcard30", config);
            config.sourceName = "vcard21";
            config.uri = "card"; // Funambol
            config.type = "text/x-vcard";            
            break;
         case TEST_CONTACT30_SOURCE:
            getTestData("vcard30", config);
            break;
         case TEST_CALENDAR_SOURCE:
            getTestData("ical20", config);
            break;
         case TEST_TASK_SOURCE:
            getTestData("itodo20", config);
            break;
         default:
            CPPUNIT_ASSERT(sourceType < TEST_MAX_SOURCE);
            break;
        }
        config.createSourceA = createSource;
        config.createSourceB = createSource;
        config.compare = compare;
    }

    virtual void getSourceConfig(int source, Config &config) {
        getSourceConfig(enabledSources[source], config);
    }

    virtual ClientTest *getClientB() {
        return clientB.get();
    }

    virtual bool isB64Enabled() {
        return false;
    }

    virtual int sync(
        const int *sources,
        SyncMode syncMode,
        const CheckSyncReport &checkReport,
        long maxMsgSize = 0,
        long maxObjSize = 0,
        bool loSupport = false,
        const char *encoding = NULL) {
        set<string> activeSources;
        for(int i = 0; sources[i] >= 0; i++) {
            activeSources.insert(getSourceName(enabledSources[sources[i]]));
        }

        string server = getenv("CLIENT_TEST_SERVER") ? getenv("CLIENT_TEST_SERVER") : "funambol";
        server += "_";
        server += clientID;
        
        class ClientTest : public EvolutionSyncClient {
        public:
            ClientTest(const string &server,
                           const set<string> &activeSources,
                           SyncMode syncMode,
                           long maxMsgSize,
                           long maxObjSize,
                           bool loSupport,
                           const char *encoding) :
                EvolutionSyncClient(server, false, activeSources, "evolution/"),
                m_syncMode(syncMode),
                m_maxMsgSize(maxMsgSize),
                m_maxObjSize(maxObjSize),
                m_loSupport(loSupport),
                m_encoding(encoding)
                {}

        protected:
            virtual void prepare(SyncManagerConfig &config,
                                 SyncSource **sources) {
                for (SyncSource **source = sources;
                     *source;
                     source++) {
                    if (m_encoding) {
                        (*source)->getConfig().setEncoding(m_encoding);
                    }
                    (*source)->setPreferredSyncMode(m_syncMode);
                }
                DeviceConfig &dc(config.getDeviceConfig());
                dc.setLoSupport(m_loSupport);
                dc.setMaxObjSize(m_maxObjSize);
                AccessConfig &ac(config.getAccessConfig());
                ac.setMaxMsgSize(m_maxMsgSize);
                EvolutionSyncClient::prepare(config, sources);
            }

        private:
            const SyncMode m_syncMode;
            const long m_maxMsgSize;
            const long m_maxObjSize;
            const bool m_loSupport;
            const char *m_encoding;
        } client(server, activeSources, syncMode, maxMsgSize, maxObjSize, loSupport, encoding);

        int res = client.sync();
        CPPUNIT_ASSERT(client.getSyncReport());
        checkReport.check(res, *client.getSyncReport());
        return res;
    }

    static bool compare(ClientTest &client, const char *fileA, const char *fileB) {
        std::string cmdstr = std::string("./synccompare ") + fileA + " " + fileB;
        return system(cmdstr.c_str()) == 0;
    }
    
private:
    string clientID;
    std::auto_ptr<TestEvolution> clientB;

    /** prefix to be used for Evolution databases */
    string evoPrefix;

    /** all sources that are active in the current test run */
    SourceType enabledSources[TEST_MAX_SOURCE];
    /** number of active sources */
    int numSources;

    /** returns the name corresponding to the type, using the same strings as the C++ client testing system */
    string getSourceName(SourceType type) {
        switch (type) {
#ifdef ENABLE_EBOOK
         case TEST_CONTACT21_SOURCE:
            return "vcard21";
            break;
         case TEST_CONTACT30_SOURCE:
            return "vcard30";
            break;
#endif
#ifdef ENABLE_ECAL
         case TEST_CALENDAR_SOURCE:
            return "ical20";
            break;
         case TEST_TASK_SOURCE:
            return "itodo20";
            break;
#endif
         default:
            CPPUNIT_ASSERT(type >= 0 && type < TEST_MAX_SOURCE);
            break;
        }
    }

    /** returns the name of the Evolution database */
    string getDatabaseName(SourceType type) {
        return evoPrefix + getSourceName(type) + "_" + clientID;
    }
    
    static SyncSource *createSource(ClientTest &client, int source, bool isSourceA) {
        string changeID = "SyncEvolution Change ID #";
        SourceType type = ((TestEvolution &)client).enabledSources[source];
        changeID += isSourceA ? "1" : "2";
        string database = ((TestEvolution &)client).getDatabaseName(type);
        
        switch (type) {
#ifdef ENABLE_EBOOK
         case TEST_CONTACT21_SOURCE:
         case TEST_CONTACT30_SOURCE:
            return new TestEvolutionSyncSource<EvolutionContactSource>(changeID, database);
            break;
#endif
#ifdef ENABLE_ECAL
         case TEST_CALENDAR_SOURCE:
            return new TestEvolutionSyncSource<EvolutionCalendarSource>(E_CAL_SOURCE_TYPE_EVENT, changeID, database);
            break;
         case TEST_TASK_SOURCE:
            return new TestEvolutionSyncSource<EvolutionCalendarSource>(E_CAL_SOURCE_TYPE_TODO, changeID, database);
#endif
         default:
            CPPUNIT_ASSERT(type >= 0 && type < TEST_MAX_SOURCE);
            return NULL;
        }
    }
};

static class RegisterTestEvolution {
public:
    RegisterTestEvolution() :
        testClient("1") {
        testClient.registerTests();
    }

private:
    TestEvolution testClient;
    
} testEvolution;
