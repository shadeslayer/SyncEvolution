/*
 * Copyright (C) 2010 Intel Corporation
 */

#ifndef INCL_CARDDAVSOURCE
#define INCL_CARDDAVSOURCE

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

class CardDAVSource : public WebDAVSource,
    public SyncSourceLogging
{
 public:
    CardDAVSource(const SyncSourceParams &params, const boost::shared_ptr<SyncEvo::Neon::Settings> &settings);

    /* implementation of SyncSourceSerialize interface */
    virtual std::string getMimeType() const { return "text/vcard"; }
    virtual std::string getMimeVersion() const { return "3.0"; }

    // implementation of SyncSourceLogging callback
    virtual std::string getDescription(const string &luid);

    // implements vCard specific conversions on top of generic WebDAV readItem()
    void readItem(const std::string &luid, std::string &item, bool raw);

 protected:
    // implementation of WebDAVSource callbacks
    virtual std::string serviceType() const { return "carddav"; }
    virtual bool typeMatches(const StringMap &props) const;
    virtual std::string homeSetProp() const { return "urn:ietf:params:xml:ns:carddav:addressbook-home-set"; }
    virtual std::string wellKnownURL() const { return "/.well-known/carddav"; }
    virtual std::string contentType() const { return "text/vcard; charset=utf-8"; }
    virtual std::string getContent() const { return "VCARD"; }
    virtual bool getContentMixed() const { return false; }
};

SE_END_CXX

#endif // ENABLE_DAV
#endif // INCL_CARDDAVSOURCE
