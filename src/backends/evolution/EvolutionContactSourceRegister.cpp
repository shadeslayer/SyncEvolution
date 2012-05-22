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

#include "EvolutionContactSource.h"
#include "test.h"

#include <syncevo/declarations.h>
SE_BEGIN_CXX

static SyncSource *createSource(const SyncSourceParams &params)
{
    SourceType sourceType = SyncSource::getSourceType(params.m_nodes);
    bool isMe = sourceType.m_backend == "Evolution Address Book";
    bool maybeMe = sourceType.m_backend == "addressbook";
    bool enabled;

    EDSAbiWrapperInit();
    enabled = EDSAbiHaveEbook && EDSAbiHaveEdataserver;
    
    if (isMe || maybeMe) {
        if (sourceType.m_format == "text/x-vcard") {
            return
#ifdef ENABLE_EBOOK
                enabled ? new EvolutionContactSource(params, EVC_FORMAT_VCARD_21) :
#endif
                isMe ? RegisterSyncSource::InactiveSource(params) : NULL;
        } else if (sourceType.m_format == "" || sourceType.m_format == "text/vcard") {
            return
#ifdef ENABLE_EBOOK
                enabled ? new EvolutionContactSource(params, EVC_FORMAT_VCARD_30) :
#endif
                isMe ? RegisterSyncSource::InactiveSource(params) : NULL;
        }
    }
    return NULL;
}

static RegisterSyncSource registerMe("Evolution Address Book",
#ifdef ENABLE_EBOOK
                                     true,
#else
                                     false,
#endif
                                     createSource,
                                     "Evolution Address Book = Evolution Contacts = addressbook = contacts = evolution-contacts\n"
                                     "   vCard 2.1 = text/x-vcard\n"
                                     "   vCard 3.0 (default) = text/vcard\n"
                                     "   The later is the internal format of Evolution and preferred with\n"
                                     "   servers that support it.",
                                     Values() +
                                     (Aliases("Evolution Address Book") + "Evolution Contacts" + "evolution-contacts"));

#ifdef ENABLE_EBOOK
#ifdef ENABLE_UNIT_TESTS

class EvolutionContactTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(EvolutionContactTest);
    CPPUNIT_TEST(testInstantiate);
    CPPUNIT_TEST(testImport);
    CPPUNIT_TEST_SUITE_END();

protected:
    void testInstantiate() {
        boost::shared_ptr<SyncSource> source;
        source.reset(SyncSource::createTestingSource("addressbook", "addressbook", true));
        source.reset(SyncSource::createTestingSource("addressbook", "contacts", true));
        source.reset(SyncSource::createTestingSource("addressbook", "evolution-contacts", true));
        source.reset(SyncSource::createTestingSource("addressbook", "Evolution Contacts", true));
        source.reset(SyncSource::createTestingSource("addressbook", "Evolution Address Book:text/x-vcard", true));
        source.reset(SyncSource::createTestingSource("addressbook", "Evolution Address Book:text/vcard", true));
    }

    /**
     * Tests parsing of contacts as they might be send by certain servers.
     * This complements the actual testing with real servers and might cover
     * cases not occurring with servers that are actively tested against.
     */
    void testImport() {
        // this only tests that we can instantiate something under the type "addressbook";
        // it might not be an EvolutionContactSource
        boost::shared_ptr<EvolutionContactSource> source21(dynamic_cast<EvolutionContactSource *>(SyncSource::createTestingSource("evolutioncontactsource21", "evolution-contacts:text/x-vcard", true)));
        boost::shared_ptr<EvolutionContactSource> source30(dynamic_cast<EvolutionContactSource *>(SyncSource::createTestingSource("evolutioncontactsource30", "Evolution Address Book:text/vcard", true)));
        string parsed;

#if 0
        // TODO: enable testing of incoming items again. Right now preparse() doesn't
        // do anything and needs to be replaced with Synthesis mechanisms.

        // SF bug 1796086: sync with EGW: lost or messed up telephones
        parsed = "BEGIN:VCARD\r\nVERSION:3.0\r\nTEL;CELL:cell\r\nEND:VCARD\r\n";
        CPPUNIT_ASSERT_EQUAL(parsed,
                             preparse(*source21,
                                      "BEGIN:VCARD\nVERSION:2.1\nTEL;CELL:cell\nEND:VCARD\n",
                                      "text/x-vcard"));

        parsed = "BEGIN:VCARD\r\nVERSION:3.0\r\nTEL;TYPE=CAR:car\r\nEND:VCARD\r\n";
        CPPUNIT_ASSERT_EQUAL(parsed,
                             preparse(*source21,
                                      "BEGIN:VCARD\nVERSION:2.1\nTEL;TYPE=CAR:car\nEND:VCARD\n",
                                      "text/x-vcard"));

        parsed = "BEGIN:VCARD\r\nVERSION:3.0\r\nTEL;TYPE=HOME:home\r\nEND:VCARD\r\n";
        CPPUNIT_ASSERT_EQUAL(parsed,
                             preparse(*source21,
                                      "BEGIN:VCARD\nVERSION:2.1\nTEL:home\nEND:VCARD\n",
                                      "text/x-vcard"));

        // TYPE=PARCEL not supported by Evolution, used to represent Evolutions TYPE=OTHER
        parsed = "BEGIN:VCARD\r\nVERSION:3.0\r\nTEL;TYPE=OTHER:other\r\nEND:VCARD\r\n";
        CPPUNIT_ASSERT_EQUAL(parsed,
                             preparse(*source21,
                                      "BEGIN:VCARD\nVERSION:2.1\nTEL;TYPE=PARCEL:other\nEND:VCARD\n",
                                      "text/x-vcard"));

        parsed = "BEGIN:VCARD\r\nVERSION:3.0\r\nTEL;TYPE=HOME;TYPE=VOICE:cell\r\nEND:VCARD\r\n";
        CPPUNIT_ASSERT_EQUAL(parsed,
                             preparse(*source21,
                                      "BEGIN:VCARD\nVERSION:2.1\nTEL;TYPE=HOME,VOICE:cell\nEND:VCARD\n",
                                      "text/x-vcard"));
#endif
    }
};

SYNCEVOLUTION_TEST_SUITE_REGISTRATION(EvolutionContactTest);
#endif // ENABLE_UNIT_TESTS

namespace {
#if 0
}
#endif

static class VCard30Test : public RegisterSyncSourceTest {
public:
    VCard30Test() : RegisterSyncSourceTest("eds_contact", "eds_contact") {}

    virtual void updateConfig(ClientTestConfig &config) const
    {
        config.m_type = "evolution-contacts:text/vcard";
        config.m_update = config.m_genericUpdate;
        // this property gets re-added by EDS and thus cannot be removed
	config.m_essentialProperties.insert("X-EVOLUTION-FILE-AS");
    }
} vCard30Test;

}

#endif // ENABLE_EBOOK


SE_END_CXX
