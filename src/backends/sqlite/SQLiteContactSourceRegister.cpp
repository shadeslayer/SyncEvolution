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

#include "SQLiteContactSource.h"

#ifdef ENABLE_UNIT_TESTS
# include <cppunit/extensions/TestFactoryRegistry.h>
# include <cppunit/extensions/HelperMacros.h>
#endif

#include <syncevo/declarations.h>
SE_BEGIN_CXX

static SyncSource *createSource(const SyncSourceParams &params)
{
    SourceType sourceType = SyncSource::getSourceType(params.m_nodes);
    bool isMe = sourceType.m_backend == "SQLite Address Book";

#ifndef ENABLE_SQLITE
    return isMe ? RegisterSyncSource::InactiveSource(params) : NULL;
#else
    bool maybeMe = sourceType.m_backend == "addressbook";
    
    if (isMe || maybeMe) {
        if (sourceType.m_format == "" || sourceType.m_format == "text/x-vcard") {
            return new SQLiteContactSource(params);
        } else {
            return NULL;
        }
    }
    return NULL;
#endif
}

static RegisterSyncSource registerMe("SQLite Address Book",
#ifdef ENABLE_SQLITE
                                     true,
#else
                                     false,
#endif
                                     createSource,
                                     "SQLite Address Book = addressbook = contacts = sqlite-contacts\n"
                                     "   vCard 2.1 (default) = text/x-vcard\n",
                                     Values() +
                                     (Aliases("SQLite Address Book") + "sqlite-contacts" + "sqlite"));

#ifdef ENABLE_SQLITE
#ifdef ENABLE_UNIT_TESTS

class EvolutionSQLiteContactsTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(EvolutionSQLiteContactsTest);
    CPPUNIT_TEST(testInstantiate);
    CPPUNIT_TEST_SUITE_END();

protected:
    void testInstantiate() {
        boost::shared_ptr<SyncSource> source;
        source.reset(SyncSource::createTestingSource("contacts", "contacts", true));
        source.reset(SyncSource::createTestingSource("contacts", "addressbook", true));
        source.reset(SyncSource::createTestingSource("contacts", "sqlite-contacts", true));
        source.reset(SyncSource::createTestingSource("contacts", "SQLite Address Book:text/x-vcard", true));
    }
};

SYNCEVOLUTION_TEST_SUITE_REGISTRATION(EvolutionSQLiteContactsTest);

#endif // ENABLE_UNIT_TESTS

/*Client-Test requries the backends is an instance of TestingSyncSource
 * which in turn requires backends support serialized access for the data
 * which is not supported by SQLiteContactSource.*/
#if 0
#ifdef ENABLE_INTEGRATION_TESTS
namespace {
#if 0
}
#endif

static class VCard21Test : public RegisterSyncSourceTest {
public:
    VCard21Test() : RegisterSyncSourceTest("sqlite_vcard21", "vcard21") {}

    virtual void updateConfig(ClientTestConfig &config) const
    {
        config.m_type = "sqlite-contacts:text/x-vcard";
        config.m_testcases = "testcases/sqlite_vcard21.vcf";
    }
} VCard21Test;

}
#endif // ENABLE_INTEGRATION_TESTS
#endif 

#endif // ENABLE_SQLITE

SE_END_CXX
