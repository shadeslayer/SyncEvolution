/*
 * Copyright (C) 2007-2009 Patrick Ohly <patrick.ohly@gmx.de>
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

#include "config.h"

#ifdef ENABLE_QTCONTACTS

#include "QtContactsSource.h"

#include <QCoreApplication>
#include <QDebug>

#include <QContact>
#include <QContactManager>
#include <QContactFetchRequest>
#include <QContactRemoveRequest>
#include <QContactSaveRequest>
#include <QContactTimestamp>
#include <QContactLocalIdFilter>
#include <QContactThumbnail>
#include <QContactAvatar>

#include <QVersitContactExporter>
#include <QVersitContactImporter>
#include <QVersitContactHandler>
#include <QVersitDocument>
#include <QVersitWriter>
#include <QVersitReader>
#include <QVersitProperty>
#include <QVersitContactImporterPropertyHandlerV2>
#include <QVersitContactExporterDetailHandlerV2>

#include <syncevo/SmartPtr.h>

#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/split.hpp>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

using namespace QtMobility;

/**
 * This handler represents QContactDetails which have no
 * mapping to vCard by storing them inside a X-SYNCEVO-QTCONTACTS
 * property.
 *
 * The exact format is:
 * X-SYNCEVOLUTION-QTCONTACTS:<detail>;(<field>;<encoding>;<serialized value>)*
 *
 * <detail> = detail name
 * <field> = field name
 * <encoding> = as in backup plugin (BOOL/INT/UINT/DATE/TIME/DATETIME/STRING/VARIANT)
 *              STRING = QString as UTF-8 string, with special characters escaped
 *              as in N
 *              VARIANT = anything else, including byte arrays
 *
 * This is similar to the QtMobility 1.1 backup plugin (http://doc.qt.nokia.com/qtmobility-1.1/versitplugins.html).
 * The main differences are:
 * - This handler has a 1:1 mapping between QContactDetail and
 *   vCard property; the backup plugin uses one property per QContactDetail
 *   field and groups to combine them.
 * - Details which have a mapping to vCard are left untouched.
 *   The backup plugin always adds at least the DetailUri.
 *
 * The reasons for implementing our own handler is:
 * - The "restore" part of the backup/restore plugin is completely missing
 *   in QtMobility 1.1 and therefore it is unusable.
 * - The single property per detail approach is more readable (IMHO, of course).
 * - Turning a property back into a detail is likely to be easier when all information
 *   is in single property.
 * - Groups in vCard are unusual and thus more likely to confuse peers.
 *   The extended format used by this handler only relies on the normal X- property
 *   extension.
 *
 * The restore from property part of this handler ignores all details
 * and fields which are not valid for the contact. In other words, it
 * does not define details.
 *
 * Example backup plugin:
 * G1.UID:{8c0bc9aa-9379-4aec-b8f1-78ba55992076}
 * G1.X-NOKIA-QCONTACTFIELD;DETAIL=Guid;FIELD=DetailUri:http://www.semanticdesk
 *  top.org/ontologies/2007/03/22/nco#default-contact-me#Guid
 * G2.N:;Me;;;
 * G2.X-NOKIA-QCONTACTFIELD;DETAIL=Name;FIELD=DetailUri:http://www.semanticdesk
 *  top.org/ontologies/2007/03/22/nco#default-contact-me#Name
 * G3.TEL;TYPE=VOICE:
 * G3.X-NOKIA-QCONTACTFIELD;DETAIL=PhoneNumber;FIELD=DetailUri:urn:uuid:5087e2a
 *  2-39f4-37a9-757c-ee291294f9e9
 * G4.X-NOKIA-QCONTACTFIELD;DETAIL=Pet;FIELD=Name:Rex
 * G4.X-NOKIA-QCONTACTFIELD;DETAIL=Pet;FIELD=Age;DATATYPE=INT:14
 *
 * Example this handler:
 * UID:{8c0bc9aa-9379-4aec-b8f1-78ba55992076}
 * N:;Me;;;
 * TEL:
 * X-SYNCEVO-QTCONTACTS:Pet^Name^STRING^Rex^Age^INT^14
 *
 * The somewhat strange ^ separator is necessary because custom properties cannot
 * be of compound type in QVersit (http://bugreports.qt.nokia.com/browse/QTMOBILITY-1298).
 */
class SyncEvoQtContactsHandler : public QVersitContactImporterPropertyHandlerV2,
                                 public QVersitContactExporterDetailHandlerV2
{
    const QMap<QString, QContactDetailDefinition> m_details;

public:
    /**
     * @param  details    definition of all details that are valid for a contact (only relevant for parsing vCard)
     */
    SyncEvoQtContactsHandler(const QMap<QString, QContactDetailDefinition> &details = QMap<QString, QContactDetailDefinition>()) :
        m_details(details)
    {}

    virtual void contactProcessed(const QContact &contact, QVersitDocument *document ) {}
    virtual void detailProcessed( const QContact &contact,
                                  const QContactDetail &detail,
                                  const QVersitDocument &document,
                                  QSet<QString> *processedFields,
                                  QList<QVersitProperty> *toBeRemoved,
                                  QList<QVersitProperty> *toBeAdded)
    {
        // ignore details if
        // - already encoded (assumed to do a good enough job)
        // - read-only = synthesized (we would not be able to write it back anyway)
        // - the default "Type = Contact"
        // - empty detail (empty QContactName otherwise would be encoded)
        if (!toBeAdded->empty() ||
            (detail.accessConstraints() & QContactDetail::ReadOnly) ||
            (detail.definitionName() == "Type" && contact.type() == "Contact") ||
            detail.isEmpty()) {
            return;
        }

        QStringList content;
        content << detail.definitionName(); // <detail>
        QVariantMap fields = detail.variantValues();
        for (QVariantMap::const_iterator entry = fields.begin();
             entry != fields.end();
             ++entry) {
            const QString &fieldName = entry.key();
            const QVariant &value = entry.value();
            content << fieldName; // <field>
            if (value.type() == QVariant::String) {
                content << "STRING" << value.toString().toUtf8();
            } else if (value.type() == QVariant::Bool) {
                content << "BOOL" << (value.toBool() ? "1" : "0");
            } else if (value.type() == QVariant::Int) {
                content << "INT" << QString::number(value.toInt());
            } else if (value.type() == QVariant::UInt) {
                content << "UINT" << QString::number(value.toUInt());
            } else if (value.type() == QVariant::Date) {
                content << "DATE" << value.toDate().toString(Qt::ISODate);
            } else if (value.type() == QVariant::DateTime) {
                content << "DATETIME" << value.toDateTime().toString(Qt::ISODate);
            } else {
                QByteArray valueBytes;
                QDataStream stream(&valueBytes, QIODevice::WriteOnly);
                stream << value;
                content << "VARIANT" << valueBytes.toHex().constData();
            }
            *processedFields << fieldName;
        }

        // Using QVersitProperty::CompoundType and the string list
        // as-is would be nice, but isn't supported by QtMobility 1.2.0 beta
        // because QVersitReader will not know that the property is
        // of compound type and will replace \; with ; without splitting
        // into individual strings first. See http://bugreports.qt.nokia.com/browse/QTMOBILITY-1298
        //
        // Workaround: replace ^ inside strings with |<hex value of ^> and then use ^ as separator
        // These characters were chosen because they are not special in vCard and thus
        // require no further escaping.
        QVersitProperty prop;
        prop.setName("X-SYNCEVO-QTCONTACTS");
#ifdef USE_QVERSIT_COMPOUND
        prop.setValueType(QVersitProperty::CompoundType);
        prop.setValue(QVariant(content));
#else
        StringEscape escape('|', "^");
        std::list<std::string> strings;
        BOOST_FOREACH(const QString &str, content) {
            strings.push_back(escape.escape(string(str.toUtf8().constData())));
        }
        prop.setValue(QVariant(QString::fromUtf8(boost::join(strings, "^").c_str())));
#endif
        *toBeAdded << prop;
    }

    virtual void documentProcessed(const QVersitDocument &document, QContact *contact) {}
    virtual void propertyProcessed( const QVersitDocument &document,
                                    const QVersitProperty &property,
                                    const QContact &contact,
                                    bool *alreadyProcessed,
                                    QList<QContactDetail> *updatedDetails)
    {
        if (*alreadyProcessed ||
            property.name() != "X-SYNCEVO-QTCONTACTS") {
            // not something that we need to parse
            return;
        }

        *alreadyProcessed = true;
#ifdef USE_QVERSIT_COMPOUND
        QStringList content = property.value<QStringList>();
#else
        QStringList content;
        StringEscape escape('|', "^");
        typedef boost::split_iterator<string::iterator> string_split_iterator;
        string valueString = property.value().toUtf8().constData();
        string_split_iterator it =
            boost::make_split_iterator(valueString, boost::first_finder("^", boost::is_iequal()));
        while (it != string_split_iterator()) {
            content << QString::fromUtf8(escape.unescape(std::string(it->begin(), it->end())).c_str());
            ++it;
        }
#endif
        // detail name available?
        if (content.size() > 0) {
            const QString &detailName = content[0];
            QMap<QString, QContactDetailDefinition>::const_iterator it = m_details.constFind(detailName);
            // detail still exists?
            if (it != m_details.constEnd()) {
                const QContactDetailDefinition &definition = *it;

                // now decode all fields and copy into new detail
                QContactDetail detail(content[0]);
                int i = 1;
                while (i + 2 < content.size()) {
                    const QString &fieldName = content[i++];
                    const QString &type = content[i++];
                    const QString &valueString = content[i++];
                    QVariant value;

                    if (type == "STRING") {
                        value.setValue(valueString);
                    } else if (type == "BOOL") {
                        value.setValue(valueString == "1");
                    } else if (type == "INT") {
                        value.setValue(valueString.toInt());
                    } else if (type == "UINT") {
                        value.setValue(valueString.toUInt());
                    } else if (type == "DATE") {
                        value.setValue(QDate::fromString(valueString, Qt::ISODate));
                    } else if (type == "DATETIME") {
                        value.setValue(QDateTime::fromString(valueString, Qt::ISODate));
                    } else if (type == "VARIANT") {
                        QByteArray valueBytes = QByteArray::fromHex(valueString.toAscii());
                        QDataStream stream(&valueBytes, QIODevice::ReadOnly);
                        stream >> value;
                    } else {
                        // unknown type, skip it
                        continue;
                    }

                    // skip fields which are (no longer) valid, have wrong type or wrong value
                    QMap<QString, QContactDetailFieldDefinition> fields = definition.fields();
                    QMap<QString, QContactDetailFieldDefinition>::const_iterator it2 =
                        fields.constFind(fieldName);
                    if (it2 != fields.constEnd()) {
                        if (it2->dataType() == value.type()) {
                            QVariantList allowed = it2->allowableValues();
                            if (allowed.empty() ||
                                allowed.indexOf(value) != -1) {
                                // add field
                                detail.setValue(fieldName, value);
                            }
                        }
                    }
                }

                // update contact with the new detail
                *updatedDetails << detail;
            }
        }
    }
};



class QtContactsData
{
    QtContactsSource *m_parent;
    QString m_managerURI;
    cxxptr<QContactManager> m_manager;

public:
    QtContactsData(QtContactsSource *parent,
                   const QString &managerURI) :
        m_parent(parent),
        m_managerURI(managerURI)
    {
        if (!qApp) {
            static const char *argv[] = { "SyncEvolution" };
            static int argc = 1;
            new QCoreApplication(argc, (char **)argv);
        }
    }

    static QList<QContactLocalId> createContactList(const string &uid)
    {
        QList<QContactLocalId> list;
        list.append(atoi(uid.c_str()));
        return list;
    }

    static QContactLocalIdFilter createFilter(const string &uid)
    {
        QContactLocalIdFilter filter;
        filter.setIds(createContactList(uid));
        return filter;
    }

    static string getLUID(const QContact &contact)
    {
        QContactLocalId id = contact.localId();
        return StringPrintf("%u", id);
    }

    static string getRev(const QContact &contact)
    {
        QContactTimestamp rev = contact.detail<QContactTimestamp>();
        QDateTime stamp = rev.lastModified();
        if (!stamp.isValid()) {
            stamp = rev.created();
        }
        return stamp.toString().toLocal8Bit().constData();
    }

    template<class T>
    void checkError(const char *op, T &req)
    {
        if (req.error()) {
            m_parent->throwError(StringPrintf("%s: failed with error %d", op, req.error()));
        }
    }
    template<class T>
    void checkError(const char *op,
                    T &req,
                    const QMap<int, QContactManager::Error> &errors)
    {
        if (errors.isEmpty()) {
            checkError(op, req);
        } else {
            list<string> res;
            foreach (int index, errors.keys()) {
                res.push_back(StringPrintf("entry #%d failed with error %d", index, errors[index]));
            }
            m_parent->throwError(StringPrintf("%s: failed with error %d, ", op, req.error()) +
                                 boost::join(res, ", "));
        }
    }

    friend class QtContactsSource;
};

QtContactsSource::QtContactsSource(const SyncSourceParams &params) :
    TrackingSyncSource(params)
{
    m_data = NULL;
    SyncSourceLogging::init(InitList<std::string>("N_FIRST") + "N_MIDDLE" + "N_LAST",
                            " ",
                            m_operations);
}

QtContactsSource::~QtContactsSource()
{
    delete m_data;
}

void QtContactsSource::open()
{
    QString buffer;
    QDebug(&buffer) << "available managers (default one first): " << QContactManager::availableManagers();
    SE_LOG_DEBUG(NULL, NULL, buffer.toUtf8().data());

    string id = getDatabaseID();
    m_data = new QtContactsData(this, id.c_str());
    cxxptr<QContactManager> manager(QContactManager::fromUri(m_data->m_managerURI),
                                    "QTContactManager");
    if (manager->error()) {
        throwError(StringPrintf("failed to open QtContact database %s, error code %d",
                                m_data->m_managerURI.toLocal8Bit().constData(),
                                manager->error()));
    }
    buffer = "";
    QDebug(&buffer) << manager->managerUri() << " manager supports contact types: " << manager->supportedContactTypes() <<
        " and data types: " << manager->supportedDataTypes();
    SE_LOG_DEBUG(NULL, NULL, buffer.toUtf8().data());

    m_data->m_manager = manager;
}

bool QtContactsSource::isEmpty()
{
    return false;
}

void QtContactsSource::close()
{
    m_data->m_manager.set(0);
}

QtContactsSource::Databases QtContactsSource::getDatabases()
{
    Databases result;
    QStringList availableManagers = QContactManager::availableManagers();

    result.push_back(Database("select database via QtContacts Manager URL",
                              "qtcontacts:tracker:"));
    return result;
}

void QtContactsSource::listAllItems(RevisionMap_t &revisions)
{
    QContactFetchRequest fetch;
    fetch.setManager(m_data->m_manager.get());

    // only need ID and time stamps
    QContactFetchHint hint;
    hint.setOptimizationHints(QContactFetchHint::OptimizationHints(QContactFetchHint::NoRelationships|QContactFetchHint::NoBinaryBlobs));
    hint.setDetailDefinitionsHint(QStringList() << QContactTimestamp::DefinitionName);
    fetch.setFetchHint(hint);

    fetch.start();
    fetch.waitForFinished();
    m_data->checkError("read all items", fetch);
    foreach (const QContact &contact, fetch.contacts()) {
        string revision = QtContactsData::getRev(contact);
        string luid = QtContactsData::getLUID(contact);
        if (luid == "2147483647" &&
            revision == "") {
            // Seems to be a special, artifical contact that always
            // exists. Ignore it.
            //
            // Also note that qtcontacts-tracker and/or QtContacts
            // spew out a warning on stdout about it (?):
            // skipping contact with unsupported IRI: "http://www.semanticdesktop.org/ontologies/2007/03/22/nco#default-contact-emergency"
            continue;
        }
        revisions[luid] = revision;
    }
}

void QtContactsSource::readItem(const string &uid, std::string &item, bool raw)
{
    QContactFetchRequest fetch;
    fetch.setManager(m_data->m_manager.get());
    fetch.setFilter(QtContactsData::createFilter(uid));
    fetch.start();
    fetch.waitForFinished();

    QList<QContact> contacts = fetch.contacts();
    for (int i = 0; i < contacts.size(); ++i) {
        QContact &contact = contacts[i];
        const QContactAvatar avatar = contact.detail(QContactAvatar::DefinitionName);
        const QContactThumbnail thumb = contact.detail(QContactThumbnail::DefinitionName);
        if (!avatar.isEmpty() && thumb.isEmpty()) {
            QImage image(avatar.imageUrl().path());
            QContactThumbnail thumbnail;
            thumbnail.setThumbnail(image);
            contact.saveDetail(&thumbnail);
        }
    }

    QStringList profiles;
#ifdef USE_PROFILE_BACKUP_RAW_FORMAT
    if (raw) {
        profiles << QVersitContactHandlerFactory::ProfileBackup;
    }
#endif
    SyncEvoQtContactsHandler handler;
    QVersitContactExporter exporter(profiles);
    exporter.setDetailHandler(&handler);
    if (!exporter.exportContacts(contacts, QVersitDocument::VCard30Type)) {
        throwError(uid + ": encoding as vCard 3.0 failed");
    }
    QByteArray vcard;
    QVersitWriter writer(&vcard);
    if (!writer.startWriting(exporter.documents())) {
        throwError(uid + ": writing as vCard 3.0 failed");
    }
    writer.waitForFinished();
    item = vcard.constData();
    m_data->checkError("encoding as vCard 3.0", writer);
}

TrackingSyncSource::InsertItemResult QtContactsSource::insertItem(const string &uid, const std::string &item, bool raw)
{
    QVersitReader reader(QByteArray(item.c_str()));
    if (!reader.startReading()) {
        throwError("reading vCard failed");
    }
    reader.waitForFinished();
    m_data->checkError("decoding vCard", reader);

    QStringList profiles;
#ifdef USE_PROFILE_BACKUP_RAW_FORMAT
    if (raw) {
        profiles << QVersitContactHandlerFactory::ProfileBackup;
    }
#endif
    SyncEvoQtContactsHandler handler(m_data->m_manager->detailDefinitions());
    QVersitContactImporter importer(profiles);
    importer.setPropertyHandler(&handler);
    if (!importer.importDocuments(reader.results())) {
        throwError("importing vCard failed");
    }

    QList<QContact> contacts = importer.contacts();
    QContact &contact = contacts.first();

    if (!uid.empty()) {
        QContactId id;
        id.setManagerUri(m_data->m_managerURI);
        id.setLocalId(atoi(uid.c_str()));
        contact.setId(id);
    }

    QContactSaveRequest save;
    save.setManager(m_data->m_manager.get());
    save.setContacts(QList<QContact>() << contact);
    save.start();
    save.waitForFinished();
    m_data->checkError("saving contact", save, save.errorMap());

    QList<QContact> savedContacts = save.contacts();
    QContact &savedContact = savedContacts.first();

    // Saving is not guaranteed to update the time stamp (BMC #5710).
    // Need to read again.
    QContactFetchRequest fetch;
    fetch.setManager(m_data->m_manager.get());
    fetch.setFilter(QtContactsData::createFilter(QtContactsData::getLUID(savedContact)));
    QContactFetchHint hint;
    hint.setOptimizationHints(QContactFetchHint::OptimizationHints(QContactFetchHint::NoRelationships|QContactFetchHint::NoBinaryBlobs));
    hint.setDetailDefinitionsHint(QStringList() << QContactTimestamp::DefinitionName);
    fetch.setFetchHint(hint);
    fetch.start();
    fetch.waitForFinished();
    QContact &finalContact = fetch.contacts().first();

    return InsertItemResult(QtContactsData::getLUID(savedContact),
                            QtContactsData::getRev(finalContact),
                            false);
}


void QtContactsSource::removeItem(const string &uid)
{
#if 1
    QContactRemoveRequest remove;
    remove.setManager(m_data->m_manager.get());
    remove.setContactIds(QtContactsData::createContactList(uid));
    remove.start();
    remove.waitForFinished();
    m_data->checkError("remove contact", remove, remove.errorMap());
#else
    m_data->m_manager->removeContact(atoi(uid.c_str()));
#endif
}

std::string QtContactsSource::getDescription(const string &luid)
{
    try {
        QContactFetchRequest fetch;
        fetch.setManager(m_data->m_manager.get());
        fetch.setFilter(QtContactsData::createFilter(luid));
        fetch.start();
        fetch.waitForFinished();
        if (fetch.contacts().isEmpty()) {
            return "";
        }
        QContact contact = fetch.contacts().first();
        string descr = contact.displayLabel().toLocal8Bit().constData();
        return descr;
    } catch (...) {
        // Instead of failing we log the error and ask
        // the caller to log the UID. That way transient
        // errors or errors in the logging code don't
        // prevent syncs.
        handleException();
        return "";
    }
}



SE_END_CXX

#endif /* ENABLE_QTCONTACTS */

#ifdef ENABLE_MODULES
# include "QtContactsSourceRegister.cpp"
#endif
