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

#include "FileSyncSource.h"
#include "test.h"

#include <syncevo/declarations.h>
SE_BEGIN_CXX

static SyncSource *createSource(const SyncSourceParams &params)
{
    SourceType sourceType = SyncSource::getSourceType(params.m_nodes);
    // The string returned by getSourceType() is always the one
    // registered as main Aliases() below.
    bool isMe = sourceType.m_backend == "file";

#ifndef ENABLE_FILE
    // tell SyncEvolution if the user wanted to use a disabled sync source,
    // otherwise let it continue searching
    return isMe ? RegisterSyncSource::InactiveSource(params) : NULL;
#else
    // Also recognize one of the standard types?
    // Not in the FileSyncSource!
    bool maybeMe = false /* sourceType.m_backend == "addressbook" */;
    
    if (isMe || maybeMe) {
        // The FileSyncSource always needs the database format.
        if (!sourceType.m_localFormat.empty()) {
            return new FileSyncSource(params, sourceType.m_localFormat);
        } else {
            return NULL;
        }
    }
    return NULL;
#endif
}

static RegisterSyncSource registerMe("Files in one directory",
#ifdef ENABLE_FILE
                                     true,
#else
                                     false,
#endif
                                     createSource,
                                     "Files in one directory = file\n"
                                     "   Stores items in one directory as one file per item.\n"
                                     "   The directory is selected via database=[file://]<path>.\n"
                                     "   It will only be created if the prefix is given, otherwise\n"
                                     "   it must exist already.\n"
                                     "   The database format *must* be specified explicitly. It may be\n"
                                     "   different from the sync format, as long as there are\n"
                                     "   conversion rules (for example, vCard 2.1 <-> vCard 3.0). If\n"
                                     "   the sync format is empty, the database format is used.\n"
                                     "   Examples for databaseFormat + syncFormat:\n"
                                     "      text/plain + text/plain\n"
                                     "      text/x-vcard + text/vcard\n"
                                     "      text/calendar\n"
                                     "   Examples for evolutionsource:\n"
                                     "      /home/joe/datadir - directory must exist\n"
                                     "      file:///tmp/scratch - directory is created\n",
                                     Values() +
                                     (Aliases("file") + "Files in one directory"));

#ifdef ENABLE_FILE
#ifdef ENABLE_UNIT_TESTS

class FileSyncSourceUnitTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(FileSyncSourceUnitTest);
    CPPUNIT_TEST(testInstantiate);
    CPPUNIT_TEST_SUITE_END();

protected:
    void testInstantiate() {
        boost::shared_ptr<SyncSource> source;
        source.reset(SyncSource::createTestingSource("file", "file:text/vcard:3.0", true));
        source.reset(SyncSource::createTestingSource("file", "file:text/plain:1.0", true));
        source.reset(SyncSource::createTestingSource("file", "Files in one directory:text/x-vcard:2.1", true));
    }
};

SYNCEVOLUTION_TEST_SUITE_REGISTRATION(FileSyncSourceUnitTest);

#endif // ENABLE_UNIT_TESTS

// The anonymous namespace ensures that we don't get
// name clashes: although the classes and objects are
// only defined in this file, the methods generated
// for local classes will clash with other methods
// of other classes with the same name if no namespace
// is used.
//
// The code above is not yet inside the anonymous
// name space because it would show up inside
// the CPPUnit unit test names. Use a unique class
// name there.
namespace {
#if 0
}
#endif

static class VCard30Test : public RegisterSyncSourceTest {
public:
    VCard30Test() : RegisterSyncSourceTest("file_contact", "eds_contact") {}

    virtual void updateConfig(ClientTestConfig &config) const
    {
        config.m_type = "file:text/vcard:3.0";
    }
} VCard30Test;

static class ICal20Test : public RegisterSyncSourceTest {
public:
    ICal20Test() : RegisterSyncSourceTest("file_event", "eds_event") {}

    virtual void updateConfig(ClientTestConfig &config) const
    {
        config.m_type = "file:text/calendar:2.0";

        // A sync source which supports linked items (= recurring
        // event with detached exception) is expected to handle
        // inserting the parent or child twice by turning the
        // second operation into an update. The file backend is
        // to dumb for that and therefore fails these tests:
        //
        // Client::Source::file_event::testLinkedItemsInsertParentTwice
        // Client::Source::file_event::testLinkedItemsInsertChildTwice
        //
        // Disable linked item testing to avoid this.
        config.m_sourceKnowsItemSemantic = false;
    }
} ICal20Test;

static class ITodo20Test : public RegisterSyncSourceTest {
public:
    ITodo20Test() : RegisterSyncSourceTest("file_task", "eds_task") {}

    virtual void updateConfig(ClientTestConfig &config) const
    {
        config.m_type = "file:text/calendar:2.0";
    }
} ITodo20Test;

static class SuperTest : public RegisterSyncSourceTest {
public:
    SuperTest() : RegisterSyncSourceTest("file_calendar+todo", "calendar+todo") {}

    virtual void updateConfig(ClientTestConfig &config) const
    {
        config.m_type = "virtual:text/x-vcalendar";
        config.m_subConfigs = "file_event,file_task";
    }

} superTest;

}

#endif // ENABLE_FILE

SE_END_CXX
