/*
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

#include "akonadisyncsource.h"
#include <syncevo/SyncSource.h>
#include "test.h"

#include <boost/algorithm/string.hpp>


#include <syncevo/declarations.h>
SE_BEGIN_CXX

static SyncSource *createSource(const SyncSourceParams &params)
{
    SourceType sourceType = SyncSource::getSourceType(params.m_nodes);
    bool isMe;    
    
    
    
    isMe = sourceType.m_backend == "KDE Address Book";
    if (isMe /* || sourceType.m_backend == "addressbook" */) {
        if (sourceType.m_format == "" || sourceType.m_format == "text/vcard" 
                || sourceType.m_format == "text/x-vcard") { 
            return
#ifdef ENABLE_AKONADI
                new AkonadiContactSource(params)
#else
                isMe ? RegisterSyncSource::InactiveSource(params) : NULL
#endif
                ;
        } else {
            return NULL;
        }
    }

    isMe = sourceType.m_backend == "KDE Task List";
    if (isMe /* || sourceType.m_backend == "todo" */) {
        if (sourceType.m_format == "" || sourceType.m_format == "text/calendar" 
                || sourceType.m_format == "text/x-vcalendar") { 
            return
#ifdef ENABLE_AKONADI
                new AkonadiTaskSource(params)
#else
                isMe ? RegisterSyncSource::InactiveSource(params) : NULL
#endif
                ;
        } else {
            return NULL;
        }
    }

    isMe = sourceType.m_backend == "KDE Memos";
    if (isMe /* || sourceType.m_backend == "memo" */) {
        if (sourceType.m_format == "" || sourceType.m_format == "text/plain") {
            return
#ifdef ENABLE_AKONADI
                new AkonadiMemoSource(params)
#else
                isMe ? RegisterSyncSource::InactiveSource(params) : NULL
#endif
                ;
        } else {
            return NULL;
        }
    }

    isMe = sourceType.m_backend == "KDE Calendar";
    if (isMe /* || sourceType.m_backend == "calendar" */) {
        if (sourceType.m_format == "" || sourceType.m_format == "text/calendar" ||
            sourceType.m_format == "text/x-vcalendar" /* this is for backwards compatibility with broken configs */ ) {
            return
#ifdef ENABLE_AKONADI
                new AkonadiCalendarSource(params)
#else
                isMe ? RegisterSyncSource::InactiveSource(params) : NULL
#endif
                ;
        } else {
            return NULL;
        }
    }

    return NULL;
}

static RegisterSyncSource registerMe("KDE Contact/Calendar/Task List/Memos",
#ifdef ENABLE_AKONADI
                                     true,
#else
                                     false,
#endif
                                     createSource,
                                     "KDE Address Book = KDE Contacts = addressbook = contacts = kde-contacts\n"
                                     "   vCard 2.1 (default) = text/x-vcard\n"
                                     "   vCard 3.0 = text/vcard\n"
                                     "   The later is the internal format of KDE and preferred with\n"
                                     "   servers that support it. One such server is ScheduleWorld\n"
                                     "   together with the \"card3\" uri.\n"
                                     "KDE Calendar = calendar = events = kde-events\n"
                                     "   iCalendar 2.0 (default) = text/calendar\n"
                                     "   vCalendar 1.0 = text/x-calendar\n"
                                     "KDE Task List = KDE Tasks = todo = tasks = kde-tasks\n"
                                     "   iCalendar 2.0 (default) = text/calendar\n"
                                     "   vCalendar 1.0 = text/x-calendar\n"
                                     "KDE Memos = memo = memos = kde-memos\n"
                                     "   plain text in UTF-8 (default) = text/plain\n",				     
                                     Values() +
                                     (Aliases("KDE Address Book") + "KDE Contacts" + "kde-contacts") +
                                     (Aliases("KDE Calendar") + "kde-calendar") +
                                     (Aliases("KDE Task List") + "KDE Tasks" + "kde-tasks") +
                                     (Aliases("KDE Memos") + "kde-memos"));

#ifdef ENABLE_AKONADI
#ifdef ENABLE_UNIT_TESTS

class AkonadiTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(AkonadiTest);
    CPPUNIT_TEST(testInstantiate);
    // CPPUNIT_TEST(testOpenDefaultCalendar);
    // CPPUNIT_TEST(testOpenDefaultTodo);
    // CPPUNIT_TEST(testOpenDefaultMemo);
    CPPUNIT_TEST(testTimezones);
    CPPUNIT_TEST_SUITE_END();

protected:
    static string addItem(boost::shared_ptr<TestingSyncSource> source,
                          string &data) {
        SyncSourceRaw::InsertItemResult res = source->insertItemRaw("", data);
        return res.m_luid;
    }

    void testInstantiate() {
        boost::shared_ptr<SyncSource> source;
        // source.reset(SyncSource::createTestingSource("addressbook", "addressbook", true));
        // source.reset(SyncSource::createTestingSource("addressbook", "contacts", true));
        source.reset(SyncSource::createTestingSource("addressbook", "kde-contacts", true));
        source.reset(SyncSource::createTestingSource("addressbook", "KDE Contacts", true));
        source.reset(SyncSource::createTestingSource("addressbook", "KDE Address Book:text/x-vcard", true));
        source.reset(SyncSource::createTestingSource("addressbook", "KDE Address Book:text/vcard", true));


        // source.reset(SyncSource::createTestingSource("calendar", "calendar", true));
        source.reset(SyncSource::createTestingSource("calendar", "kde-calendar", true));
        source.reset(SyncSource::createTestingSource("calendar", "KDE Calendar:text/calendar", true));

        // source.reset(SyncSource::createTestingSource("tasks", "tasks", true));
        source.reset(SyncSource::createTestingSource("tasks", "kde-tasks", true));
        source.reset(SyncSource::createTestingSource("tasks", "KDE Tasks", true));
        source.reset(SyncSource::createTestingSource("tasks", "KDE Task List:text/calendar", true));

        // source.reset(SyncSource::createTestingSource("memos", "memos", true));
        source.reset(SyncSource::createTestingSource("memos", "kde-memos", true));
        source.reset(SyncSource::createTestingSource("memos", "KDE Memos:text/plain", true));
    }

    // TODO: support default databases

    // void testOpenDefaultAddressBook() {
    //     boost::shared_ptr<TestingSyncSource> source;
    //     source.reset((TestingSyncSource *)SyncSource::createTestingSource("contacts", "kde-contacts", true, NULL));
    //     CPPUNIT_ASSERT_NO_THROW(source->open());
    // }

    // void testOpenDefaultCalendar() {
    //     boost::shared_ptr<TestingSyncSource> source;
    //     source.reset((TestingSyncSource *)SyncSource::createTestingSource("calendar", "kde-calendar", true, NULL));
    //     CPPUNIT_ASSERT_NO_THROW(source->open());
    // }

    // void testOpenDefaultTodo() {
    //     boost::shared_ptr<TestingSyncSource> source;
    //     source.reset((TestingSyncSource *)SyncSource::createTestingSource("tasks", "kde-tasks", true, NULL));
    //     CPPUNIT_ASSERT_NO_THROW(source->open());
    // }

    // void testOpenDefaultMemo() {
    //     boost::shared_ptr<TestingSyncSource> source;
    //     source.reset((TestingSyncSource *)SyncSource::createTestingSource("memos", "kde-memos", true, NULL));
    //     CPPUNIT_ASSERT_NO_THROW(source->open());
    // }

    void testTimezones() {
        const char *prefix = getenv("CLIENT_TEST_EVOLUTION_PREFIX");
        if (!prefix) {
            prefix = "SyncEvolution_Test_";
        }

        boost::shared_ptr<TestingSyncSource> source;
        source.reset((TestingSyncSource *)SyncSource::createTestingSource("eds_event", "kde-calendar", true, prefix));
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

SYNCEVOLUTION_TEST_SUITE_REGISTRATION(AkonadiTest);

#endif // ENABLE_UNIT_TESTS

namespace {
#if 0
}
#endif

static class vCard30Test : public RegisterSyncSourceTest {
public:
    vCard30Test() : RegisterSyncSourceTest("kde_contact", "eds_contact") {}

    virtual void updateConfig(ClientTestConfig &config) const
    {
        config.m_type = "kde-contacts";
    }
} vCard30Test;

static class iCal20Test : public RegisterSyncSourceTest {
public:
    iCal20Test() : RegisterSyncSourceTest("kde_event", "eds_event") {}

    virtual void updateConfig(ClientTestConfig &config) const
    {
        config.m_type = "kde-calendar";
    }
} iCal20Test;

static class iTodo20Test : public RegisterSyncSourceTest {
public:
    iTodo20Test() : RegisterSyncSourceTest("kde_task", "eds_task") {}

    virtual void updateConfig(ClientTestConfig &config) const
    {
        config.m_type = "kde-tasks";
    }
} iTodo20Test;

static class MemoTest : public RegisterSyncSourceTest {
public:
    MemoTest() : RegisterSyncSourceTest("kde_memo", "eds_memo") {}

    virtual void updateConfig(ClientTestConfig &config) const
    {
        config.m_type = "KDE Memos"; // use an alias here to test that
    }
} memoTest;

}

#endif // ENABLE_AKONADI

SE_END_CXX
