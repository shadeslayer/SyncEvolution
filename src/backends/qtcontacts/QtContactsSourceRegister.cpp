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

#ifdef ENABLE_QTCONTACTS
#include <QContactBirthday>
#include <QContactEmailAddress>
#include <QContactName>
#endif

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
                isMe ? RegisterSyncSource::InactiveSource(params) : NULL;
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
                                     "   vCard 3.0 = text/vcard\n"
                                     "   'database' is specified via a QtContacts URI, which\n"
                                     "   consists of qtcontacts:<backend>:<URL encoded parameters>.\n"
                                     "   Examples: 'qtcontacts:tracker:' or 'qtcontacts:eds:source=local:/system'\n",
                                     Values() +
                                     (Aliases("QtContacts") + "qt-contacts"));

#ifdef ENABLE_QTCONTACTS
#ifdef ENABLE_UNIT_TESTS

class QtContactsSourceUnitTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(QtContactsSourceUnitTest);
    CPPUNIT_TEST(testInstantiate);
    CPPUNIT_TEST(testHandler);
    CPPUNIT_TEST_SUITE_END();

protected:
    void testInstantiate() {
        boost::shared_ptr<SyncSource> source;
        source.reset(SyncSource::createTestingSource("qtcontacts", "qtcontacts:text/vcard:3.0", true));
        source.reset(SyncSource::createTestingSource("qtcontacts", "QtContacts", true));
    }

    void testHandler() {
        QContact out;
        QList<QContact> contacts;
        QContactDetailFieldDefinition field;
        QContactDetailDefinition unique;
        QMap<QString, QContactDetailDefinition> details;
        unique.setName("Unique");
        unique.setUnique(true);
        field.setDataType(QVariant::Bool);
        unique.insertField("Bool", field);
        field.setDataType(QVariant::Int);
        unique.insertField("Int", field);
        field.setDataType(QVariant::UInt);
        unique.insertField("UInt", field);
        field.setDataType(QVariant::Date);
        unique.insertField("Date", field);
        field.setDataType(QVariant::DateTime);
        unique.insertField("DateTime", field);
        field.setDataType(QVariant::String);
        unique.insertField("String", field);
        field.setDataType(QVariant::ByteArray);
        unique.insertField("ByteArray", field);
        details["Unique"] = unique;

        QContactDetailDefinition multiple;
        multiple.setName("Multiple");
        field.setDataType(QVariant::String);
        multiple.insertField("String", field);
        details["Multiple"] = multiple;

        QContactBirthday birthday;
        birthday.setDate(QDate(2000, 1, 1));
        CPPUNIT_ASSERT(out.saveDetail(&birthday));
        QContactEmailAddress email;
        email.setEmailAddress("john.doe@foo.com");
        CPPUNIT_ASSERT(out.saveDetail(&email));
        QContactDetail detailUnique("Unique");
        detailUnique.setValue("Bool", QVariant(true));
        detailUnique.setValue("Int", QVariant(-1));
        detailUnique.setValue("UInt", QVariant(4294967295u));
        detailUnique.setValue("Date", QVariant(QDate(2011, 12, 1)));
        detailUnique.setValue("DateTime", QVariant(QDateTime(QDate(2011, 12, 1), QTime(23, 59, 59))));
        detailUnique.setValue("String", QVariant(QString("hello world;\nhow are you?")));
        detailUnique.setValue("ByteArray", QVariant(QByteArray() + 'a' + 'b' + 'c'));
        CPPUNIT_ASSERT(out.saveDetail(&detailUnique));
        QContactDetail detailMulti1("Multiple");
        detailMulti1.setValue("String", QVariant(QString("hello")));
        CPPUNIT_ASSERT(out.saveDetail(&detailMulti1));
        QContactDetail detailMulti2("Multiple");
        detailMulti2.setValue("String", QVariant(QString("world")));
        CPPUNIT_ASSERT(out.saveDetail(&detailMulti2));

        // empty name because parser otherwise does things like
        // synthesizing custom and display name, which breaks the
        // comparison below
        QContactName name;
        CPPUNIT_ASSERT(out.saveDetail(&name));

        contacts << out;
        SyncEvoQtContactsHandler handler(details);
        QVersitContactExporter exporter;
        exporter.setDetailHandler(&handler);
        CPPUNIT_ASSERT(exporter.exportContacts(contacts, QVersitDocument::VCard30Type));
        QByteArray vcard;
        QVersitWriter writer(&vcard);
        CPPUNIT_ASSERT(writer.startWriting(exporter.documents()));
        writer.waitForFinished();
        CPPUNIT_ASSERT(!writer.error());
        string item = vcard.constData();
        CPPUNIT_ASSERT_EQUAL(string("BEGIN:VCARD\r\n"
                                    "VERSION:3.0\r\n"
                                    "BDAY:2000-01-01\r\n"
                                    "EMAIL:john.doe@foo.com\r\n"

                                    "X-SYNCEVO-QTCONTACTS:Unique^Bool^BOOL^1^ByteArray^VARIANT^0000000c0000000003\r\n"
                                    " 616263^Date^DATE^2011-12-01^DateTime^DATETIME^2011-12-01T23:59:59^Int^INT^-\r\n"
                                    " 1^String^STRING^hello world\\;|0ahow are you?^UInt^UINT^4294967295\r\n"
                                    "X-SYNCEVO-QTCONTACTS:Multiple^String^STRING^hello\r\n"
                                    "X-SYNCEVO-QTCONTACTS:Multiple^String^STRING^world\r\n"
                                    "FN:\r\n"
                                    "N:;;;;\r\n"
                                    "END:VCARD\r\n"),
                             item);

        QVersitReader reader(QByteArray(item.c_str()));
        CPPUNIT_ASSERT(reader.startReading());
        reader.waitForFinished();
        CPPUNIT_ASSERT(!reader.error());
        QVersitContactImporter importer;
        importer.setPropertyHandler(&handler);
        CPPUNIT_ASSERT(importer.importDocuments(reader.results()));
        contacts = importer.contacts();
        QContact &in = contacts.first();

        QString outString, inString;
        QDebug(&outString) << out;
        QDebug(&inString) << in;
        if (out != in) {
            // strings are never quite equal due to key, so skip this check
            // if QContact itself thinks its values are equal
            CPPUNIT_ASSERT_EQUAL(string(outString.toUtf8().constData()),
                                 string(inString.toUtf8().constData()));
        }
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
    VCard30Test() : RegisterSyncSourceTest("qt_contact", "eds_contact") {}

    virtual void updateConfig(ClientTestConfig &config) const
    {
        config.m_type = "qt-contacts:text/vcard";
        config.m_testcases = "testcases/qt_contact.vcf";
    }
} vCard30Test;

}

#endif // ENABLE_QTCONTACTS

SE_END_CXX
