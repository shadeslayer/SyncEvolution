/*
 * Copyright (C) 2005-2009 Patrick Ohly <patrick.ohly@gmx.de>
 * Copyright (C) 2009 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) version 3.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301  USA
 */

#include "config.h"

#include <ClientTest.h>

#include <exception>
#include <fstream>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "CmdlineSyncClient.h"
#include <syncevo/SyncSource.h>
#include <syncevo/util.h>
#include <syncevo/VolatileConfigNode.h>

#include <boost/bind.hpp>

#include <syncevo/declarations.h>

SE_BEGIN_CXX

/*
 * always provide this test class, even if not used:
 * that way the test scripts can unconditionally
 * invoke "client-test SyncEvolution"
 */
CPPUNIT_REGISTRY_ADD_TO_DEFAULT("SyncEvolution");

class EvolutionLocalTests : public LocalTests {
public:
    EvolutionLocalTests(const std::string &name, ClientTest &cl, int sourceParam, ClientTest::Config &co) :
        LocalTests(name, cl, sourceParam, co)
        {}

    virtual void addTests() {
        LocalTests::addTests();
    }
};

/**
 * This is a workaround for libecal/libebook in Evolution >= 2.30.
 * The storage daemons shut down after only 10 seconds of no client
 * being attached.  Due to limitations in libecal/libebook this is not
 * detected when only using the synchronous API ("destroyed" signal
 * not delivered, see e-cal.c), which then leads to D-Bus errors.
 *
 * The workaround consists of keeping one open SyncEvolution backend
 * around for each of eds_event and eds_contact/30, if they ever were used
 * during testing.
 */
static map<string, boost::shared_ptr<TestingSyncSource> > lockEvolution;
static void CleanupSources()
{
    lockEvolution.clear();
}

/**
 * This code uses the ClientTest and and information provided by
 * the backends in their RegisterSyncSourceTest instances to test
 * real synchronization with a server.
 *
 * Configuration is done by environment variables which indicate which
 * part below the root node "client-test" of the the configuration tree to use;
 * beyond that everything needed for synchronization is read from the
 * configuration tree.
 *
 * - CLIENT_TEST_SERVER = maps to name of root node in configuration tree
 * - CLIENT_TEST_EVOLUTION_PREFIX = a common "evolutionsource" prefix for *all*
 *                                  sources; the source name followed by "_[12]"
 *                                  is appended to get unique names
 * - CLIENT_TEST_EVOLUTION_USER = sets the "evolutionuser" property of all sources
 * - CLIENT_TEST_EVOLUTION_PASSWORD = sets the "evolutionpassword" property of all sources
 * - CLIENT_TEST_SOURCES = comma separated list of active sources,
 *                         names as selected in their RegisterSyncSourceTest
 *                         instances
 * - CLIENT_TEST_DELAY = number of seconds to sleep between syncs, required
 *                       by some servers
 * - CLIENT_TEST_LOG = logfile name of a server, can be empty:
 *                     if given, then the content of that file will be
 *                     copied and stored together with the client log
 *                     (only works on Unix)
 * - CLIENT_TEST_NUM_ITEMS = numbers of contacts/events/... to use during
 *                           local and sync tests which create artificial
 *                           items
 *
 * The CLIENT_TEST_SERVER also has another meaning: it is used as hint
 * by the synccompare.pl script and causes it to automatically ignore
 * known, acceptable data modifications caused by sending an item to
 * a server and back again. Currently the script recognizes "funambol",
 * "scheduleworld", "synthesis" and "egroupware" as special server
 * names.
 */
class TestEvolution : public ClientTest {
public:
    /**
     * can be instantiated as client A with id == "1" and client B with id == "2"
     */
    TestEvolution(const string &id) :
        ClientTest(getenv("CLIENT_TEST_DELAY") ? atoi(getenv("CLIENT_TEST_DELAY")) : 0,
                   getenv("CLIENT_TEST_LOG") ? getenv("CLIENT_TEST_LOG") : ""),
        m_initialized(false),
        m_clientID(id),
        m_configs(SyncSource::getTestRegistry())
    {
    }

    virtual std::string getClientID() const { return m_clientID; }

    /**
     * code depends on other global constructors to run first, execute it after constructor but before
     * any other methods
     */
    void init()
    {
        if (m_initialized) {
	    return;
        } else {
            m_initialized = true;
        }

        // allow each backend test to create more backend tests
        size_t count = 0;
        while (count != m_configs.size()) {
            count = m_configs.size();
            BOOST_FOREACH (const RegisterSyncSourceTest *test,
                           TestRegistry(m_configs)) {
                test->init();
            }
        }

        const char *server = getenv("CLIENT_TEST_SERVER");

        if (m_clientID == "1") {
            m_clientB.reset(new TestEvolution("2"));
        }

        /* check server */
        if (!server) {
            server = "funambol";
            setenv("CLIENT_TEST_SERVER", "funambol", 1);
        }

        /* override Evolution database names? */
        const char *evoprefix = getenv("CLIENT_TEST_EVOLUTION_PREFIX");
        m_evoPrefix = evoprefix ? evoprefix :  "SyncEvolution_Test_";
        const char *evouser = getenv("CLIENT_TEST_EVOLUTION_USER");
        if (evouser) {
            m_evoUser = evouser;
        }
        const char *evopasswd = getenv("CLIENT_TEST_EVOLUTION_PASSWORD");
        if (evopasswd) {
            m_evoPassword = evopasswd;
        }

        /* check sources */
        const char *sourcelist = getenv("CLIENT_TEST_SOURCES");
        set<string> sources;
        if (sourcelist) {
            boost::split(sources, sourcelist, boost::is_any_of(","));
        } else {
            BOOST_FOREACH(const RegisterSyncSourceTest *test, m_configs) {
                if (!test->m_configName.empty()) {
                    sources.insert(test->m_configName);
                }
            }
        }

        BOOST_FOREACH(const RegisterSyncSourceTest *test, m_configs) {
            if (sources.find(test->m_configName) != sources.end() &&
                !test->m_configName.empty()) {
                m_syncSource2Config.push_back(test->m_configName);
            }
        }

        /* Local Test SyncSource : remove all virtual datastores, inserting the
         * sub datastores*/
        ClientTest::Config conf;
        BOOST_FOREACH (string source, sources) {
            getSourceConfig (source, conf);
            if (!conf.m_subConfigs.empty()) {
                vector<string> subs;
                boost::split (subs, conf.m_subConfigs, boost::is_any_of(","));
                BOOST_FOREACH (string sub, subs) {
                    pushLocalSource2Config(sub);
                }
            } else {
                pushLocalSource2Config(source);
            }
        }
        // get configuration and set obligatory fields
        LoggerBase::instance().setLevel(Logger::DEBUG);
        std::string root = std::string("evolution/") + server + "_" + m_clientID;
        boost::shared_ptr<SyncConfig> config(new SyncConfig(string(server) + "_" + m_clientID));
        boost::shared_ptr<SyncConfig> from = boost::shared_ptr<SyncConfig> ();

        if (!config->exists()) {
            // no configuration yet, create in different contexts because
            // device ID is different
            config.reset(new SyncConfig(string(server) + "_" + m_clientID + "@client-test-" + m_clientID));
            config->setDefaults();
            from = SyncConfig::createPeerTemplate(server);
            if(from) {
                set<string> filter;
                config->copy(*from, &filter);
            }
            config->setDevID(m_clientID == "1" ? "sc-api-nat" : "sc-pim-ppc");
        }
        BOOST_FOREACH(const RegisterSyncSourceTest *test, m_configs) {
            if (test->m_configName.empty()) {
                continue;
            }

            ClientTest::Config testconfig;
            getSourceConfig(test, testconfig);
            CPPUNIT_ASSERT(!testconfig.m_type.empty());

            boost::shared_ptr<SyncSourceConfig> sc = config->getSyncSourceConfig(testconfig.m_sourceName);
            if (!sc || !sc->exists()) {
                // no configuration yet
                config->setSourceDefaults(testconfig.m_sourceName);
                sc = config->getSyncSourceConfig(testconfig.m_sourceName);
                CPPUNIT_ASSERT(sc);
                sc->setURI(testconfig.m_uri);
                if(from && !testconfig.m_sourceNameServerTemplate.empty()) {
                    boost::shared_ptr<SyncSourceConfig> scServerTemplate = from->getSyncSourceConfig(testconfig.m_sourceNameServerTemplate);
                    sc->setURI(scServerTemplate->getURI());
                }
            }

            // Set these properties if not set yet: that means the env
            // variables are used when creating the config initially,
            // but then no longer can be used to change the config.
            // This prevents accidentally running a test with default
            // values, for example for the database.
            if (!sc->getDatabaseID().wasSet()) {
                string database = getDatabaseName(test->m_configName);
                sc->setDatabaseID(database);
            }
            if (!sc->getUser().wasSet() && !m_evoUser.empty()) {
                sc->setUser(m_evoUser);
            }
            if (!sc->getPassword().wasSet() && !m_evoPassword.empty()) {
                sc->setPassword(m_evoPassword);
            }
            // Always set this one, to ensure the config matches the test.
            sc->setBackend(SourceType(testconfig.m_type).m_backend);
        }
        config->flush();
    }

    virtual LocalTests *createLocalTests(const std::string &name, int sourceParam, ClientTest::Config &co) {
        init();
        return new EvolutionLocalTests(name, *this, sourceParam, co);
    }

    virtual int getNumLocalSources() {
        init();
        return m_localSource2Config.size();
    }

    virtual int getNumSyncSources() {
        init();
        return m_syncSource2Config.size();
    }

    virtual void getLocalSourceConfig(int source, Config &config) {
        init();
        getSourceConfig(m_configs[m_localSource2Config[source]], config);
    }

    virtual void getSyncSourceConfig(int source, Config &config) {
        init();
        getSourceConfig(m_configs[m_syncSource2Config[source]], config);
    }

    virtual int getLocalSourcePosition(const string &configName) {
        for (size_t i=0; i< m_localSource2Config.size(); i++) {
            if(m_localSource2Config[i] == configName) {
                return i;
                break;
            }
        }
        return -1;
    }

    virtual void getSourceConfig (const string &configName, Config &config) {
        init();
        return getSourceConfig (m_configs[configName], config);
    }

    static void getSourceConfig(const RegisterSyncSourceTest *test, Config &config) {
        ClientTest::getTestData(test->m_testCaseName.c_str(), config);
        config.m_createSourceA = createSource;
        config.m_createSourceB = createSource;
        config.m_sourceName = test->m_configName.c_str();
        config.m_linkedSources = test->m_linkedSources;

        test->updateConfig(config);
    }

    virtual ClientTest *getClientB() {
        init();
        return m_clientB.get();
    }

    virtual bool isB64Enabled() {
        return false;
    }

    virtual SyncMLStatus doSync(const int *sources,
                                const std::string &logbase,
                                const SyncOptions &options)
    {
        init();

        // Let "Client_Sync_Current" symlink point to a new, empty
        // directory logbase + ".server". Can be used by SyncEvolution
        // server as per-test logdir.
        std::string current = logbase + ".server";
        rm_r(current);
        mkdir_p(current);
        rm_r("Client_Sync_Current");
        symlink(current.c_str(), "Client_Sync_Current");

        string server = getenv("CLIENT_TEST_SERVER") ? getenv("CLIENT_TEST_SERVER") : "funambol";
        server += "_";
        server += m_clientID;
        

        class ClientTest : public CmdlineSyncClient {
        public:
            ClientTest(const string &server,
                       const string &logbase,
                       const SyncOptions &options) :
                CmdlineSyncClient(server, false),
                m_logbase(logbase),
                m_options(options),
                m_started(false)
                {}

        protected:
            virtual void prepare() {
                setLogDir(m_logbase, true);
                setMaxLogDirs(0, true);
                setMaxObjSize(m_options.m_maxObjSize, true);
                setMaxMsgSize(m_options.m_maxMsgSize, true);
                setWBXML(m_options.m_isWBXML, true);
                setRetryDuration(m_options.m_retryDuration, true);
                setRetryInterval(m_options.m_retryInterval, true);
                if (m_options.m_syncMode == SYNC_TWO_WAY &&
                    m_options.m_checkReport.syncMode == SYNC_NONE) {
                    // For this test, any kind of final sync mode is
                    // acceptable. Disable slow sync prevention
                    // temporarily. The check for the requested sync
                    // mode is perhaps too conservative, but in
                    // practice the only test where slow sync
                    // prevention caused a test failure was
                    // Client::Sync::eds_contact::testTwoWaySync after
                    // some other failed test, so let's be conservative...
                    setPreventSlowSync(false);
                }
                SyncContext::prepare();
                if (m_options.m_prepareCallback &&
                    m_options.m_prepareCallback(*this, m_options)) {
                    m_options.m_isAborted = true;
                }
            }

            virtual void displaySyncProgress(sysync::TProgressEventEnum type,
                                             int32_t extra1, int32_t extra2, int32_t extra3)
            {
                if (!m_started) {
                    m_started = true;
                    if (m_options.m_startCallback(*this, m_options)) {
                        m_options.m_isAborted = true;
                    }
                }
            }

            virtual bool checkForAbort() { return m_options.m_isAborted; }
            virtual bool checkForSuspend() {return m_options.m_isSuspended;}

            virtual boost::shared_ptr<TransportAgent> createTransportAgent()
            {
                boost::shared_ptr<TransportAgent>wrapper = m_options.m_transport;
                boost::shared_ptr<TransportAgent>agent =SyncContext::createTransportAgent();
                if (!wrapper.get())
                    return agent;
                dynamic_cast<TransportWrapper*>(wrapper.get())->setAgent(agent);
                dynamic_cast<TransportWrapper*>(wrapper.get())->setSyncOptions(&m_options);
                return wrapper;
            }

        private:
            const string m_logbase;
            SyncOptions m_options;
            bool m_started;
        } client(server, logbase, options);

        // configure active sources with the desired sync mode,
        // disable the rest
        FilterConfigNode::ConfigFilter filter;
        filter["sync"] = InitStateString("none", true);
        client.setConfigFilter(false, "", filter);
        filter["sync"] =
            InitStateString(PrettyPrintSyncMode(options.m_syncMode), true);
        for(int i = 0; sources[i] >= 0; i++) {
            std::string &name = m_syncSource2Config[sources[i]];
            client.setConfigFilter(false, name, filter);
            checkEvolutionSource(name);
        }

        SyncReport report;
        SyncMLStatus status = client.sync(&report);
        options.m_checkReport.check(status, report);
        return status;
    }
  
private:
    bool m_initialized;
    string m_clientID;
    std::auto_ptr<TestEvolution> m_clientB;
    const TestRegistry &m_configs;

    /** prefix, username, password to be used for local databases */
    string m_evoPrefix, m_evoUser, m_evoPassword;

    /**
     * The ClientTest framework identifies active configs with an integer.
     * This is the mapping to the corresponding config name, created when
     * constructing this instance.
     */
    vector<string> m_localSource2Config;
    vector<string> m_syncSource2Config;

    /** returns the name of the Evolution database */
    string getDatabaseName(const string &configName) {
        if (configName == "calendar+todo") {
            return "eds_event,eds_task";
        } else if (configName == "file_calendar+todo") {
            return "file_event,file_task";
        }
        return m_evoPrefix + configName + "_" + m_clientID;
    }

    /** called by test frame work */
    static TestingSyncSource *createSource(ClientTest &client, const std::string &clientID, int source, bool isSourceA) {
        TestEvolution &evClient((TestEvolution &)client);
        string name = evClient.m_localSource2Config[source];

        // implement Evolution shutdown workaround (see lockEvolution above)
        evClient.checkEvolutionSource(name);

        return evClient.createNamedSource(name, isSourceA);
    }

    /** called internally in this class */
    TestingSyncSource *createNamedSource(const string &name, bool isSourceA) {
        string database = getDatabaseName(name);
        std::string config = "target-config@client-test";
        const char *server = getenv("CLIENT_TEST_SERVER");
        if (server) {
            config += "-";
            config += server;
        }
        std::string tracking =
            string("_") + m_clientID +
            "_" + (isSourceA ? "A" : "B");
        SE_LOG_DEBUG(NULL, NULL, "instantiating testing source %s in config %s, with tracking name %s",
                     name.c_str(),
                     config.c_str(),
                     tracking.c_str());
        boost::shared_ptr<SyncConfig> context(new SyncConfig(config));
        SyncSourceNodes nodes = context->getSyncSourceNodes(name, tracking);

        // The user of client-test must have configured the source
        // @<CLIENT_TEST_SERVER>_<m_clientID>/<name> when doing
        // Client::Sync testing.  Our testing source must use the same
        // properties, but different change tracking.
        std::string peerName = server ? (std::string(server) + "_" + m_clientID) : "@default";
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
        context->flush();

        // Same as in init() above: set values if still empty, but don't
        // overwrite anything.
        boost::shared_ptr<FilterConfigNode> props = nodes.getProperties();
        std::string value;
        if (!props->getProperty("database", value)) {
            props->setProperty("database", database);
        }
        if (!props->getProperty("databaseUser", value) &&
            !m_evoUser.empty()) {
            props->setProperty("databaseUser", m_evoUser);
        }
        if (!props->getProperty("databasePassword", value) &&
            !m_evoPassword.empty()) {
            props->setProperty("databasePassword", m_evoPassword);
        }

        SyncSourceParams params(name,
                                nodes,
                                context);
        const RegisterSyncSourceTest *test = m_configs[name];
        ClientTestConfig testConfig;
        getSourceConfig(test, testConfig);

        PersistentSyncSourceConfig sourceConfig(params.m_name, params.m_nodes);
        sourceConfig.setSourceType(SourceType(testConfig.m_type));

        // downcasting here: anyone who registers his sources for testing
        // must ensure that they are indeed TestingSyncSource instances
        SyncSource *ss = SyncSource::createSource(params);
        return static_cast<TestingSyncSource *>(ss);
    }

    // push source into localsource2config if it doesn't exist in the vector
    void pushLocalSource2Config(const string &source) {
        bool finded = false;
        BOOST_FOREACH(string element, m_localSource2Config) {
            if (boost::iequals(element, source)) {
                finded = true;
                break;
            }
        }
        if (!finded) {
            m_localSource2Config.push_back (source);
        }
    }

    void checkEvolutionSource(std::string &name)
    {
        string basename;

        // hard-coded names as used by src/backends/evolution;
        // if some other backend reuses them, it gets the
        // same treatment, which shouldn't cause any harm
        if (name == "eds_contact") {
            basename = "ebook";
        } else if (name == "eds_event" ||
                   name == "text") {
            basename = "ecal";
        }

        if (!basename.empty() &&
            lockEvolution.find(basename) == lockEvolution.end()) {
            lockEvolution[basename].reset(createNamedSource(name, true));
            lockEvolution[basename]->open();
            ClientTest::registerCleanup(CleanupSources);
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

SE_END_CXX
