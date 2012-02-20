/*
 * Copyright (C) 2010 Intel Corporation
 */

#ifndef INCL_WEBDAVSOURCE
#define INCL_WEBDAVSOURCE

#include <config.h>

#include <syncevo/SyncConfig.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX
extern BoolConfigProperty &WebDAVCredentialsOkay();
SE_END_CXX

#ifdef ENABLE_DAV

#include <syncevo/TrackingSyncSource.h>
#include <boost/noncopyable.hpp>
#include "NeonCXX.h"

SE_BEGIN_CXX

class ContextSettings;

/**
 * Implements generic access to a WebDAV collection.
 *
 * Change tracking is based on TrackingSyncSource, with the following mapping:
 * - locally unique id = relative URI of resource in collection
 * - revision string = ETag of resource in collection
 */
class WebDAVSource : public TrackingSyncSource, private boost::noncopyable
{
 public:
    /**
     * @param settings     instance which provides necessary settings callbacks for Neon
     */
    WebDAVSource(const SyncSourceParams &params,
                 const boost::shared_ptr<Neon::Settings> &settings);

    /**
     * Utility function: replace HTML entities until none are left
     * in the decoded string - for Yahoo! Contacts bug.
     */
    static void replaceHTMLEntities(std::string &item);

 protected:
    /**
     * Initialize HTTP session and locate the right collection.
     * To be called after open() to do the heavy initializtion
     * work.
     */
    void contactServer();

    /**
     * Scan server based on username/password/syncURL. Callback is
     * passed name and URL of each collection (in this order), may
     * return false to stop scanning gracefully or throw errors to
     * abort.
     *
     * @return true if scanning completed, false if callback requested stop
     */
    bool findCollections(const boost::function<bool (const std::string &,
                                                     const Neon::URI &)> &callback);

    /** store resource URL permanently after successful sync */
    void storeServerInfos();

    /* implementation of SyncSource interface */
    virtual void open();
    virtual bool isEmpty();
    virtual void close();
    virtual Databases getDatabases();
    void getSynthesisInfo(SynthesisInfo &info,
                          XMLConfigFragments &fragments);

    /** intercept TrackingSyncSource::beginSync() to do the expensive initialization */
    virtual void beginSync(const std::string &lastToken, const std::string &resumeToken) {
        contactServer();
        TrackingSyncSource::beginSync(lastToken, resumeToken);
    }
    /** hook into session to store infos */
    virtual std::string endSync(bool success) {
        if (success) {
             storeServerInfos();
	}
	return TrackingSyncSource::endSync(success);
    }

    /* implementation of TrackingSyncSource interface */
    virtual std::string databaseRevision();
    virtual void listAllItems(RevisionMap_t &revisions);
    virtual InsertItemResult insertItem(const string &luid, const std::string &item, bool raw);
    void readItem(const std::string &luid, std::string &item, bool raw);
    virtual void removeItem(const string &uid);

    /**
     * A resource path is turned into a locally unique ID by
     * stripping the calendar path prefix, or keeping the full
     * path for resources outside of the calendar.
     */
    std::string path2luid(const std::string &path);

    /**
     * Full path can be reconstructed from relative LUID by
     * appending it to the calendar path, or using the path
     * as it is.
     */
    std::string luid2path(const std::string &luid);

    /**
     * ETags are turned into revision strings by ignoring the W/ weak
     * marker (because we don't care for literal storage of items) and
     * by stripping the quotation marks.
     */
    std::string ETag2Rev(const std::string &etag);

    /**
     * Calculates the time after which the next operation is
     * expected to complete before giving up, based on
     * current time and retry settings.
     * @return absolute time, empty if no retrying allowed
     */
    Timespec createDeadline() const;

    // access to neon session and calendar, valid between open() and close()
    boost::shared_ptr<Neon::Session> getSession() { return m_session; }
    Neon::URI &getCalendar() { return m_calendar; }

    // access to settings owned by this instance
    Neon::Settings &settings() { return *m_settings; }

    /**
     * SRV type to be used for finding URL (caldav, carddav, ...)
     */
    virtual string serviceType() const = 0;

    /**
     * return true if resource with the given properties is something we can work with;
     * properties which are queried are currently hard-coded in WebDAVSource::open()
     *
     * @param props    mapping from fully qualified properties to their values,
     *                 normalized by neon library
     */
    virtual bool typeMatches(const StringMap &props) const = 0;

    /**
     * property pointing to URL path with suitable collections ("calendar-home-set", "address-home-set", ...);
     * must be known to WebDAVSource::open() already
     */
    virtual std::string homeSetProp() const = 0;

    /**
     * well-known URL, including full path (/.well-known/caldav),
     * empty if none
     */
    virtual std::string wellKnownURL() const = 0;

    /**
     * HTTP content type for PUT
     */
    virtual std::string contentType() const = 0;

    /**
     * create new resource name (only last component, not full path)
     *
     * Some servers require that this matches the item content,
     * for example Yahoo CardDAV wants <uid>.vcf.
     *
     * @param item    original item data
     * @param buffer  empty, may be filled with modified item data
     * @retval luid   new resource name, not URL encoded
     * @return item data to be sent
     */
    virtual const std::string *createResourceName(const std::string &item, std::string &buffer, std::string &luid) { luid = UUID(); return &item; }

    /**
     * optionally modify item content to match the luid of the item we are going to update
     */
    virtual const std::string *setResourceName(const std::string &item, std::string &buffer, const std::string &luid) { return &item; }

 private:
    /** settings to be used, never NULL, may be the same as m_contextSettings */
    boost::shared_ptr<Neon::Settings> m_settings;
    /** settings constructed by us instead of caller, may be NULL */
    boost::shared_ptr<ContextSettings> m_contextSettings;
    boost::shared_ptr<Neon::Session> m_session;

    /** normalized path: including backslash, URI encoded */
    Neon::URI m_calendar;

    /** information about certain paths (path->property->value)*/
    typedef std::map<std::string, std::map<std::string, std::string> > Props_t;
    Props_t m_davProps;

    /** extract value from first <DAV:href>value</DAV:href>, empty string if not inside propval */
    std::string extractHREF(const std::string &propval);
    /** extract all <DAV:href>value</DAV:href> values from a set, empty if none */
    std::list<std::string> extractHREFs(const std::string &propval);

    void openPropCallback(const Neon::URI &uri,
                          const ne_propname *prop,
                          const char *value,
                          const ne_status *status);

    void listAllItemsCallback(const Neon::URI &uri,
                              const ne_prop_result_set *results,
                              RevisionMap_t &revisions,
                              bool &failed);

    void backupData(const boost::function<Operations::BackupData_t> &op,
                    const Operations::ConstBackupInfo &oldBackup,
                    const Operations::BackupInfo &newBackup,
                    BackupReport &report) {
        contactServer();
        op(oldBackup, newBackup, report);
    }

    void restoreData(const boost::function<Operations::RestoreData_t> &op,
                     const Operations::ConstBackupInfo &oldBackup,
                     bool dryrun,
                     SyncSourceReport &report) {
        contactServer();
        op(oldBackup, dryrun, report);
    }

    /**
     * return true if the resource with the given properties is one
     * of those collections which is guaranteed to not contain
     * other, unrelated collections (a CalDAV collection must not
     * contain a CardDAV collection, for example)
     */
    bool ignoreCollection(const StringMap &props) const;

 protected:
    /**
     * Extracts ETag from response header, empty if not found.
     */
    std::string getETag(Neon::Request &req) { return ETag2Rev(req.getResponseHeader("ETag")); }

    /**
     * Extracts new LUID from response header, empty if not found.
     */
    std::string getLUID(Neon::Request &req);
};

SE_END_CXX

#endif // ENABLE_DAV
#endif // INCL_WEBDAVSOURCE
