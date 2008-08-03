/*
 * Copyright (C) 2008 Patrick Ohly
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

#include "EvolutionCalendarSource.h"
#include "EvolutionMemoSource.h"
#include "SyncEvolutionUtil.h"

#include <boost/algorithm/string.hpp>

static EvolutionSyncSource *createSource(const EvolutionSyncSourceParams &params)
{
    pair <string, string> sourceType = EvolutionSyncSource::getSourceType(params.m_nodes);
    bool isMe;

    isMe = sourceType.first == "Evolution Task List";
    if (isMe || sourceType.first == "todo") {
        if (sourceType.second == "" || sourceType.second == "text/calendar") {
#ifdef ENABLE_ECAL
            return new EvolutionCalendarSource(E_CAL_SOURCE_TYPE_TODO, params);
#else
            return isMe ? RegisterSyncSource::InactiveSource : NULL;
#endif
        } else {
            return NULL;
        }
    }

    isMe = sourceType.first == "Evolution Memos";
    if (isMe || sourceType.first == "memo") {
        if (sourceType.second == "" || sourceType.second == "text/plain") {
#ifdef ENABLE_ECAL
            return new EvolutionMemoSource(params);
#else
            return isMe ? RegisterSyncSource::InactiveSource : NULL;
#endif
        } else if (sourceType.second == "text/calendar") {
#ifdef ENABLE_ECAL
            return new EvolutionCalendarSource(E_CAL_SOURCE_TYPE_JOURNAL, params);
#else
            return isMe ? RegisterSyncSource::InactiveSource : NULL;
#endif
        } else {
            return NULL;
        }
    }

    isMe = sourceType.first == "Evolution Calendar";
    if (isMe || sourceType.first == "calendar") {
        if (sourceType.second == "" || sourceType.second == "text/calendar" ||
            sourceType.second == "text/x-vcalendar" /* this is for backwards compatibility with broken configs */ ) {
#ifdef ENABLE_ECAL
            return new EvolutionCalendarSource(E_CAL_SOURCE_TYPE_EVENT, params);
#else
            return isMe ? RegisterSyncSource::InactiveSource : NULL;
#endif
        } else {
            return NULL;
        }
    }

    return NULL;
}

static RegisterSyncSource registerMe("Evolution Calendar/Task List/Memos",
#ifdef ENABLE_ECAL
                                     true,
#else
                                     false,
#endif
                                     createSource,
                                     "Evolution Calendar = calendar = events = evolution-events\n"
                                     "   always uses iCalendar 2.0\n"
                                     "Evolution Task List = Evolution Tasks = todo = tasks = evolution-tasks\n"
                                     "   always uses iCalendar 2.0\n"
                                     "Evolution Memos = memo = memos = evolution-memos\n"
                                     "   plain text in UTF-8 (default) = text/plain\n"
                                     "   iCalendar 2.0 = text/calendar\n"
                                     "   The later format is not tested because none of the\n"
                                     "   supported SyncML servers accepts it.\n",
                                     Values() +
                                     (Aliases("Evolution Calendar") + "evolution-calendar") +
                                     (Aliases("Evolution Task List") + "Evolution Tasks" + "evolution-tasks") +
                                     (Aliases("Evolution Memos") + "evolution-memos"));

#ifdef ENABLE_ECAL
#ifdef ENABLE_UNIT_TESTS

class EvolutionCalendarTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(EvolutionCalendarTest);
    CPPUNIT_TEST(testInstantiate);
    CPPUNIT_TEST(testOpenDefaultCalendar);
    CPPUNIT_TEST(testOpenDefaultTodo);
    CPPUNIT_TEST(testOpenDefaultMemo);
    CPPUNIT_TEST(testTimezones);
    CPPUNIT_TEST_SUITE_END();

protected:
    static string addItem(boost::shared_ptr<EvolutionSyncSource> source,
                          string &data) {
        SyncItem item;

        item.setData(data.c_str(), data.size());
        source->addItemThrow(item);
        CPPUNIT_ASSERT(item.getKey());
        return item.getKey();
    }

    void testInstantiate() {
        boost::shared_ptr<EvolutionSyncSource> source;
        source.reset(EvolutionSyncSource::createTestingSource("calendar", "calendar", true));
        source.reset(EvolutionSyncSource::createTestingSource("calendar", "evolution-calendar", true));
        source.reset(EvolutionSyncSource::createTestingSource("calendar", "Evolution Calendar:text/calendar", true));

        source.reset(EvolutionSyncSource::createTestingSource("calendar", "tasks", true));
        source.reset(EvolutionSyncSource::createTestingSource("calendar", "evolution-tasks", true));
        source.reset(EvolutionSyncSource::createTestingSource("calendar", "Evolution Tasks", true));
        source.reset(EvolutionSyncSource::createTestingSource("calendar", "Evolution Task List:text/calendar", true));

        source.reset(EvolutionSyncSource::createTestingSource("calendar", "memos", true));
        source.reset(EvolutionSyncSource::createTestingSource("calendar", "evolution-memos", true));
        source.reset(EvolutionSyncSource::createTestingSource("calendar", "Evolution Memos:text/plain", true));
        source.reset(EvolutionSyncSource::createTestingSource("calendar", "Evolution Memos:text/calendar", true));
    }

    void testOpenDefaultCalendar() {
        boost::shared_ptr<EvolutionSyncSource> source;
        source.reset(EvolutionSyncSource::createTestingSource("calendar", "evolution-calendar", true, NULL));
        CPPUNIT_ASSERT_NO_THROW(source->open());
    }

    void testOpenDefaultTodo() {
        boost::shared_ptr<EvolutionSyncSource> source;
        source.reset(EvolutionSyncSource::createTestingSource("calendar", "evolution-tasks", true, NULL));
        CPPUNIT_ASSERT_NO_THROW(source->open());
    }

    void testOpenDefaultMemo() {
        boost::shared_ptr<EvolutionSyncSource> source;
        source.reset(EvolutionSyncSource::createTestingSource("calendar", "evolution-memos", true, NULL));
        CPPUNIT_ASSERT_NO_THROW(source->open());
    }

    void testTimezones() {
        const char *prefix = getenv("CLIENT_TEST_EVOLUTION_PREFIX");
        if (!prefix) {
            prefix = "SyncEvolution_Test_";
        }

        boost::shared_ptr<EvolutionSyncSource> source;
        source.reset(EvolutionSyncSource::createTestingSource("ical20", "evolution-calendar", true, prefix));
        CPPUNIT_ASSERT_NO_THROW(source->open());

        string newyork = 
            "BEGIN:VCALENDAR\n"
            "PRODID:-//Ximian//NONSGML Evolution Calendar//EN\n"
            "VERSION:2.0\n"
            "BEGIN:VTIMEZONE\n"
            "TZID:America/New_York\n"
            "BEGIN:STANDARD\n"
            "TZOFFSETFROM:-0400\n"
            "TZOFFSETTO:-0500\n"
            "TZNAME:EST\n"
            "DTSTART:19701025T020000\n"
            "RRULE:FREQ=YEARLY;INTERVAL=1;BYDAY=-1SU;BYMONTH=10\n"
            "END:STANDARD\n"
            "BEGIN:DAYLIGHT\n"
            "TZOFFSETFROM:-0500\n"
            "TZOFFSETTO:-0400\n"
            "TZNAME:EDT\n"
            "DTSTART:19700405T020000\n"
            "RRULE:FREQ=YEARLY;INTERVAL=1;BYDAY=1SU;BYMONTH=4\n"
            "END:DAYLIGHT\n"
            "END:VTIMEZONE\n"
            "BEGIN:VEVENT\n"
            "UID:artificial\n"
            "DTSTAMP:20060416T205224Z\n"
            "DTSTART;TZID=America/New_York:20060406T140000\n"
            "DTEND;TZID=America/New_York:20060406T143000\n"
            "TRANSP:OPAQUE\n"
            "SEQUENCE:2\n"
            "SUMMARY:timezone New York with custom definition\n"
            "DESCRIPTION:timezone New York with custom definition\n"
            "CLASS:PUBLIC\n"
            "CREATED:20060416T205301Z\n"
            "LAST-MODIFIED:20060416T205301Z\n"
            "END:VEVENT\n"
            "END:VCALENDAR\n";

        string luid;
        CPPUNIT_ASSERT_NO_THROW(luid = addItem(source, newyork));

        string newyork_suffix = newyork;
        boost::replace_first(newyork_suffix,
                             "UID:artificial",
                             "UID:artificial-2");
        boost::replace_all(newyork_suffix,
                           "TZID:America/New_York",
                           "TZID://FOOBAR/America/New_York-SUFFIX");
        CPPUNIT_ASSERT_NO_THROW(luid = addItem(source, newyork_suffix));

        
        string notimezone = 
            "BEGIN:VCALENDAR\n"
            "PRODID:-//Ximian//NONSGML Evolution Calendar//EN\n"
            "VERSION:2.0\n"
            "BEGIN:VEVENT\n"
            "UID:artificial-3\n"
            "DTSTAMP:20060416T205224Z\n"
            "DTSTART;TZID=America/New_York:20060406T140000\n"
            "DTEND;TZID=America/New_York:20060406T143000\n"
            "TRANSP:OPAQUE\n"
            "SEQUENCE:2\n"
            "SUMMARY:timezone New York without custom definition\n"
            "DESCRIPTION:timezone New York without custom definition\n"
            "CLASS:PUBLIC\n"
            "CREATED:20060416T205301Z\n"
            "LAST-MODIFIED:20060416T205301Z\n"
            "END:VEVENT\n"
            "END:VCALENDAR\n";
        CPPUNIT_ASSERT_NO_THROW(luid = addItem(source, notimezone));

        // fake VTIMEZONE where daylight saving starts on first Sunday in March
        string fake_march = 
            "BEGIN:VCALENDAR\n"
            "PRODID:-//Ximian//NONSGML Evolution Calendar//EN\n"
            "VERSION:2.0\n"
            "BEGIN:VTIMEZONE\n"
            "TZID:FAKE\n"
            "BEGIN:STANDARD\n"
            "TZOFFSETFROM:-0400\n"
            "TZOFFSETTO:-0500\n"
            "TZNAME:EST MARCH\n"
            "DTSTART:19701025T020000\n"
            "RRULE:FREQ=YEARLY;INTERVAL=1;BYDAY=-1SU;BYMONTH=10\n"
            "END:STANDARD\n"
            "BEGIN:DAYLIGHT\n"
            "TZOFFSETFROM:-0500\n"
            "TZOFFSETTO:-0400\n"
            "TZNAME:EDT\n"
            "DTSTART:19700405T020000\n"
            "RRULE:FREQ=YEARLY;INTERVAL=1;BYDAY=1SU;BYMONTH=3\n"
            "END:DAYLIGHT\n"
            "END:VTIMEZONE\n"
            "BEGIN:VEVENT\n"
            "UID:artificial-4\n"
            "DTSTAMP:20060416T205224Z\n"
            "DTSTART;TZID=FAKE:20060406T140000\n"
            "DTEND;TZID=FAKE:20060406T143000\n"
            "TRANSP:OPAQUE\n"
            "SEQUENCE:2\n"
            "SUMMARY:fake timezone with daylight starting in March\n"
            "CLASS:PUBLIC\n"
            "CREATED:20060416T205301Z\n"
            "LAST-MODIFIED:20060416T205301Z\n"
            "END:VEVENT\n"
            "END:VCALENDAR\n";
        CPPUNIT_ASSERT_NO_THROW(luid = addItem(source, fake_march));

        string fake_may = fake_march;
        boost::replace_first(fake_may,
                             "UID:artificial-4",
                             "UID:artificial-5");
        boost::replace_first(fake_may,
                             "RRULE:FREQ=YEARLY;INTERVAL=1;BYDAY=1SU;BYMONTH=3",
                             "RRULE:FREQ=YEARLY;INTERVAL=1;BYDAY=1SU;BYMONTH=5");
        boost::replace_first(fake_may,
                             "starting in March",
                             "starting in May");
        boost::replace_first(fake_may,
                             "TZNAME:EST MARCH",
                             "TZNAME:EST MAY");
        CPPUNIT_ASSERT_NO_THROW(luid = addItem(source, fake_may));

        // insert again, shouldn't re-add timezone
        CPPUNIT_ASSERT_NO_THROW(luid = addItem(source, fake_may));
    }
};

SYNCEVOLUTION_TEST_SUITE_REGISTRATION(EvolutionCalendarTest);

#endif // ENABLE_UNIT_TESTS

#ifdef ENABLE_INTEGRATION_TESTS

namespace {
#if 0
}
#endif

static class iCal20Test : public RegisterSyncSourceTest {
public:
    iCal20Test() : RegisterSyncSourceTest("ical20", "ical20") {}

    virtual void updateConfig(ClientTestConfig &config) const
    {
        config.type = "evolution-calendar";
    }
} iCal20Test;

static class iTodo20Test : public RegisterSyncSourceTest {
public:
    iTodo20Test() : RegisterSyncSourceTest("itodo20", "itodo20") {}

    virtual void updateConfig(ClientTestConfig &config) const
    {
        config.type = "evolution-tasks";
    }
} iTodo20Test;

static class MemoTest : public RegisterSyncSourceTest {
public:
    MemoTest() : RegisterSyncSourceTest("text", "") {}

    virtual void updateConfig(ClientTestConfig &config) const
    {
        config.uri = "note"; // ScheduleWorld
        config.type = "Evolution Memos"; // use an alias here to test that
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
        config.dump = dump;
        config.testcases = "testcases/imemo20.ics";
        config.type = "evolution-memos";
    }
} memoTest;

}
#endif // ENABLE_INTEGRATION_TESTS

#endif // ENABLE_ECAL
