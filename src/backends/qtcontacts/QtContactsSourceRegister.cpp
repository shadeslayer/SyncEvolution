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

#include "QtContactsSource.h"
#include "test.h"

#include <syncevo/declarations.h>
SE_BEGIN_CXX

static SyncSource *createSource(const SyncSourceParams &params)
{
    SourceType sourceType = SyncSource::getSourceType(params.m_nodes);
    bool isMe = sourceType.m_backend == "QtContacts";
    bool maybeMe = sourceType.m_backend == "addressbook";

    if (isMe || maybeMe) {
        if (sourceType.m_format == "" ||
            sourceType.m_format == "text/x-vcard" ||
            sourceType.m_format == "text/vcard") {
            return
#ifdef ENABLE_QTCONTACTS
                true ? new QtContactsSource(params) :
#endif
                isMe ? RegisterSyncSource::InactiveSource : NULL;
        }
    }
    return NULL;
}

static RegisterSyncSource registerMe("QtContacts",
#ifdef ENABLE_QTCONTACTS
                                     true,
#else
                                     false,
#endif
                                     createSource,
                                     "QtContacts = addressbook = contacts = qt-contacts\n"
                                     "   vCard 3.0 = text/vcard\n",
                                     Values() +
                                     (Aliases("QtContacts") + "qt-contacts"));

#ifdef ENABLE_QTCONTACTS
#ifdef ENABLE_UNIT_TESTS

class QtContactsSourceUnitTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(QtContactsSourceUnitTest);
    CPPUNIT_TEST(testInstantiate);
    CPPUNIT_TEST_SUITE_END();

protected:
    void testInstantiate() {
        boost::shared_ptr<SyncSource> source;
        source.reset(SyncSource::createTestingSource("qtcontacts", "qtcontacts:text/vcard:3.0", true));
        source.reset(SyncSource::createTestingSource("qtcontacts", "QtContacts", true));
    }
};

SYNCEVOLUTION_TEST_SUITE_REGISTRATION(QtContactsSourceUnitTest);

#endif // ENABLE_UNIT_TESTS

namespace {
#if 0
}
#endif

static class VCard30Test : public RegisterSyncSourceTest {
public:
    VCard30Test() : RegisterSyncSourceTest("qt_vcard30", "vcard30") {}

    virtual void updateConfig(ClientTestConfig &config) const
    {
        config.type = "qt-contacts:text/vcard";
        config.testcases = "testcases/qt_vcard30.vcf";
    }
} vCard30Test;

}

#endif // ENABLE_QTCONTACTS

SE_END_CXX
