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

void CardDAVSource::readItem(const std::string &luid, std::string &item, bool raw)
{
    WebDAVSource::readItem(luid, item, raw);

    // Workaround for Yahoo! Contacts: it encodes
    //   backslash \ single quote ' double quote "
    // as
    //   NOTE;CHARSET=utf-8;ENCODING=QUOTED-PRINTABLE: =
    //    backslash &amp;#92; single quote &#39; double quote &quot;
    //
    // This is just plain wrong. The backslash even seems to be
    // encoded twice: \ -> &#92; -> &amp;#92;
    //
    // I don't see any way to detect this broken encoding reliably
    // at runtime. In the meantime deal with it by always replacing
    // HTML enties until none are left. Obviously that means that
    // it is impossible to put HTML entities into a contact value.
    // TODO: better detection of this server bug.
    if (false) {
        replaceHTMLEntities(item);
    }
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

SE_END_CXX

#endif // ENABLE_DAV
