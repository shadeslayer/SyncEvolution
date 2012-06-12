/*
 * Copyright (C) 2010 Intel Corporation
 */

#include "CalDAVVxxSource.h"

#ifdef ENABLE_DAV

#include <syncevo/declarations.h>
SE_BEGIN_CXX

// TODO: use EDS backend icalstrdup.c
#define ical_strdup(_x) (_x)

CalDAVVxxSource::CalDAVVxxSource(const std::string &content,
                                 const SyncSourceParams &params,
                                 const boost::shared_ptr<Neon::Settings> &settings) :
    WebDAVSource(params, settings),
    m_content(content)
{
    SyncSourceLogging::init(InitList<std::string>("SUMMARY") + "LOCATION",
                            " ",
                            m_operations);
}

std::string CalDAVVxxSource::getDescription(const string &luid)
{
    // TODO
    return "";
}

bool CalDAVVxxSource::typeMatches(const StringMap &props) const
{
    std::string davcomp = StringPrintf("<urn:ietf:params:xml:ns:caldavcomp name='%s'></urn:ietf:params:xml:ns:caldavcomp>",
                                       m_content.c_str());

    StringMap::const_iterator it = props.find("urn:ietf:params:xml:ns:caldav:supported-calendar-component-set");
    if (it != props.end() &&
        it->second.find(davcomp) != std::string::npos) {
        return true;
    } else {
        return false;
    }
}

SE_END_CXX

#endif // ENABLE_DAV
