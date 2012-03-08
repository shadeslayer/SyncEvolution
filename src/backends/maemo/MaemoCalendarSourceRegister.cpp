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

#include "MaemoCalendarSource.h"
#include "test.h"

#include <syncevo/declarations.h>
SE_BEGIN_CXX

static SyncSource *createSource(const SyncSourceParams &params)
{
    SourceType sourceType = SyncSource::getSourceType(params.m_nodes);
    bool isMe = sourceType.m_backend == "Maemo Calendar";
#ifndef ENABLE_MAEMO_CALENDAR
    if (isMe) return RegisterSyncSource::InactiveSource(params);
#else
    bool maybeMe = sourceType.m_backend == "calendar";

    if (isMe || maybeMe) {
        if (sourceType.m_format == "" || sourceType.m_format == "text/calendar") {
            return new MaemoCalendarSource(EVENT, ICAL_TYPE, params);
        } else if (sourceType.m_format == "text/x-vcalendar") {
            return new MaemoCalendarSource(EVENT, VCAL_TYPE, params);
        } else {
            return NULL;
        }
    }
#endif

    isMe = sourceType.m_backend == "Maemo Tasks";
#ifndef ENABLE_MAEMO_CALENDAR
    if (isMe) return RegisterSyncSource::InactiveSource(params);
#else
    maybeMe = sourceType.m_backend == "todo";

    if (isMe || maybeMe) {
        if (sourceType.m_format == "" || sourceType.m_format == "text/calendar") {
            return new MaemoCalendarSource(TODO, ICAL_TYPE, params);
        } else if (sourceType.m_format == "text/x-vcalendar") {
            return new MaemoCalendarSource(TODO, VCAL_TYPE, params);
        } else {
            return NULL;
        }
    }
#endif

    isMe = sourceType.m_backend == "Maemo Notes";
#ifndef ENABLE_MAEMO_CALENDAR
    if (isMe) return RegisterSyncSource::InactiveSource(params);
#else
    maybeMe = sourceType.m_backend == "memo";

    if (isMe || maybeMe) {
        if (sourceType.m_format == "" || sourceType.m_format == "text/plain") {
            return new MaemoCalendarSource(JOURNAL, -1, params);
        } else if (sourceType.m_format == "text/calendar") {
            return new MaemoCalendarSource(JOURNAL, ICAL_TYPE, params);
        } else if (sourceType.m_format == "text/x-vcalendar") {
            return new MaemoCalendarSource(JOURNAL, VCAL_TYPE, params);
        } else {
            return NULL;
        }
    }
#endif

    return NULL;
}

static RegisterSyncSource registerMe("Maemo Calendar/Tasks/Notes",
#ifdef ENABLE_MAEMO_CALENDAR
                                     true,
#else
                                     false,
#endif
                                     createSource,
                                     "Maemo Calendar = calendar = events = maemo-events\n"
                                     "   iCalendar 2.0 (default) = text/calendar\n"
                                     "   vCalendar 1.0 = text/x-vcalendar\n"
                                     "Maemo Tasks = todo = tasks = maemo-tasks\n"
                                     "   iCalendar 2.0 (default) = text/calendar\n"
                                     "   vCalendar 1.0 = text/x-vcalendar\n"
                                     "Maemo Notes = memo = memos = notes = journal = maemo-notes\n"
                                     "   plain text in UTF-8 (default) = text/plain\n"
                                     "   iCalendar 2.0 = text/calendar\n"
                                     "   vCalendar 1.0 = text/x-vcalendar\n",
                                     Values() +
                                     (Aliases("Maemo Calendar") + "maemo-events") +
                                     (Aliases("Maemo Tasks") + "maemo-tasks") +
                                     (Aliases("Maemo Notes") + "maemo-notes"));

#ifdef ENABLE_MAEMO_CALENDAR
#ifdef ENABLE_UNIT_TESTS

class MaemoCalendarSourceUnitTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(MaemoCalendarSourceUnitTest);
    CPPUNIT_TEST(testInstantiate);
    CPPUNIT_TEST_SUITE_END();

protected:
    void testInstantiate() {
        boost::shared_ptr<SyncSource> source;
        source.reset(SyncSource::createTestingSource("calendar", "calendar", true));
        source.reset(SyncSource::createTestingSource("calendar", "maemo-events", true));
        source.reset(SyncSource::createTestingSource("calendar", "Maemo Calendar:text/calendar", true));
    }
};

SYNCEVOLUTION_TEST_SUITE_REGISTRATION(MaemoCalendarSourceUnitTest);

#endif // ENABLE_UNIT_TESTS

namespace {
#if 0
}
#endif

static class iCal20Test : public RegisterSyncSourceTest {
public:
    iCal20Test() : RegisterSyncSourceTest("maemo_event", "eds_event") {}

    virtual void updateConfig(ClientTestConfig &config) const
    {
        config.m_type = "maemo-events";
    }
} iCal20Test;

static class iTodo20Test : public RegisterSyncSourceTest {
public:
    iTodo20Test() : RegisterSyncSourceTest("maemo_task", "eds_task") {}

    virtual void updateConfig(ClientTestConfig &config) const
    {
        config.m_type = "maemo-tasks";
    }
} iTodo20Test;

static class MemoTest : public RegisterSyncSourceTest {
public:
    MemoTest() : RegisterSyncSourceTest("maemo_memo", "eds_memo") {}

    virtual void updateConfig(ClientTestConfig &config) const
    {
        config.m_type = "maemo-notes";
    }
} memoTest;

}

#endif // ENABLE_MAEMO_CALENDAR

SE_END_CXX
