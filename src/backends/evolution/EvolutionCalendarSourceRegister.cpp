/*
 * Copyright (C) 2008-2009 Patrick Ohly <patrick.ohly@gmx.de>
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

#include "EvolutionCalendarSource.h"
#include "EvolutionMemoSource.h"
#include "test.h"

#include <boost/algorithm/string.hpp>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

static SyncSource *createSource(const SyncSourceParams &params)
{
    SourceType sourceType = SyncSource::getSourceType(params.m_nodes);
    bool isMe;
    bool enabled;

    EDSAbiWrapperInit();
    enabled = EDSAbiHaveEcal && EDSAbiHaveEdataserver;

    isMe = sourceType.m_backend == "Evolution Task List";
    if (isMe || sourceType.m_backend == "todo") {
        if (sourceType.m_format == "" ||
            sourceType.m_format == "text/calendar" ||
            sourceType.m_format == "text/x-calendar" ||
            sourceType.m_format == "text/x-vcalendar") {
            return
#ifdef ENABLE_ECAL
                enabled ? new EvolutionCalendarSource(E_CAL_SOURCE_TYPE_TODO, params) :
#endif
                isMe ? RegisterSyncSource::InactiveSource(params) : NULL;
        }
    }

    isMe = sourceType.m_backend == "Evolution Memos";
    if (isMe || sourceType.m_backend == "memo") {
        if (sourceType.m_format == "" || sourceType.m_format == "text/plain") {
            return
#ifdef ENABLE_ECAL
                enabled ? new EvolutionMemoSource(params) :
#endif
                isMe ? RegisterSyncSource::InactiveSource(params) : NULL;
        } else if (sourceType.m_format == "text/calendar") {
            return
#ifdef ENABLE_ECAL
                enabled ? new EvolutionCalendarSource(E_CAL_SOURCE_TYPE_JOURNAL, params) :
#endif
                isMe ? RegisterSyncSource::InactiveSource(params) : NULL;
        } else {
            return NULL;
        }
    }

    isMe = sourceType.m_backend == "Evolution Calendar";
    if (isMe || sourceType.m_backend == "calendar") {
        if (sourceType.m_format == "" ||
            sourceType.m_format == "text/calendar" ||
            sourceType.m_format == "text/x-calendar" ||
            sourceType.m_format == "text/x-vcalendar") {
            return
#ifdef ENABLE_ECAL
                enabled ? new EvolutionCalendarSource(E_CAL_SOURCE_TYPE_EVENT, params) :
#endif
                isMe ? RegisterSyncSource::InactiveSource(params) : NULL;
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
                                     "   iCalendar 2.0 (default) = text/calendar\n"
                                     "   vCalendar 1.0 = text/x-vcalendar\n"
                                     "Evolution Task List = Evolution Tasks = todo = tasks = evolution-tasks\n"
                                     "   iCalendar 2.0 (default) = text/calendar\n"
                                     "   vCalendar 1.0 = text/x-vcalendar\n"
                                     "Evolution Memos = memo = memos = evolution-memos\n"
                                     "   plain text in UTF-8 (default) = text/plain\n"
                                     "   iCalendar 2.0 = text/calendar\n"
                                     "   vCalendar 1.0 = text/x-vcalendar\n"
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
    static string addItem(boost::shared_ptr<TestingSyncSource> source,
                          string &data) {
        SyncSourceRaw::InsertItemResult res = source->insertItemRaw("", data);
        return res.m_luid;
    }

    void testInstantiate() {
        boost::shared_ptr<TestingSyncSource> source;
        source.reset((TestingSyncSource *)SyncSource::createTestingSource("calendar", "calendar", true));
        source.reset((TestingSyncSource *)SyncSource::createTestingSource("calendar", "evolution-calendar", true));
        source.reset((TestingSyncSource *)SyncSource::createTestingSource("calendar", "Evolution Calendar:text/calendar", true));

        source.reset((TestingSyncSource *)SyncSource::createTestingSource("calendar", "tasks", true));
        source.reset((TestingSyncSource *)SyncSource::createTestingSource("calendar", "evolution-tasks", true));
        source.reset((TestingSyncSource *)SyncSource::createTestingSource("calendar", "Evolution Tasks", true));
        source.reset((TestingSyncSource *)SyncSource::createTestingSource("calendar", "Evolution Task List:text/calendar", true));

        source.reset((TestingSyncSource *)SyncSource::createTestingSource("calendar", "memos", true));
        source.reset((TestingSyncSource *)SyncSource::createTestingSource("calendar", "evolution-memos", true));
        source.reset((TestingSyncSource *)SyncSource::createTestingSource("calendar", "Evolution Memos:text/plain", true));
        source.reset((TestingSyncSource *)SyncSource::createTestingSource("calendar", "Evolution Memos:text/calendar", true));
    }

    void testOpenDefaultCalendar() {
        boost::shared_ptr<TestingSyncSource> source;
        source.reset((TestingSyncSource *)SyncSource::createTestingSource("calendar", "evolution-calendar", true, NULL));
        CPPUNIT_ASSERT_NO_THROW(source->open());
    }

    void testOpenDefaultTodo() {
        boost::shared_ptr<TestingSyncSource> source;
        source.reset((TestingSyncSource *)SyncSource::createTestingSource("calendar", "evolution-tasks", true, NULL));
        CPPUNIT_ASSERT_NO_THROW(source->open());
    }

    void testOpenDefaultMemo() {
        boost::shared_ptr<TestingSyncSource> source;
        source.reset((TestingSyncSource *)SyncSource::createTestingSource("calendar", "evolution-memos", true, NULL));
        CPPUNIT_ASSERT_NO_THROW(source->open());
    }

    void testTimezones() {
        const char *prefix = getenv("CLIENT_TEST_EVOLUTION_PREFIX");
        if (!prefix) {
            prefix = "SyncEvolution_Test_";
        }

        boost::shared_ptr<TestingSyncSource> source;
        source.reset((TestingSyncSource *)SyncSource::createTestingSource("eds_event", "evolution-calendar", true, prefix));
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

namespace {
#if 0
}
#endif

static class iCal20Test : public RegisterSyncSourceTest {
public:
    iCal20Test() : RegisterSyncSourceTest("eds_event", "eds_event") {}

    virtual void updateConfig(ClientTestConfig &config) const
    {
        config.m_type = "evolution-calendar";
    }
} iCal20Test;

static class iTodo20Test : public RegisterSyncSourceTest {
public:
    iTodo20Test() : RegisterSyncSourceTest("eds_task", "eds_task") {}

    virtual void updateConfig(ClientTestConfig &config) const
    {
        config.m_type = "evolution-tasks";
    }
} iTodo20Test;

static class SuperTest : public RegisterSyncSourceTest {
public:
    SuperTest() : RegisterSyncSourceTest("calendar+todo", "calendar+todo") {}

    virtual void updateConfig(ClientTestConfig &config) const
    {
        config.m_type = "virtual:text/x-vcalendar";
        config.m_subConfigs = "eds_event,eds_task";
    }

} superTest;

static class MemoTest : public RegisterSyncSourceTest {
public:
    MemoTest() : RegisterSyncSourceTest("eds_memo", "eds_memo") {}

    virtual void updateConfig(ClientTestConfig &config) const
    {
        config.m_type = "Evolution Memos"; // use an alias here to test that
    }
} memoTest;

}

#endif // ENABLE_ECAL

SE_END_CXX
