/*
 * Copyright (C) 2007 Patrick Ohly
 * Copyright (C) 2007 Funambol
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <memory>
#include <map>
#include <sstream>
using namespace std;

#include "config.h"

#ifdef ENABLE_ADDRESSBOOK

#include "AddressBookSource.h"

#include <common/base/Log.h>
#include "vocl/VConverter.h"

#include <CoreFoundation/CoreFoundation.h>

using namespace vocl;

/** converts a CFString to std::string - does not free input */
static string CFString2Std(CFStringRef cfstring) {
    CFIndex len = CFStringGetMaximumSizeOfFileSystemRepresentation(cfstring);
    arrayptr<char> buf(new char[len], "buffer");
    if (!CFStringGetFileSystemRepresentation(cfstring, buf, len)) {
        throw runtime_error("cannot convert string");
    }
    return string((char *)buf);
}

AddressBookSource::AddressBookSource(const string &name,
                                     SyncSourceConfig *sc,
                                     const string &changeId,
                                     const string &id) :
    EvolutionSyncSource(name, sc, changeId, id)
{
}

AddressBookSource::AddressBookSource(const AddressBookSource &other) :
    EvolutionSyncSource(other)
{}

EvolutionSyncSource::sources AddressBookSource::getSyncBackends()
{
    EvolutionSyncSource::sources result;

    result.push_back(EvolutionSyncSource::source("<<system>>", ""));
    return result;
}

void AddressBookSource::open()
{
    m_addressbook.set(ABGetSharedAddressBook(), "address book");
}

void AddressBookSource::beginSyncThrow(bool needAll,
                                       bool needPartial,
                                       bool deleteLocal)
{
    ref<CFArrayRef> allPersons(ABCopyArrayOfAllPeople(m_addressbook), "list of all people");

    for (CFIndex i = 0; i < CFArrayGetCount(allPersons); i++) {
        ref<CFStringRef> cfuid(ABRecordCopyUniqueId((ABRecordRef)CFArrayGetValueAtIndex(allPersons, i)), "reading UID");
        string uid(CFString2Std(cfuid));

        if (deleteLocal) {
            if (!ABRemoveRecord(m_addressbook, (ABRecordRef)CFArrayGetValueAtIndex(allPersons, i))) {
                throw runtime_error("deleting contact failed");
            }
        } else {
            if (needAll) {
                m_allItems.addItem(uid);
            }
            if (needPartial) {
                // TODO: check for changes
            }
        }
    }
}

void AddressBookSource::endSyncThrow()
{
}

void AddressBookSource::close()
{
    endSyncThrow();
    m_addressbook = NULL;
}

void AddressBookSource::exportData(ostream &out)
{
    // TODO
}

SyncItem *AddressBookSource::createItem( const string &uid, SyncState state )
{
    logItem(uid, "extracting from address book");

    ref<CFStringRef> cfuid(CFStringCreateWithFileSystemRepresentation(NULL, uid.c_str()), "cfuid");
    ref<ABPersonRef> person((ABPersonRef)ABCopyRecordForUniqueId(m_addressbook, cfuid), "contact");
    ref<CFDataRef> vcard(ABPersonCopyVCardRepresentation(person), "vcard");

    LOG.debug("%*s", (int)CFDataGetLength(vcard), (const char *)CFDataGetBytePtr(vcard));

    auto_ptr<SyncItem> item(new SyncItem(uid.c_str()));
    item->setData(CFDataGetBytePtr(vcard), CFDataGetLength(vcard));
    item->setDataType(getMimeType());
    item->setModificationTime(0);
    item->setState(state);

    return item.release();
}

int AddressBookSource::addItemThrow(SyncItem& item)
{
    int status = STC_OK;
    string data(getData(item));
    ref<CFDataRef> vcard(CFDataCreate(NULL, (const UInt8 *)data.c_str(), data.size()), "vcard");
    ref<ABPersonRef> person((ABPersonRef)ABPersonCreateWithVCardRepresentation(vcard));

    if (person) {
        if (ABAddRecord(m_addressbook, person)) {
            ref<CFStringRef> cfuid(ABRecordCopyUniqueId(person), "uid");
            string uid(CFString2Std(cfuid));
            item.setKey(uid.c_str());
        } else {
            throwError("storing new contact");
        }
    } else {
        throwError(string("parsing vcard ") + data);
    }
    return status;
}

int AddressBookSource::updateItemThrow(SyncItem& item)
{
    int status = STC_OK;

    // TODO

    return status;
}

int AddressBookSource::deleteItemThrow(SyncItem& item)
{
    int status = STC_OK;
    ref<CFStringRef> cfuid(CFStringCreateWithFileSystemRepresentation(NULL, item.getKey()), "cfuid");
    ref<ABPersonRef> person((ABPersonRef)ABCopyRecordForUniqueId(m_addressbook, cfuid));

    if (person) {
        if (!ABRemoveRecord(m_addressbook, person)) {
            throwError(string("deleting contact ") + item.getKey());
        }
    } else {
        LOG.debug("%s: %s: request to delete non-existant contact ignored",
                  getName(), item.getKey());
    }

    return status;
}

void AddressBookSource::logItem(const string &uid, const string &info, bool debug)
{
    if (LOG.getLevel() >= (debug ? LOG_LEVEL_DEBUG : LOG_LEVEL_INFO)) {
        string line;

#if 0
        // TODO

        if (e_book_get_contact( m_addressbook,
                                uid.c_str(),
                                &contact,
                                &gerror )) {
            const char *fileas = (const char *)e_contact_get_const( contact, E_CONTACT_FILE_AS );
            if (fileas) {
                line += fileas;
            } else {
                const char *name = (const char *)e_contact_get_const( contact, E_CONTACT_FULL_NAME );
                if (name) {
                    line += name;
                } else {
                    line += "<unnamed contact>";
                }
            }
        } else {
            line += "<name unavailable>";
        }
#endif

        line += " (";
        line += uid;
        line += "): ";
        line += info;
        
        (LOG.*(debug ? &Log::debug : &Log::info))( "%s: %s", getName(), line.c_str() );
    }
}

void AddressBookSource::logItem(SyncItem &item, const string &info, bool debug)
{
    if (LOG.getLevel() >= (debug ? LOG_LEVEL_DEBUG : LOG_LEVEL_INFO)) {
        string line;
        const char *data = (const char *)item.getData();
        int datasize = item.getDataSize();
        if (datasize <= 0) {
            data = "";
            datasize = 0;
        }
        string vcard( data, datasize );

        size_t offset = vcard.find( "FN:");
        if (offset != vcard.npos) {
            int len = vcard.find( "\r", offset ) - offset - 3;
            line += vcard.substr( offset + 3, len );
        } else {
            line += "<unnamed contact>";
        }

        if (!item.getKey() ) {
            line += ", NULL UID (?!)";
        } else if (!strlen( item.getKey() )) {
            line += ", empty UID";
        } else {
            line += ", ";
            line += item.getKey();

#if 0
            // TODO
            EContact *contact;
            GError *gerror = NULL;
            if (e_book_get_contact( m_addressbook,
                                    item.getKey(),
                                    &contact,
                                    &gerror )) {
                line += ", EV ";
                const char *fileas = (const char *)e_contact_get_const( contact, E_CONTACT_FILE_AS );
                if (fileas) {
                    line += fileas;
                } else {
                    const char *name = (const char *)e_contact_get_const( contact, E_CONTACT_FULL_NAME );
                    if (name) {
                        line += name;
                    } else {
                        line += "<unnamed contact>";
                    }
                }
            } else {
                line += ", not in Evolution";
            }
#endif
        }
        line += ": ";
        line += info;
        
        (LOG.*(debug ? &Log::debug : &Log::info))( "%s: %s", getName(), line.c_str() );
    }
}


#ifdef ENABLE_MODULES

extern "C" EvolutionSyncSource *SyncEvolutionCreateSource(const string &name,
                                                          SyncSourceConfig *sc,
                                                          const string &changeId,
                                                          const string &id,
                                                          const string &mimeType)
{
    if (mimeType == "AddressBook") {
        return new AddressBookSource(name, sc, changeId, id, EVC_FORMAT_VCARD_21);
    } else {
        return NULL;
    }
}

#endif /* ENABLE_MODULES */

#endif /* ENABLE_ADDRESSBOOK */
