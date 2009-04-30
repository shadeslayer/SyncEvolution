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
#include "test.h"

static EvolutionSyncSource *createSource(const EvolutionSyncSourceParams &params)
{
    pair <string, string> sourceType = EvolutionSyncSource::getSourceType(params.m_nodes);
    bool isMe = sourceType.first == "SQLite Address Book";

#ifndef ENABLE_SQLITE
    return isMe ? RegisterSyncSource::InactiveSource : NULL;
#else
    bool maybeMe = sourceType.first == "addressbook";
    
    if (isMe || maybeMe) {
        if (sourceType.second == "" || sourceType.second == "text/x-vcard") {
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
        boost::shared_ptr<EvolutionSyncSource> source;
        source.reset(EvolutionSyncSource::createTestingSource("contacts", "contacts", true));
        source.reset(EvolutionSyncSource::createTestingSource("contacts", "addressbook", true));
        source.reset(EvolutionSyncSource::createTestingSource("contacts", "sqlite-contacts", true));
        source.reset(EvolutionSyncSource::createTestingSource("contacts", "SQLite Address Book:text/x-vcard", true));
    }
};

SYNCEVOLUTION_TEST_SUITE_REGISTRATION(EvolutionSQLiteContactsTest);

#endif // ENABLE_UNIT_TESTS

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
        config.type = "sqlite-contacts:text/x-vcard";
        config.testcases = "testcases/sqlite_vcard21.vcf";
    }
} VCard21Test;

}
#endif // ENABLE_INTEGRATION_TESTS

#endif // ENABLE_SQLITE
