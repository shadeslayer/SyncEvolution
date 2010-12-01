/*
 * Copyright (C) 2010 Intel Corporation
 */

#include "CardDAVSource.h"

#ifdef ENABLE_DAV

#include <syncevo/declarations.h>
SE_BEGIN_CXX

// TODO: use EDS backend icalstrdup.c
#define ical_strdup(_x) (_x)

CardDAVSource::CardDAVSource(const SyncSourceParams &params,
                             const boost::shared_ptr<Neon::Settings> &settings) :
    WebDAVSource(params, settings)
{
    SyncSourceLogging::init(InitList<std::string>("N_FIRST") + "N_MIDDLE" + "N_LAST",
                            " ",
                            m_operations);
}

std::string CardDAVSource::getDescription(const string &luid)
{
    // TODO
    return "";
}

bool CardDAVSource::typeMatches(const StringMap &props) const
{
    StringMap::const_iterator it = props.find("DAV::resourcetype");
    if (it != props.end()) {
        const std::string &type = it->second;
        // allow parameters (no closing bracket)
        // and allow also "carddavaddressbook" (caused by invalid Neon 
        // string concatenation?!)
        if (type.find("<urn:ietf:params:xml:ns:carddav:addressbook") != type.npos ||
            type.find("<urn:ietf:params:xml:ns:carddavaddressbook") != type.npos) {
            return true;
        }
    }
    return false;
}

static const std::string UID("\nUID:");
static const std::string END_VCARD("\nEND:VCARD");
static const std::string SUFFIX(".vcf");

const std::string *CardDAVSource::createResourceName(const std::string &item, std::string &buffer, std::string &luid)
{
    luid = extractUID(item);
    if (luid.empty()) {
        // must modify item
        luid = UUID();
        buffer = item;
        size_t start = buffer.find(END_VCARD);
        if (start != buffer.npos) {
            start++;
            buffer.insert(start, StringPrintf("UID:%s\n", luid.c_str()));
        }
        luid += SUFFIX;
        return &buffer;
    } else {
        luid += SUFFIX;
        return &item;
    }
}

const std::string *CardDAVSource::setResourceName(const std::string &item, std::string &buffer, const std::string &luid)
{
    std::string olduid = luid;
    if (boost::ends_with(olduid, SUFFIX)) {
        olduid.resize(olduid.size() - SUFFIX.size());
    }

    // first check if the item already contains the right UID
    std::string uid = extractUID(item);
    if (uid == olduid) {
        return &item;
    }

    // insert or overwrite
    buffer = item;
    size_t start = buffer.find(UID);
    if (start != buffer.npos) {
        start += UID.size();
        size_t end = buffer.find("\n", start);
        if (end != buffer.npos) {
            // overwrite
            buffer.replace(start, end, olduid);
        }
    } else {
        // insert
        start = buffer.find(END_VCARD);
        if (start != buffer.npos) {
            start++;
            buffer.insert(start, StringPrintf("UID:%s\n", olduid.c_str()));
        }
    }
    return &buffer;
}

std::string CardDAVSource::extractUID(const std::string &item)
{
    std::string luid;
    // find UID, use that plus ".vcf" as resource name (expected by Yahoo Contacts)
    size_t start = item.find(UID);
    if (start != item.npos) {
        start += UID.size();
        size_t end = item.find("\n", start);
        if (end != item.npos) {
            luid = item.substr(start, end - start);
            if (boost::ends_with(luid, "\r")) {
                luid.resize(luid.size() - 1);
            }
        }
    }
    return luid;
}

SE_END_CXX

#endif // ENABLE_DAV
