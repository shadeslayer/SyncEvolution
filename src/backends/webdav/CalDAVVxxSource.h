/*
 * Copyright (C) 2010 Intel Corporation
 */

#ifndef INCL_CALDAVVXXSOURCE
#define INCL_CALDAVVXXSOURCE

#include <config.h>

#ifdef ENABLE_DAV

#include "WebDAVSource.h"
#include <syncevo/MapSyncSource.h>
#include <syncevo/eds_abi_wrapper.h>
#include <syncevo/SmartPtr.h>

#include <boost/utility.hpp>
#include <boost/shared_ptr.hpp>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

/**
 * Supports VJOURNAL and VTODO via CalDAV.
 *
 * In contrast to CalDAVSource, no complex handling
 * of UID/RECURRENCE-ID is necessary because those do not
 * apply to VJOURNAL and VTODO.
 *
 * Therefore CalDAVVxxSource is much closer to CardDAVSource,
 * except that it uses CalDAV.
 */
class CalDAVVxxSource : public WebDAVSource,
    public SyncSourceLogging
{
 public:
    /**
     * @param content     "VJOURNAL" or "VTODO"
     */
    CalDAVVxxSource(const std::string &content,
                    const SyncSourceParams &params,
                    const boost::shared_ptr<SyncEvo::Neon::Settings> &settings);

    /* implementation of SyncSourceSerialize interface */
    virtual std::string getMimeType() const {
        return m_content == "VJOURNAL" ?
            "text/calendar+plain" :
            "text/calendar";
    }
    virtual std::string getMimeVersion() const { return "2.0"; }

    // implementation of SyncSourceLogging callback
    virtual std::string getDescription(const string &luid);

 protected:
    // implementation of WebDAVSource callbacks
    virtual std::string serviceType() const { return "caldav"; }
    virtual bool typeMatches(const StringMap &props) const;
    virtual std::string homeSetProp() const { return "urn:ietf:params:xml:ns:caldav:calendar-home-set"; }
    virtual std::string wellKnownURL() const { return "/.well-known/caldav"; }
    virtual std::string contentType() const { return "text/calendar; charset=utf-8"; }
    virtual std::string suffix() const { return ".ics"; }
    virtual std::string getContent() const { return m_content; }
    virtual bool getContentMixed() const { return true; }

 private:
    const std::string m_content;
};

SE_END_CXX

#endif // ENABLE_DAV
#endif // INCL_CALDAVVXXSOURCE
