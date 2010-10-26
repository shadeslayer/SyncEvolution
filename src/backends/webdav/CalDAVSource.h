/*
 * Copyright (C) 2010 Intel Corporation
 */

#ifndef INCL_CALDAVSOURCE
#define INCL_CALDAVSOURCE

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

class CalDAVSource : public WebDAVSource,
    public SubSyncSource,
    public SyncSourceLogging
{
 public:
    CalDAVSource(const SyncSourceParams &params, const boost::shared_ptr<SyncEvo::Neon::Settings> &settings);

    /* implementation of SyncSourceSerialize interface */
    virtual const char *getMimeType() const { return "text/calendar"; }
    virtual const char *getMimeVersion() const { return "2.0"; }

    /* implementation of SubSyncSource interface */
    virtual void listAllSubItems(SubRevisionMap_t &revisions);
    virtual SubItemResult insertSubItem(const std::string &uid, const std::string &subid,
                                        const std::string &item);
    virtual void readSubItem(const std::string &uid, const std::string &subid, std::string &item);
    virtual std::string removeSubItem(const string &uid, const std::string &subid);
    virtual void flushItem(const string &uid);
    virtual std::string getSubDescription(const string &uid, const string &subid);

    // implementation of SyncSourceLogging callback
    virtual std::string getDescription(const string &luid);

    /**
     * Dump each resource item unmodified into the given directory.
     * The ConfigNode stores the luid/etag mapping.
     */
    void backupData(const SyncSource::Operations::ConstBackupInfo &oldBackup,
                    const SyncSource::Operations::BackupInfo &newBackup,
                    BackupReport &report);

    /**
     * Restore database from data stored in backupData(). Will be
     * called inside open()/close() pair. beginSync() is *not* called.
     */
    void restoreData(const SyncSource::Operations::ConstBackupInfo &oldBackup,
                     bool dryrun,
                     SyncSourceReport &report);

    // disambiguate getSynthesisAPI()
    SDKInterface *getSynthesisAPI() const { return SubSyncSource::getSynthesisAPI(); }

 private:
    /**
     * Information about each merged item.
     */
    class Event : boost::noncopyable {
    public:
        /** the ID used by WebDAVSource */
        std::string m_DAVluid;

        /** revision string in WebDAVSource */
        std::string m_etag;

        /**
         * the list of simplified RECURRENCE-IDs (without time zone,
         * see icalTime2Str()), empty string for VEVENT without
         * RECURRENCE-ID
         */
        std::set<std::string> m_subids;

        /**
         * parsed VCALENDAR component representing the current
         * state of the item as it exists on the WebDAV server,
         * must be kept up-to-date as we make changes, may be NULL
         */
        eptr<icalcomponent> m_calendar;

        /** date-time as string, without time zone */
        static std::string icalTime2Str(const icaltimetype &tt);

        /** RECURRENCE-ID, empty if none */
        static std::string getSubID(icalcomponent *icomp);
    };

    /**
     * A cache of information about each merged item. Maps from
     * WebDAVSource local ID to Event.
     */
    class EventCache : public std::map<std::string, boost::shared_ptr<Event> >
    {
      public:
        EventCache() : m_initialized(false) {}
        bool m_initialized;
    } m_cache;

    Event &loadItem(const std::string &davLUID);
    Event &loadItem(Event &event);

    /** callback for listAllSubItems: parse and add new item */
    int appendItem(SubRevisionMap_t &revisions,
                   std::string &href,
                   std::string &etag,
                   std::string &data);

    /** callback for backupData(): dump into backup */
    int backupItem(ItemCache &cache,
                   std::string &href,
                   std::string &etag,
                   std::string &data);
};

SE_END_CXX

#endif // ENABLE_DAV
#endif // INCL_CALDAVSOURCE
