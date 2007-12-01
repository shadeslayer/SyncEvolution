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
#include <fstream>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#ifdef HAVE_VALGRIND_VALGRIND_H
# include <valgrind/valgrind.h>
#endif
#ifdef HAVE_EXECINFO_H
# include <execinfo.h>
#endif

#include "EvolutionSyncClient.h"
#include "EvolutionCalendarSource.h"
#include "EvolutionMemoSource.h"
#include "EvolutionContactSource.h"
#include "SQLiteContactSource.h"
#include "AddressBookSource.h"

/**
 * A helper class for Mac OS X which switches between different
 * address books by renaming the database file. This is a hack
 * because it makes assumptions about the data storage - and it
 * does not work, apparently because the library caches information
 * in memory...
 *
 * The initial address book is called "system" and it will be
 * restored during normal termination - but don't count on that...
 */
class MacOSAddressBook {
public:
    /**
     * moves the current address book out of the way and
     * makes the one with the selected suffix the default one
     */
    void select(const string &suffix) {
        if (suffix != m_currentBook) {
            const char *home = getenv("HOME");
            int res;
            CPPUNIT_ASSERT(home);

            int wdfd = open(".", O_RDONLY);
            CPPUNIT_ASSERT(wdfd >= 0);
            res = chdir(home);
            CPPUNIT_ASSERT(res >= 0);
            res = chdir("Library/Application Support/AddressBook/");
            CPPUNIT_ASSERT(res >= 0);

            string baseName = "AddressBook.data";

            string oldBook = baseName + "." + m_currentBook;
            res = rename(baseName.c_str(), oldBook.c_str());
            printf("renamed %s to %s: %s\n",
                   baseName.c_str(),
                   oldBook.c_str(),
                   res >= 0 ? "successfully" : strerror(errno));
            CPPUNIT_ASSERT(res >= 0 || errno == ENOENT);

            string newBook = baseName + "." + suffix;
            res = rename(newBook.c_str(), baseName.c_str());
            printf("renamed %s to %s: %s\n",
                   newBook.c_str(),
                   baseName.c_str(),
                   res >= 0 ? "successfully" : strerror(errno));
            CPPUNIT_ASSERT(res >= 0 || errno == ENOENT);

            // touch it
            res = open(baseName.c_str(), O_WRONLY|O_CREAT, 0600);
            if (res >= 0) {
                close(res);
            }

            unlink("ABPerson.skIndexInverted");
            unlink("AddressBook.data.previous");

            // chdir(home);
            // chdir("Library/Caches/com.apple.AddressBook");
            // system("rm -rf MetaData");

            m_currentBook = suffix;

            res = fchdir(wdfd);
            CPPUNIT_ASSERT(res >= 0);
        }
    }

    static MacOSAddressBook &get() { return m_singleton; }

private:
    MacOSAddressBook() :
        m_currentBook("system") {}
    ~MacOSAddressBook() {
        select("system");
    }

    string m_currentBook;
    static MacOSAddressBook m_singleton;
};
MacOSAddressBook MacOSAddressBook::m_singleton;


/** a wrapper class which automatically does an open() in the constructor and a close() in the destructor */
template<class T> class TestEvolutionSyncSource : public T {
public:
    TestEvolutionSyncSource(ECalSourceType type, string changeID, string database) :
        T(type, "dummy", NULL, changeID, database) {}
    TestEvolutionSyncSource(string changeID, string database) :
        T("dummy", NULL, changeID, database) {}
    TestEvolutionSyncSource(string changeID, string database, string configPath) :
        T("dummy", NULL, changeID, database, configPath) {}

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

class EvolutionLocalTests : public LocalTests {
public:
    EvolutionLocalTests(const std::string &name, ClientTest &cl, int sourceParam, ClientTest::Config &co) :
        LocalTests(name, cl, sourceParam, co)
        {}

    virtual void addTests() {
        LocalTests::addTests();

#ifdef ENABLE_MAEMO
        if (config.createSourceA &&
            config.createSourceB &&
            config.templateItem &&
            strstr(config.templateItem, "BEGIN:VCARD") &&
            config.uniqueProperties) {
            ADD_TEST(EvolutionLocalTests, testOssoDelete);
        }
#endif
    }

private:

    // insert am item,
    // overwrite it with an additional X-OSSO-CONTACT-STATE:DELETED as Maemoe address book does,
    // iterate again and check that our own code deleted the item
    void testOssoDelete() {
        // get into clean state with one template item added
        deleteAll(createSourceA);
        insert(createSourceA, config.templateItem);

        // add X-OSSO-CONTACT-STATE:DELETED
        string item = config.templateItem;
        const char *comma = strchr(config.uniqueProperties, ':');
        size_t offset = item.find(config.uniqueProperties, 0,
                                  comma ? comma - config.uniqueProperties : strlen(config.uniqueProperties));
        CPPUNIT_ASSERT(offset != item.npos);
        item.insert(offset, "X-OSSO-CONTACT-STATE:DELETED\n");
        update(createSourceA, item.c_str(), false);

        // opening and preparing the source should delete the item
        std::auto_ptr<SyncSource> source;
        SOURCE_ASSERT_NO_FAILURE(source.get(), source.reset(createSourceA()));
        SOURCE_ASSERT(source.get(), source->beginSync() == 0 );
        CPPUNIT_ASSERT_EQUAL(0, countItemsOfType(source.get(), TOTAL_ITEMS));
        CPPUNIT_ASSERT_EQUAL(0, countItemsOfType(source.get(), NEW_ITEMS));
        CPPUNIT_ASSERT_EQUAL(0, countItemsOfType(source.get(), UPDATED_ITEMS));
        CPPUNIT_ASSERT_EQUAL(1, countItemsOfType(source.get(), DELETED_ITEMS));
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
            sourcelist = "vcard21,vcard30,ical20,text,itodo20,sqlite,addressbook";
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
            if (sourceType == TEST_CALENDAR_SOURCE ||
                sourceType == TEST_TASK_SOURCE ||
                sourceType == TEST_MEMO_SOURCE) {
                continue;
            }
#endif
#ifndef ENABLE_SQLITE
            if (sourceType == TEST_SQLITE_CONTACT_SOURCE) {
                continue;
            }
#endif
#ifndef ENABLE_ADDRESSBOOK
            if (sourceType == TEST_ADDRESS_BOOK_SOURCE) {
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

    virtual LocalTests *createLocalTests(const std::string &name, int sourceParam, ClientTest::Config &co) {
        return new EvolutionLocalTests(name, *this, sourceParam, co);
    }

    enum SourceType {
        TEST_CONTACT21_SOURCE,
        TEST_CONTACT30_SOURCE,
        TEST_CALENDAR_SOURCE,
        TEST_TASK_SOURCE,
        TEST_MEMO_SOURCE,
        TEST_SQLITE_CONTACT_SOURCE,
        TEST_ADDRESS_BOOK_SOURCE,
        TEST_MAX_SOURCE
    };

    virtual int getNumSources() {
        return numSources;
    }

    virtual void getSourceConfig(SourceType sourceType, Config &config) {
        memset(&config, 0, sizeof(config));
        
        switch (sourceType) {
         case TEST_CONTACT21_SOURCE:
            // we cannot use the C++ client libraries' vcard21 test
            // data because Evolution only imports vCard 3.0 items,
            // but we can use the vcard30 test data and synchronize
            // it as vCard 2.1
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
            config.type = "text/x-todo"; // special type required by SyncEvolution
            break;
         case TEST_MEMO_SOURCE:
            config.sourceName = "text";
            config.uri = "note"; // ScheduleWorld
            config.type = "text/plain";
            config.insertItem =
                "BEGIN:VCALENDAR\n"
                "PRODID:-//Ximian//NONSGML Evolution Calendar//EN\n"
                "VERSION:2.0\n"
                "METHOD:PUBLISH\n"
                "BEGIN:VJOURNAL\n"
                "SUMMARY:Summary\n"
                "DESCRIPTION:Summary\\nBody text\n"
                "END:VJOURNAL\n"
                "END:VCALENDAR\n";
            config.updateItem =
                "BEGIN:VCALENDAR\n"
                "PRODID:-//Ximian//NONSGML Evolution Calendar//EN\n"
                "VERSION:2.0\n"
                "METHOD:PUBLISH\n"
                "BEGIN:VJOURNAL\n"
                "SUMMARY:Summary Modified\n"
                "DESCRIPTION:Summary Modified\\nBody text\n"
                "END:VJOURNAL\n"
                "END:VCALENDAR\n";
            /* change summary, as in updateItem, and the body in the other merge item */
            config.mergeItem1 = config.updateItem;
            config.mergeItem2 =
                "BEGIN:VCALENDAR\n"
                "PRODID:-//Ximian//NONSGML Evolution Calendar//EN\n"
                "VERSION:2.0\n"
                "METHOD:PUBLISH\n"
                "BEGIN:VJOURNAL\n"
                "SUMMARY:Summary\n"
                "DESCRIPTION:Summary\\nBody modified\n"
                "END:VJOURNAL\n"
                "END:VCALENDAR\n";                
            config.templateItem = config.insertItem;
            config.uniqueProperties = "SUMMARY:DESCRIPTION";
            config.sizeProperty = "DESCRIPTION";
            config.import = ClientTest::import;
            config.dump = dumpMemoSource;
            config.testcases = "testcases/imemo20.ics";
            break;
         case TEST_SQLITE_CONTACT_SOURCE:
            getTestData("vcard21", config);
            config.sourceName = "sqlite";
            config.type = "sqlite";
            break;
         case TEST_ADDRESS_BOOK_SOURCE:
            getTestData("vcard30", config);
            config.sourceName = "addressbook";
            config.type = "addressbook";
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
            if (enabledSources[sources[i]] == TEST_ADDRESS_BOOK_SOURCE) {
                MacOSAddressBook::get().select(clientID);
            }
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
                    (*source)->getConfig().setEncoding(m_encoding ? m_encoding : "");
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
    string addressBookConfigPath;

    /** prefix to be used for Evolution databases */
    string evoPrefix;

    /** all sources that are active in the current test run */
    SourceType enabledSources[TEST_MAX_SOURCE];
    /** number of active sources */
    int numSources;

    /** returns the name corresponding to the type, using the same strings as the C++ client testing system */
    static string getSourceName(SourceType type) {
        switch (type) {
         case TEST_CONTACT21_SOURCE:
            return "vcard21";
            break;
         case TEST_CONTACT30_SOURCE:
            return "vcard30";
            break;
         case TEST_CALENDAR_SOURCE:
            return "ical20";
            break;
         case TEST_TASK_SOURCE:
            return "itodo20";
            break;
         case TEST_MEMO_SOURCE:
            return "text";
            break;
         case TEST_SQLITE_CONTACT_SOURCE:
            return "sqlite";
            break;
        case TEST_ADDRESS_BOOK_SOURCE:
            return "addressbook";
            break;
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
        SyncSource *ss = NULL;
        
        switch (type) {
         case TEST_CONTACT21_SOURCE:
         case TEST_CONTACT30_SOURCE:
#ifdef ENABLE_EBOOK
            ss = new TestEvolutionSyncSource<EvolutionContactSource>(changeID, database);
#endif
            break;
         case TEST_CALENDAR_SOURCE:
#ifdef ENABLE_ECAL
            ss = new TestEvolutionSyncSource<EvolutionCalendarSource>(E_CAL_SOURCE_TYPE_EVENT, changeID, database);
#endif
            break;
         case TEST_TASK_SOURCE:
#ifdef ENABLE_ECAL         
            ss = new TestEvolutionSyncSource<EvolutionCalendarSource>(E_CAL_SOURCE_TYPE_TODO, changeID, database);
#endif
            break;
         case TEST_MEMO_SOURCE:
#ifdef ENABLE_ECAL         
            ss = new TestEvolutionSyncSource<EvolutionMemoSource>(E_CAL_SOURCE_TYPE_JOURNAL, changeID, database);
#endif
            break;
         case TEST_SQLITE_CONTACT_SOURCE:
#ifdef ENABLE_SQLITE
            ss = new TestEvolutionSyncSource<SQLiteContactSource>(changeID, database);

            // this is a hack: it guesses the last sync time stamp by remembering
            // the last time the sync source was created
            static time_t lastts[TEST_MAX_SOURCE];
            char anchor[DIM_ANCHOR];
            time_t nextts;

            timestampToAnchor(lastts[type], anchor);
            ss->setLastAnchor(anchor);
            nextts = time(NULL);
            while (lastts[type] == nextts) {
                sleep(1);
                nextts = time(NULL);
            }
            lastts[type] = nextts;
#endif
            break;
         case TEST_ADDRESS_BOOK_SOURCE:
#ifdef ENABLE_ADDRESSBOOK
            MacOSAddressBook::get().select(((TestEvolution &)client).clientID);
            ss = new TestEvolutionSyncSource<AddressBookSource>(changeID, database, string("client-test-changes/") + ((TestEvolution &)client).getSourceName(type));
#endif
            break;
         default:
            CPPUNIT_ASSERT(type >= 0 && type < TEST_MAX_SOURCE);
        }

        return ss;
    }

    /**
     * dump memos in iCalendar 2.0 format for synccompare: ClientTest::dump() would
     * dump the plain text
     */
    static int dumpMemoSource(ClientTest &client, SyncSource &source, const char *file) {
        std::ofstream out(file);

#ifdef ENABLE_ECAL
        ((EvolutionMemoSource &)source).exportData(out);
#endif
        out.close();
        return out.bad();
    }

};

static void handler(int sig)
{
    void *buffer[100];
    int size;
    
    fprintf(stderr, "\ncaught signal %d\n", sig);
    fflush(stderr);
#ifdef HAVE_EXECINFO_H
    size = backtrace(buffer, sizeof(buffer)/sizeof(buffer[0]));
    backtrace_symbols_fd(buffer, size, 2);
#endif
#ifdef HAVE_VALGRIND_VALGRIND_H
    VALGRIND_PRINTF_BACKTRACE("\ncaught signal %d\n", sig);
#endif
    /* system("objdump -l -C -d client-test >&2"); */
    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_handler = SIG_DFL;
    sigaction(SIGABRT, &act, NULL);
    abort();
}

static class RegisterTestEvolution {
public:
    RegisterTestEvolution() :
        testClient("1") {
        struct sigaction act;

        memset(&act, 0, sizeof(act));
        act.sa_handler = handler;
        sigaction(SIGABRT, &act, NULL);
        sigaction(SIGSEGV, &act, NULL);
        sigaction(SIGILL, &act, NULL);

#if defined(HAVE_GLIB) && defined(HAVE_EDS)
        // this is required on Maemo and does not harm either on a normal
        // desktop system with Evolution
        g_type_init();
#endif
        testClient.registerTests();
    }

private:
    TestEvolution testClient;
    
} testEvolution;
