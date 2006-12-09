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
        CPPUNIT_ASSERT_NO_THROW(open());
        CPPUNIT_ASSERT(!hasFailed());
        return T::beginSync();
    }

    virtual int endSync() {
        int res = T::endSync();
        CPPUNIT_ASSERT_NO_THROW(close());
        CPPUNIT_ASSERT(!hasFailed());
        return res;
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
    /**
     * can be instantiated as client A with id == "1" and client B with id == "2"
     */
    TestEvolution(const string &id) :
        ClientTest(getenv("TEST_EVOLUTION_DELAY") ? atoi(getenv("TEST_EVOLUTION_DELAY")) : 0,
                   getenv("TEST_EVOLUTION_LOG") ? getenv("TEST_EVOLUTION_LOG") : ""),
        clientID(id) {
        if (id == "1") {
            clientB = new TestEvolution("2");
        } else {
            clientB = NULL;
        }
    }

    ~TestEvolution() {
        if (clientB) {
            delete clientB;
        }
    }   

    enum sourceType {
#ifdef ENABLE_EBOOK
        TEST_CONTACT_SOURCE,
#endif
#ifdef ENABLE_ECAL
        TEST_CALENDAR_SOURCE,
        TEST_TASK_SOURCE,
#endif
        TEST_MAX_SOURCE
    };

    virtual int getNumSources() {
        return TEST_MAX_SOURCE;
    }
    
    virtual void getSourceConfig(int source, Config &config) {
        memset(&config, 0, sizeof(config));
        
        switch (source) {
#ifdef ENABLE_EBOOK
         case TEST_CONTACT_SOURCE:
            config.sourceName = "Contact";
            config.createSourceA = createContactSourceA;
            config.createSourceB = createContactSourceB;
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
#endif /* ENABLE_EBOOK */
#ifdef ENABLE_ECAL
         case TEST_CALENDAR_SOURCE:
            config.sourceName = "Calendar";
            config.createSourceA = createCalendarSourceA;
            config.createSourceB = createCalendarSourceB;
            config.insertItem =
                "BEGIN:VCALENDAR\n"
                "PRODID:-//Ximian//NONSGML Evolution Calendar//EN\n"
                "VERSION:2.0\n"
                "METHOD:PUBLISH\n"
                "BEGIN:VEVENT\n"
                "SUMMARY:phone meeting\n"
                "DTEND;20060406T163000Z\n"
                "DTSTART;20060406T160000Z\n"
                "UID:1234567890!@#$%^&*()<>@dummy\n"
                "DTSTAMP:20060406T211449Z\n"
                "LAST-MODIFIED:20060409T213201\n"
                "CREATED:20060409T213201\n"
                "LOCATION:my office\n"
                "DESCRIPTION:let's talk\n"
                "CLASS:PUBLIC\n"
                "TRANSP:OPAQUE\n"
                "SEQUENCE:1\n"
                "END:VEVENT\n"
                "END:VCALENDAR\n";
            config.updateItem =
                "BEGIN:VCALENDAR\n"
                "PRODID:-//Ximian//NONSGML Evolution Calendar//EN\n"
                "VERSION:2.0\n"
                "METHOD:PUBLISH\n"
                "BEGIN:VEVENT\n"
                "SUMMARY:meeting on site\n"
                "DTEND;20060406T163000Z\n"
                "DTSTART;20060406T160000Z\n"
                "UID:1234567890!@#$%^&*()<>@dummy\n"
                "DTSTAMP:20060406T211449Z\n"
                "LAST-MODIFIED:20060409T213201\n"
                "CREATED:20060409T213201\n"
                "LOCATION:big meeting room\n"
                "DESCRIPTION:nice to see you\n"
                "CLASS:PUBLIC\n"
                "TRANSP:OPAQUE\n"
                "SEQUENCE:1\n"
                "END:VEVENT\n"
                "END:VCALENDAR\n";
            config.complexUpdateItem = NULL;
            /* change location in insertItem in testMerge() */
            config.mergeItem1 =
                "BEGIN:VCALENDAR\n"
                "PRODID:-//Ximian//NONSGML Evolution Calendar//EN\n"
                "VERSION:2.0\n"
                "METHOD:PUBLISH\n"
                "BEGIN:VEVENT\n"
                "SUMMARY:phone meeting\n"
                "DTEND;20060406T163000Z\n"
                "DTSTART;20060406T160000Z\n"
                "UID:1234567890!@#$%^&*()<>@dummy\n"
                "DTSTAMP:20060406T211449Z\n"
                "LAST-MODIFIED:20060409T213201\n"
                "CREATED:20060409T213201\n"
                "LOCATION:calling from home\n"
                "DESCRIPTION:let's talk\n"
                "CLASS:PUBLIC\n"
                "TRANSP:OPAQUE\n"
                "SEQUENCE:1\n"
                "END:VEVENT\n"
                "END:VCALENDAR\n";
            config.mergeItem2 =
                "BEGIN:VCALENDAR\n"
                "PRODID:-//Ximian//NONSGML Evolution Calendar//EN\n"
                "VERSION:2.0\n"
                "METHOD:PUBLISH\n"
                "BEGIN:VEVENT\n"
                "SUMMARY:phone meeting\n"
                "DTEND;20060406T163000Z\n"
                "DTSTART;20060406T160000Z\n"
                "UID:1234567890!@#$%^&*()<>@dummy\n"
                "DTSTAMP:20060406T211449Z\n"
                "LAST-MODIFIED:20060409T213201\n"
                "CREATED:20060409T213201\n"
                "LOCATION:my office\n"
                "DESCRIPTION:what the heck\\, let's even shout a bit\n"
                "CLASS:PUBLIC\n"
                "TRANSP:OPAQUE\n"
                "SEQUENCE:1\n"
                "END:VEVENT\n"
                "END:VCALENDAR\n";
            config.templateItem = config.insertItem;
            config.uniqueProperties = "SUMMARY:UID";
            config.sizeProperty = "DESCRIPTION";
            config.import = ClientTest::import;
            config.dump = ClientTest::dump;
            config.compare = compare;
            config.testcases = "calendar.tests";
            break;
         case TEST_TASK_SOURCE:
            config.sourceName = "Todo";
            config.createSourceA = createTaskSourceA;
            config.createSourceB = createTaskSourceB;
            config.insertItem =
                "BEGIN:VCALENDAR\n"
                "PRODID:-//Ximian//NONSGML Evolution Calendar//EN\n"
                "VERSION:2.0\n"
                "METHOD:PUBLISH\n"
                "BEGIN:VTODO\n"
                "UID:20060417T173712Z-4360-727-1-2730@gollum\n"
                "DTSTAMP:20060417T173712Z\n"
                "SUMMARY:do me\n"
                "DESCRIPTION:to be done\n"
                "PRIORITY:0\n"
                "STATUS:IN-PROCESS\n"
                "CREATED:20060417T173712\n"
                "LAST-MODIFIED:20060417T173712\n"
                "END:VTODO\n"
                "END:VCALENDAR\n";
            config.updateItem =
                "BEGIN:VCALENDAR\n"
                "PRODID:-//Ximian//NONSGML Evolution Calendar//EN\n"
                "VERSION:2.0\n"
                "METHOD:PUBLISH\n"
                "BEGIN:VTODO\n"
                "UID:20060417T173712Z-4360-727-1-2730@gollum\n"
                "DTSTAMP:20060417T173712Z\n"
                "SUMMARY:do me ASAP\n"
                "DESCRIPTION:to be done\n"
                "PRIORITY:1\n"
                "STATUS:IN-PROCESS\n"
                "CREATED:20060417T173712\n"
                "LAST-MODIFIED:20060417T173712\n"
                "END:VTODO\n"
                "END:VCALENDAR\n";
            config.complexUpdateItem = NULL;
            /* change summary in insertItem in testMerge() */
            config.mergeItem1 =
                "BEGIN:VCALENDAR\n"
                "PRODID:-//Ximian//NONSGML Evolution Calendar//EN\n"
                "VERSION:2.0\n"
                "METHOD:PUBLISH\n"
                "BEGIN:VTODO\n"
                "UID:20060417T173712Z-4360-727-1-2730@gollum\n"
                "DTSTAMP:20060417T173712Z\n"
                "SUMMARY:do me please\\, please\n"
                "DESCRIPTION:to be done\n"
                "PRIORITY:0\n"
                "STATUS:IN-PROCESS\n"
                "CREATED:20060417T173712\n"
                "LAST-MODIFIED:20060417T173712\n"
                "END:VTODO\n"
                "END:VCALENDAR\n";
            config.mergeItem2 =
                "BEGIN:VCALENDAR\n"
                "PRODID:-//Ximian//NONSGML Evolution Calendar//EN\n"
                "VERSION:2.0\n"
                "METHOD:PUBLISH\n"
                "BEGIN:VTODO\n"
                "UID:20060417T173712Z-4360-727-1-2730@gollum\n"
                "DTSTAMP:20060417T173712Z\n"
                "SUMMARY:do me\n"
                "DESCRIPTION:to be done\n"
                "PRIORITY:7\n"
                "STATUS:IN-PROCESS\n"
                "CREATED:20060417T173712\n"
                "LAST-MODIFIED:20060417T173712\n"
                "END:VTODO\n"
                "END:VCALENDAR\n";
            config.templateItem = config.insertItem;
            config.uniqueProperties = "SUMMARY:UID";
            config.sizeProperty = "DESCRIPTION";
            config.import = ClientTest::import;
            config.dump = ClientTest::dump;
            config.compare = compare;
            config.testcases = "todo.tests";
            break;
#endif /* ENABLE_ECAL */
         default:
            CPPUNIT_ASSERT(source < TEST_MAX_SOURCE);
            break;
        }
    }

    virtual ClientTest *getClientB() {
        return clientB;
    }

    virtual bool isB64Enabled() {
        return false;
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
#ifdef ENABLE_EBOOK
             case TEST_CONTACT_SOURCE:
                database = "addressbook";
                break;
#endif
#ifdef ENABLE_ECAL
             case TEST_CALENDAR_SOURCE:
                database = "calendar";
                break;
             case TEST_TASK_SOURCE:
                database = "task";
                break;
#endif
             default:
                CPPUNIT_ASSERT(sources[i] < TEST_MAX_SOURCE);
                break;
            }

            activeSources.insert(database + "_" + clientID);
        }

        string server = getenv("TEST_EVOLUTION_SERVER") ? getenv("TEST_EVOLUTION_SERVER") : "funambol";
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
    string clientID;
    TestEvolution *clientB;
    
    SyncSource *createSyncSource(sourceType type, string changeID, string database) {
        switch (type) {
#ifdef ENABLE_EBOOK
         case TEST_CONTACT_SOURCE:
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
            return NULL;
        }
    }

#ifdef ENABLE_EBOOK
    static SyncSource *createContactSourceA(ClientTest &client) {
        return ((TestEvolution *)&client)->createSyncSource(TEST_CONTACT_SOURCE, "SyncEvolution Change ID #1",
                                                            string("SyncEvolution test #") + ((TestEvolution &)client).clientID);
    }
    static SyncSource *createContactSourceB(ClientTest &client) {
        return ((TestEvolution *)&client)->createSyncSource(TEST_CONTACT_SOURCE, "SyncEvolution Change ID #2",
                                                            string("SyncEvolution test #") + ((TestEvolution &)client).clientID);
    }
#endif

#ifdef ENABLE_ECAL
    static SyncSource *createCalendarSourceA(ClientTest &client) {
        return ((TestEvolution *)&client)->createSyncSource(TEST_CALENDAR_SOURCE, "SyncEvolution Change ID #1",
                                                            string("SyncEvolution test #") + ((TestEvolution &)client).clientID);
    }
    static SyncSource *createCalendarSourceB(ClientTest &client) {
        return ((TestEvolution *)&client)->createSyncSource(TEST_CALENDAR_SOURCE, "SyncEvolution Change ID #2",
                                                            string("SyncEvolution test #") + ((TestEvolution &)client).clientID);
    }

    static SyncSource *createTaskSourceA(ClientTest &client) {
        return ((TestEvolution *)&client)->createSyncSource(TEST_TASK_SOURCE, "SyncEvolution Change ID #1",
                                                            string("SyncEvolution test #") + ((TestEvolution &)client).clientID);
    }
    static SyncSource *createTaskSourceB(ClientTest &client) {
        return ((TestEvolution *)&client)->createSyncSource(TEST_TASK_SOURCE, "SyncEvolution Change ID #2",
                                                            string("SyncEvolution test #") + ((TestEvolution &)client).clientID);
    }
#endif
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
