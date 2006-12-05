/*
 * Copyright (C) 2005 Patrick Ohly
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

#include <cppunit/extensions/HelperMacros.h>
#include <exception>

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
        CPPUNIT_ASSERT_NO_THROW(open());
        CPPUNIT_ASSERT(!hasFailed());
        return T::beginSync();
    }

    virtual int endSync() {
        CPPUNIT_ASSERT_NO_THROW(close());
        CPPUNIT_ASSERT(!hasFailed());
        return T::endSync();
    }
};

static bool compare(ClientTest &client, const char *fileA, const char *fileB)
{
    stringstream cmd;

    string diff = getCurrentTest() + ".diff";
    simplifyFilename(diff);
    cmd << "perl synccompare " << fileA << " " << fileB << ">" << diff;
    cmd << "  || (echo; echo '*** " << diff << " non-empty ***'; cat " << diff << "; exit 1 )";

    string cmdstr = cmd.str();
    return system(cmdstr.c_str()) == 0;
}

class TestEvolution : public ClientTest {
public:
    enum sourceType {
        TEST_CONTACT_SOURCE,
        TEST_CALENDAR_SOURCE,
#if 0
        TEST_TASK_SOURCE,
#endif
        TEST_MAX_SOURCE
    };

    virtual int getNumSources() {
        return TEST_MAX_SOURCE;
    }
    
    virtual void getSourceConfig(int source, Config &config) {
        const char *delaystr = getenv("TEST_EVOLUTION_DELAY");
        int delayseconds = delaystr ? atoi(delaystr) : 0;

        memset(&config, 0, sizeof(config));
        
        switch (source) {
         case TEST_CONTACT_SOURCE:
            config.sourceName = "Contact";
            config.createSourceA = createContactSourceA;
            config.createSourceB = createContactSourceB;
            config.serverDelaySeconds = delayseconds;
            config.insertItem =
                "BEGIN:VCARD\n"
                "VERSION:3.0\n"
                "TITLE:tester\n"
                "FN:John Doe\n"
                "N:Doe;John;;;\n"
                "TEL;TYPE=WORK;TYPE=VOICE:business 1\n"
                "X-EVOLUTION-FILE-AS:Doe\\, John\n"
                "X-MOZILLA-HTML:FALSE\n"
                "NOTE:\n"
                "END:VCARD\n";
            config.updateItem =
                "BEGIN:VCARD\n"
                "VERSION:3.0\n"
                "TITLE:tester\n"
                "FN:Joan Doe\n"
                "N:Doe;Joan;;;\n"
                "X-EVOLUTION-FILE-AS:Doe\\, Joan\n"
                "TEL;TYPE=WORK;TYPE=VOICE:business 2\n"
                "BDAY:2006-01-08\n"
                "X-MOZILLA-HTML:TRUE\n"
                "END:VCARD\n";
            /* adds a second phone number: */
            config.complexUpdateItem =
                "BEGIN:VCARD\n"
                "VERSION:3.0\n"
                "TITLE:tester\n"
                "FN:Joan Doe\n"
                "N:Doe;Joan;;;\n"
                "X-EVOLUTION-FILE-AS:Doe\\, Joan\n"
                "TEL;TYPE=WORK;TYPE=VOICE:business 1\n"
                "TEL;TYPE=HOME;TYPE=VOICE:home 2\n"
                "BDAY:2006-01-08\n"
                "X-MOZILLA-HTML:TRUE\n"
                "END:VCARD\n";
            /* add a telephone number, email and X-AIM to initial item */
            config.mergeItem1 =
                "BEGIN:VCARD\n"
                "VERSION:3.0\n"
                "TITLE:tester\n"
                "FN:John Doe\n"
                "N:Doe;John;;;\n"
                "X-EVOLUTION-FILE-AS:Doe\\, John\n"
                "X-MOZILLA-HTML:FALSE\n"
                "TEL;TYPE=WORK;TYPE=VOICE:business 1\n"
                "EMAIL:john.doe@work.com\n"
                "X-AIM:AIM JOHN\n"
                "END:VCARD\n";
            config.mergeItem2 =
                "BEGIN:VCARD\n"
                "VERSION:3.0\n"
                "TITLE:developer\n"
                "FN:John Doe\n"
                "N:Doe;John;;;\n"
                "X-EVOLUTION-FILE-AS:Doe\\, John\n"
                "X-MOZILLA-HTML:TRUE\n"
                "BDAY:2006-01-08\n"
                "END:VCARD\n";
            config.templateItem = config.insertItem;
            config.uniqueProperties = "FN:N:X-EVOLUTION-FILE-AS";
            config.sizeProperty = "NOTE";
            config.import = ClientTest::import;
            config.dump = ClientTest::dump;
            config.compare = compare;
            config.testcases = "addressbook.tests";
            break;
         case TEST_CALENDAR_SOURCE:
            config.sourceName = "Calendar";
            config.createSourceA = createCalendarSourceA;
            config.createSourceB = createCalendarSourceB;
            config.serverDelaySeconds = delayseconds;
            break;
        }
    }

    virtual ClientTest *getClientB() {
        return NULL;
    }

    virtual int sync(
        const int *sources,
        SyncMode syncMode,
        long maxMsgSize = 0,
        long maxObjSize = 0,
        bool loSupport = false,
        const char *encoding = NULL) {
        set<string> activeSources;
        for(int i = 0; sources[i] >= 0; i++) {
            string database;
            
            switch (sources[i]) {
             case TEST_CONTACT_SOURCE:
                database = "addressbook";
                break;
             case TEST_CALENDAR_SOURCE:
                database = "calendar";
                break;
#if 0
             case TEST_TASK_SOURCE:
                database = "task";
                break;
#endif
             default:
                CPPUNIT_ASSERT(sources[i] < TEST_MAX_SOURCE);
                break;
            }

            /* TODO */
            activeSources.insert(database + "_1");
        }

        /* TODO */
        string server = "funambol_1";
        
        class ClientTest : public EvolutionSyncClient {
        public:
            ClientTest(const string &server,
                           const set<string> &activeSources,
                           SyncMode syncMode,
                           long maxMsgSize,
                           long maxObjSize,
                           bool loSupport,
                           const char *encoding) :
                EvolutionSyncClient(server, false, activeSources),
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

        return client.sync();
    }
    
private:
    SyncSource *createSyncSource(sourceType type, string changeID, string database) {
        switch (type) {
         case TEST_CONTACT_SOURCE:
            return new TestEvolutionSyncSource<EvolutionContactSource>(changeID, database);
            break;
         case TEST_CALENDAR_SOURCE:
            return new TestEvolutionSyncSource<EvolutionCalendarSource>(E_CAL_SOURCE_TYPE_EVENT, changeID, database);
            break;
#if 0
         case TEST_TASK_SOURCE:
            return new TestEvolutionSyncSource<EvolutionCalendarSource>(E_CAL_SOURCE_TYPE_TASK, changeID, database);
#endif
         default:
            return NULL;
        }
    }

    static SyncSource *createContactSourceA(ClientTest &client) {
        return ((TestEvolution *)&client)->createSyncSource(TEST_CONTACT_SOURCE, "SyncEvolution Change ID #1", "SyncEvolution test #1");
    }
    static SyncSource *createContactSourceB(ClientTest &client) {
        return ((TestEvolution *)&client)->createSyncSource(TEST_CONTACT_SOURCE, "SyncEvolution Change ID #2", "SyncEvolution test #1");
    }

    static SyncSource *createCalendarSourceA(ClientTest &client) {
        return ((TestEvolution *)&client)->createSyncSource(TEST_CALENDAR_SOURCE, "SyncEvolution Change ID #1", "SyncEvolution test #1");
    }
    static SyncSource *createCalendarSourceB(ClientTest &client) {
        return ((TestEvolution *)&client)->createSyncSource(TEST_CALENDAR_SOURCE, "SyncEvolution Change ID #2", "SyncEvolution test #1");
    }

#if 0
    static SyncSource *createTaskSourceA(ClientTest &client) {
        return ((TestEvolution *)&client)->createSyncSource(TEST_TASK_SOURCE, "SyncEvolution Change ID #1", "SyncEvolution test #1");
    }
    static SyncSource *createTaskSourceB(ClientTest &client) {
        return ((TestEvolution *)&client)->createSyncSource(TEST_TASK_SOURCE, "SyncEvolution Change ID #2", "SyncEvolution test #1");
    }
#endif
};

static class RegisterTestEvolution {
public:
    RegisterTestEvolution() {
        testClient.registerTests();
    }

private:
    TestEvolution testClient;
    
} testEvolution;
