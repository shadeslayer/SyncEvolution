/*
 * Copyright (C) 2009 m-otion communications GmbH <knipp@m-otion.com>, waived
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

#include "XMLRPCSyncSource.h"
#include "test.h"

#include <syncevo/declarations.h>
SE_BEGIN_CXX

static SyncSource *createSource(const SyncSourceParams &params)
{
    SourceType sourceType = SyncSource::getSourceType(params.m_nodes);
    // The string returned by getSourceType() is always the one
    // registered as main Aliases() below.
    bool isMe = sourceType.m_backend == "XMLRPC interface";

#ifndef ENABLE_XMLRPC
    // tell SyncEvolution if the user wanted to use a disabled sync source,
    // otherwise let it continue searching
    return isMe ? RegisterSyncSource::InactiveSource(params) : NULL;
#else
    // Also recognize one of the standard types?
    // Not in the FileSyncSource!
    bool maybeMe = false /* sourceType.m_backend == "addressbook" */;

    if (isMe || maybeMe) {
        // The FileSyncSource always needs the data format
        // parameter in sourceType.m_format.
        if (/* sourceType.m_format == "" || sourceType.m_format == "text/x-vcard" */
            sourceType.m_format.size()) {
            return new XMLRPCSyncSource(params, sourceType.m_format);
        } else {
            return NULL;
        }
    }
    return NULL;
#endif
}

static RegisterSyncSource registerMe("XMLRPC interface for data exchange",
#ifdef ENABLE_XMLRPC
                                     true,
#else
                                     false,
#endif
                                     createSource,
                                     "XMLRPC interface = xmlrpc\n"
                                     "   Data exchange is done via an XMLRPC interface on the datastore.\n",
                                     Values() +
                                     (Aliases("XMLRPC interface") + "xmlrpc"));

#ifdef ENABLE_XMLRPC
#ifdef ENABLE_UNIT_TESTS

class XMLRPCSyncSourceUnitTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(XMLRPCSyncSourceUnitTest);
    CPPUNIT_TEST(testInstantiate);
    CPPUNIT_TEST_SUITE_END();

protected:
    void testInstantiate() {
        boost::shared_ptr<SyncSource> source;
        source.reset(SyncSource::createTestingSource("xmlrpc", "xmlrpc:text/vcard:3.0", true));
        source.reset(SyncSource::createTestingSource("xmlrpc", "xmlrpc:text/plain:1.0", true));
        source.reset(SyncSource::createTestingSource("xmlrpc", "XMLRPC interface:text/x-vcard:2.1", true));
    }
};

SYNCEVOLUTION_TEST_SUITE_REGISTRATION(XMLRPCSyncSourceUnitTest);

#endif // ENABLE_UNIT_TESTS

#ifdef ENABLE_INTEGRATION_TESTS

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

static class VCard21Test : public RegisterSyncSourceTest {
public:
    VCard21Test() : RegisterSyncSourceTest("xmlrpc_contact", "eds_contact") {}

    virtual void updateConfig(ClientTestConfig &config) const
    {
        // set type as required by XMLRPCSyncSource
        // and leave everything else at its default
        config.type = "xmlrpc:text/x-vcard:2.1";
    }
} VCard21Test;

static class VCard30Test : public RegisterSyncSourceTest {
public:
    VCard30Test() : RegisterSyncSourceTest("xmlrpc_contact", "eds_contact") {}

    virtual void updateConfig(ClientTestConfig &config) const
    {
        config.type = "xmlrpc:text/vcard:3.0";
    }
} VCard30Test;

static class ICal20Test : public RegisterSyncSourceTest {
public:
    ICal20Test() : RegisterSyncSourceTest("xmlrpc_event", "eds_event") {}

    virtual void updateConfig(ClientTestConfig &config) const
    {
        config.type = "xmlrpc:text/calendar:2.0";

        // A sync source which supports linked items (= recurring
        // event with detached exception) is expected to handle
        // inserting the parent or child twice by turning the
        // second operation into an update. The file backend is
        // to dumb for that and therefore fails these tests:
        //
        // Client::Source::xmlrpc_event::testLinkedItemsInsertParentTwice
        // Client::Source::xmlrpc_event::testLinkedItemsInsertChildTwice
        //
        // Disable linked item testing to avoid this.
        config.sourceKnowsItemSemantic = false;
    }
} ICal20Test;

static class ITodo20Test : public RegisterSyncSourceTest {
public:
    ITodo20Test() : RegisterSyncSourceTest("xmlrpc_task", "eds_task") {}

    virtual void updateConfig(ClientTestConfig &config) const
    {
        config.type = "xmlrpc:text/calendar:2.0";
    }
} ITodo20Test;

}
#endif // ENABLE_INTEGRATION_TESTS

#endif // ENABLE_XMLRPC

SE_END_CXX
