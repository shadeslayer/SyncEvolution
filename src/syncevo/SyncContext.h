/*
 * Copyright (C) 2005-2009 Patrick Ohly <patrick.ohly@gmx.de>
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

#ifndef INCL_EVOLUTIONSYNCCLIENT
#define INCL_EVOLUTIONSYNCCLIENT

#include <syncevo/SmartPtr.h>
#include <syncevo/SyncConfig.h>
#include <syncevo/SyncML.h>
#include <syncevo/SynthesisEngine.h>

#include <string>
#include <set>
#include <map>
#include <stdint.h>
using namespace std;

#include <boost/smart_ptr.hpp>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

class TransportAgent;
class SourceList;
class SyncSource;

struct SuspendFlags
{
    static const time_t ABORT_INTERVAL = 2; 
    enum CLIENT_STATE
    {
        CLIENT_NORMAL,
        CLIENT_SUSPEND,
        CLIENT_ABORT,
        CLIENT_ILLEGAL
    }state;
    time_t last_suspend;

    SuspendFlags():state(CLIENT_NORMAL),last_suspend(0)
    {}
};

/**
 * This is the main class inside SyncEvolution which
 * looks at the configuration, activates all enabled
 * sources and executes the synchronization.
 *
 * All interaction with the user (reporting progress, asking for
 * passwords, ...) is done via virtual methods. The default
 * implementation of those uses stdin/out.
 *
 */
class SyncContext : public SyncConfig, public ConfigUserInterface {
    const string m_server;
    const set<string> m_sources;
    const bool m_doLogging;
    bool m_quiet;
    bool m_dryrun;

    /**
     * flags for suspend and abort
     */
    static SuspendFlags s_flags;

    /**
     * a pointer to the active SourceList instance if one exists; 
     * used for error handling in throwError() on the iPhone
     */
    static SourceList *m_sourceListPtr;

    /**
     * Connection to the Synthesis engine. Always valid in a
     * constructed SyncContext. Use getEngine() to reference
     * it.
     */
    SharedEngine m_engine;

    /**
     * Synthesis session handle. Only valid while sync is running.
     */
    SharedSession m_session;

    /**
     * installs session in SyncContext and removes it again
     * when going out of scope
     */
    class SessionSentinel {
        SyncContext &m_client;
    public:
        SessionSentinel(SyncContext &client, SharedSession &session) :
        m_client(client) {
            m_client.m_session = session;
        }
        ~SessionSentinel() {
            m_client.m_session.reset();
        }
    };

  public:
    /**
     * @param server     identifies the server config to be used
     * @param doLogging  write additional log and datatbase files about the sync
     */
    SyncContext(const string &server,
                        bool doLogging = false,
                        const set<string> &sources = set<string>());
    ~SyncContext();

    using SyncConfig::savePassword;

    bool getQuiet() { return m_quiet; }
    void setQuiet(bool quiet) { m_quiet = quiet; }

    bool getDryRun() { return m_dryrun; }
    void setDryRun(bool dryrun) { m_dryrun = dryrun; }

    static SuspendFlags& getSuspendFlags() {return s_flags;}

    /**
     * Executes the sync, throws an exception in case of failure.
     * Handles automatic backups and report generation.
     *
     * @retval complete sync report, skipped if NULL
     * @return overall sync status, for individual sources see report
     */
    SyncMLStatus sync(SyncReport *report = NULL);

    /**
     * Determines the log directory of the previous sync (either in
     * temp or logdir) and shows changes since then.
     */
    void status();

    enum RestoreDatabase {
        DATABASE_BEFORE_SYNC,
        DATABASE_AFTER_SYNC
    };

    /**
     * Restore data of selected sources from before or after the given
     * sync session, identified by absolute path to the log dir.
     */
    void restore(const string &dirname, RestoreDatabase database);

    /**
     * Sleep the sync session in interval seconds, blocks until the interval
     * is expired or a user suspend/abort request is signaled (CTRL+C)
     * returns time left to sleep if the method is interrupted by CTRL+C.
     */
    virtual int sleep (int interval);

    /**
     * fills vector with absolute path to information about previous
     * sync sessions, oldest one first
     */
    void getSessions(vector<string> &dirs);

    /**
     * fills report with information about previous session
     */
    void readSessionInfo(const string &dir, SyncReport &report);

    /**
     * fills report with information about local changes
     *
     * Only sync sources selected in the SyncContext
     * constructor are checked. The local item changes will be set in
     * the SyncReport's ITEM_LOCAL ITEM_ADDED/UPDATED/REMOVED.
     *
     * Some sync sources might not be able to report this
     * information outside of a regular sync, in which case
     * these fields are set to -1. 
     *
     * Start and end times of the check are also reported.
     */
    void checkStatus(SyncReport &report);

    /**
     * throws a runtime_error with the given string
     * or (on the iPhone, where exception handling is not
     * supported by the toolchain) prints an error directly
     * and aborts
     *
     * output format: <error>
     *
     * @param error     a string describing the error
     */
    static void throwError(const string &error);

    /**
     * throw an exception after an operation failed and
     * remember that this instance has failed
     *
     * output format: <action>: <error string>
     *
     * @Param action   a string describing the operation or object involved
     * @param error    the errno error code for the failure
     */
    static void throwError(const string &action, int error);

    /**
     * An error handler which prints the error message and then
     * stops the program. Never returns.
     *
     * The API was chosen so that it can be used as libebook/libecal
     * "backend-dies" signal handler.
     */
    static void fatalError(void *object, const char *error);

    /**
     * When using Evolution this function starts a background thread
     * which drives the default event loop. Without that loop
     * "backend-died" signals are not delivered. The problem with
     * the thread is that it seems to interfere with gconf startup
     * when added to the main() function of syncevolution. Therefore
     * it is started by SyncSource::beginSync() (for unit
     * testing of sync sources) and SyncContext::sync() (for
     * normal operation).
     */
    static void startLoopThread();

    /**
     * Finds activated sync source by name. May return  NULL
     * if no such sync source was defined or is not currently
     * instantiated. Pointer remains valid throughout the sync
     * session. Called by Synthesis DB plugin to find active
     * sources.
     *
     * @TODO: roll SourceList into SyncContext and
     * make this non-static
     */
    static SyncSource *findSource(const char *name);

    /**
     * intercept config filters
     *
     * This call removes the "sync" source property and remembers
     * it separately because it has to be applied to only the active
     * sync sources; the generic config handling code would apply
     * it to all sources.
     */
    virtual void setConfigFilter(bool sync, const FilterConfigNode::ConfigFilter &filter);

    SharedEngine getEngine() { return m_engine; }
    const SharedEngine getEngine() const { return m_engine; }

    bool getDoLogging() { return m_doLogging; }
    std::string getServer() { return m_server; }

    /**
     * Handle for active session, may be NULL.
     */
    SharedSession getSession() { return m_session; }

  protected:
    /** exchange active Synthesis engine */
    SharedEngine swapEngine(SharedEngine newengine) {
        SharedEngine oldengine = m_engine;
        m_engine = newengine;
        return oldengine;
    }

    /** sentinel class which creates, installs and removes a new
        Synthesis engine for the duration of its own life time */
    class SwapEngine {
        SyncContext &m_client;
        SharedEngine m_oldengine;

    public:
        SwapEngine(SyncContext &client) :
        m_client(client) {
            SharedEngine syncengine(m_client.createEngine());
            m_oldengine = m_client.swapEngine(syncengine);
        }

        ~SwapEngine() {
            m_client.swapEngine(m_oldengine);
        }
    };

    /**
     * Create a Synthesis engine for the currently active
     * sources (might be empty!) and settings.
     */
    SharedEngine createEngine();

    /**
     * Maps from source name to sync mode with one default
     * for all sources which don't have a specific entry
     * in the hash.
     */
    class SyncModes : public std::map<string, SyncMode> {
        SyncMode m_syncMode;

    public:
        SyncModes(SyncMode syncMode = SYNC_NONE) :
        m_syncMode(syncMode)
        {}

        SyncMode getDefaultSyncMode() { return m_syncMode; }
        void setDefaultMode(SyncMode syncMode) { m_syncMode = syncMode; }

        SyncMode getSyncMode(const string &sourceName) const {
            const_iterator it = find(sourceName);
            if (it == end()) {
                return m_syncMode;
            } else {
                return it->second;
            }
        }

        void setSyncMode(const string &sourceName, SyncMode syncMode) {
            (*this)[sourceName] = syncMode;
        }
    };

    /**
     * An utility function which can be used as part of
     * prepare() below to reconfigure the sync mode that
     * is going to be used for the active sync session.
     * SYNC_NONE as mode means that the sync mode of the
     * source is not modified and the default from the
     * configuration is used.
     */
    void setSyncModes(const std::vector<SyncSource *> &sources,
                      const SyncModes &modes);

    /**
     * Return skeleton Synthesis client XML configuration.
     *
     * If it contains a <datastore/> element, then that element will
     * be replaced by the configurations of all active sync
     * sources. Otherwise the configuration is used as-is.
     *
     * The default implementation of this function takes the configuration from
     * (in this order):
     * - ./syncevolution.xml
     * - <server config dir>/syncevolution.xml
     * - built-in default
     *
     * @retval xml         is filled with Synthesis client config which may hav <datastore/>
     * @retval configname  a string describing where the config came from
     */
    virtual void getConfigTemplateXML(string &xml, string &configname);

    /**
     * Return complete Synthesis XML configuration.
     *
     * Calls getConfigTemplateXML(), then fills in
     * sync source XML fragments if necessary.
     *
     * @retval xml         is filled with complete Synthesis client config
     * @retval configname  a string describing where the config came from
     */
    virtual void getConfigXML(string &xml, string &configname);

    /**
     * A helper function which interactively asks the user for
     * a certain password. May throw errors.
     *
     * The default implementation uses stdin/stdout to communicate
     * with the user.
     *
     * @param passwordName the name of the password in the config file
     * @param descr        A simple string explaining what the password is needed for,
     *                     e.g. "SyncML server". Has to be unique and understandable
     *                     by the user.
     * @param key          the key used to retrieve password
     * @return entered password
     */
    virtual string askPassword(const string &passwordName, const string &descr, const ConfigPasswordKey &key);

    /**
     * A helper function which is used for user interface to save
     * a certain password with a specific mechanism. 
     * Currently possibly syncml server. May throw errors.
     * The default implementation do nothing.
     */
    virtual bool savePassword(const string &passwordName, const string &password, const ConfigPasswordKey &key) { 
        return false; 
    }

    /**
     * Callback for derived classes: called after initializing the
     * client, but before doing anything with its configuration.
     * Can be used to override the client configuration.
     */
    virtual void prepare() {}

    /**
     * Callback for derived classes: called after setting up the client's
     * and sources' configuration. Can be used to reconfigure sources before
     * actually starting the synchronization.
     *
     * @param sources   a NULL terminated array of all active sources
     */
    virtual void prepare(const std::vector<SyncSource *> &sources) {}

    /**
     * instantiate transport agent
     *
     * Called by engine when it needs to do HTTP POST requests.  The
     * transport agent will be used throughout the sync session and
     * unref'ed when no longer needed. At most one agent will be
     * requested at a time. The transport agent is intentionally
     * returned as a Boost shared pointer so that a pointer to a
     * class with a different life cycle is possible, either by
     * keeping a reference or by returning a shared_ptr where the
     * destructor doesn't do anything.
     *
     * The default implementation instantiates one of the builtin
     * transport agents, depending on how it was compiled.
     *
     * @return transport agent
     */
    virtual boost::shared_ptr<TransportAgent> createTransportAgent();

    /**
     * display a text message from the server
     *
     * Not really used by SyncML servers. Could be displayed in a
     * modal dialog.
     *
     * @param message     string with local encoding, possibly with line breaks
     */
    virtual void displayServerMessage(const string &message);

    /**
     * display general sync session progress
     *
     * @param type    PEV_*, see <synthesis/engine_defs.h>
     * @param extra1  extra information depending on type
     * @param extra2  extra information depending on type
     * @param extra3  extra information depending on type
     */
    virtual void displaySyncProgress(sysync::TProgressEventEnum type,
                                     int32_t extra1, int32_t extra2, int32_t extra3);

    /**
     * display sync source specific progress
     *
     * @param type    PEV_*, see <synthesis/engine_defs.h>
     * @param source  source which is the target of the event
     * @param extra1  extra information depending on type
     * @param extra2  extra information depending on type
     * @param extra3  extra information depending on type
     */
    virtual void displaySourceProgress(sysync::TProgressEventEnum type,
                                       SyncSource &source,
                                       int32_t extra1, int32_t extra2, int32_t extra3);

    /**
     * Called to find out whether user wants to abort sync.
     *
     * Will be called regularly. Once it has flagged an abort, all
     * following calls should return the same value. When the engine
     * aborts, the sync is shut down as soon as possible.  The next
     * sync most likely has to be done in slow mode, so don't do this
     * unless absolutely necessary.
     *
     * @return true if user wants to abort
     */
    virtual bool checkForAbort() { return (s_flags.state == SuspendFlags::CLIENT_ABORT);}

    /**
     * Called to find out whether user wants to suspend sync.
     *
     * Same as checkForAbort(), but the session is finished
     * gracefully so that it can be resumed.
     */
    virtual bool checkForSuspend() { return (s_flags.state == SuspendFlags::CLIENT_SUSPEND);}

 private:
    /**
     * the code common to init() and status():
     * populate source list with active sources and open
     */
    void initSources(SourceList &sourceList);

    /**
     * utility function for status() and getChanges():
     * iterate over sources, check for changes and copy result
     */
    void checkSourceChanges(SourceList &sourceList, SyncReport &changes);

    /**
     * sets up Synthesis session and executes it
     */
    SyncMLStatus doSync();

    /**
     * iterate over files mentioned in getSSLServerCertificates()
     * and return name of first one which is found, empty string
     * if none
     */
    std::string findSSLServerCertificate();

    /**
     * override sync mode of all active sync sources if set
     */
    string m_overrideMode;

    // total retry duration
    int m_retryDuration;
    // message resend interval
    int m_retryInterval;
    // Current retry count
    int m_retries;

public:
    static bool transport_cb (void *data);
    bool processTransportCb();
};

SE_END_CXX
#endif // INCL_EVOLUTIONSYNCCLIENT
