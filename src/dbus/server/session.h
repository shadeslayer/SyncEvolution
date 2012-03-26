/*
 * Copyright (C) 2011 Intel Corporation
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

#ifndef SESSION_H
#define SESSION_H

#include <syncevo/SynthesisEngine.h>
#include <boost/weak_ptr.hpp>
#include <boost/utility.hpp>

#include <gdbus-cxx-bridge.h>

#include <syncevo/SuspendFlags.h>

#include "session-common.h"
#include "read-operations.h"
#include "progress-data.h"
#include "source-progress.h"
#include "source-status.h"
#include "timer.h"
#include "timeout.h"
#include "resource.h"
#include "dbus-callbacks.h"

SE_BEGIN_CXX

class Connection;
class Server;
class ForkExecParent;
class SessionProxy;
class InfoReq;

/**
 * Represents and implements the Session interface.  Use
 * boost::shared_ptr to track it and ensure that there are references
 * to it as long as the connection is needed.
 *
 * The actual implementation is split into two parts:
 * - state as exposed via D-Bus is handled entirely in this class
 * - syncing and command line execution run inside
 *   the forked syncevo-dbus-helper
 *
 * This allows creating and tracking a Session locally in
 * syncevo-dbus-server and minimizes asynchronous calls into the
 * helper. The helper is started on demand (which might be never,
 * for simple sessions).
 */
class Session : public GDBusCXX::DBusObjectHelper,
                public Resource,
                private ReadOperations,
                private boost::noncopyable
{
    Server &m_server;
    std::vector<std::string> m_flags;
    const std::string m_sessionID;
    std::string m_peerDeviceID;

    /** Starts the helper, on demand (see useHelperAsync()). */
    boost::shared_ptr<ForkExecParent> m_forkExecParent;
    /** The D-Bus proxy for the helper. */
    boost::shared_ptr<SessionProxy> m_helper;

    /**
     * Ensures that helper is running and that its D-Bus API is
     * available via m_helper, then invokes the success
     * callback. Startup errors are reported back via the error
     * callback. It is the responsibility of that error callback to
     * turn the session into the right failure state, usually via
     * Session::failed(). Likewise, any unexpected failures or helper
     * shutdowns need to be monitored by the caller of
     * useHelperAsync(). useHelperAsync() merely logs these events.
     *
     * useHelperAsync() and its helper function, useHelper2(), are the
     * ones called directly from the main event loop. They ensure that
     * any exceptions thrown inside them, including exceptions thrown
     * by the result.done(), are logged and turned into
     * result.failed() calls.
     *
     * In practice, the helper is started at most once per session, to
     * run the operation (see runOperation()). When it terminates, the
     * session is either considered "done" or "failed", depending on
     * whether the operation has completed already.
     */
    void useHelperAsync(const SimpleResult &result);

    /**
     * Finish the work started by useHelperAsync once helper has
     * connected. The operation might still fail at this point.
     */
    void useHelper2(const SimpleResult &result, const boost::signals2::connection &c);

    /** set up m_helper */
    void onConnect(const GDBusCXX::DBusConnectionPtr &conn) throw ();
    /** unset m_helper but not m_forkExecParent (still processing signals) */
    void onQuit(int result) throw ();
    /** set after abort() and suspend(), to turn "child died' into the LOCERR_USERABORT status code */
    void expectChildTerm(int result) throw ();
    /** log failure */
    void onFailure(SyncMLStatus status, const std::string &explanation) throw ();
    /** log error output from helper */
    void onOutput(const char *buffer, size_t length);

    bool m_serverMode;
    bool m_serverAlerted;
    SharedBuffer m_initialMessage;
    string m_initialMessageType;

    boost::weak_ptr<Connection> m_connection;
    std::string m_connectionError;
    bool m_useConnection;

    /** temporary config changes */
    FilterConfigNode::ConfigFilter m_syncFilter;
    FilterConfigNode::ConfigFilter m_sourceFilter;
    SessionCommon::SourceFilters_t m_sourceFilters;

    /** whether dbus clients set temporary configs */
    bool m_tempConfig;

    /**
     * whether the dbus clients updated, removed or cleared configs,
     * ignoring temporary configuration changes
     */
    bool m_setConfig;

    /** Session life cycle */
    enum SessionStatus {
        SESSION_IDLE,         /**< not active yet, only Detach() allowed */
        SESSION_ACTIVE,       /**< active, config changes and Sync()/Execute() allowed */
        SESSION_RUNNING,      /**< one-time operation (Sync() or Execute()) in progress */
        SESSION_DONE          /**< operation completed, only Detach() still allowed */
    };
    SessionStatus m_status;

    /**
     * set when operation was aborted, enables special handling of "child quit" in onQuit().
     */
    bool m_wasAborted;

    /**
     * Indicates whether this session was initiated by the peer or locally.
     */
    bool m_remoteInitiated;

    /**
     * the sync status for session
     */
    enum SyncStatus {
        SYNC_QUEUEING,  ///< waiting to become ready for use
        SYNC_IDLE,      ///< ready, session is initiated but sync not started
        SYNC_RUNNING,   ///< sync is running
        SYNC_ABORT,     ///< sync is aborting
        SYNC_SUSPEND,   ///< sync is suspending
        SYNC_DONE,      ///< sync is done
        SYNC_ILLEGAL
    } m_syncStatus;

    /** maps to names as used in D-Bus API */
    inline std::string static syncStatusToString(SyncStatus state)
    {
        static const char * const strings[SYNC_ILLEGAL] = {
            "queueing",
            "idle",
            "running",
            "aborting",
            "suspending",
            "done"
        };
        return state >= SYNC_QUEUEING && state < SYNC_ILLEGAL ?
            strings[state] :
            "";
    }

    /** step info: whether engine is waiting for something */
    bool m_stepIsWaiting;

    /**
     * Priority which determines position in queue.
     * Lower is more important. PRI_DEFAULT is zero.
     */
    int m_priority;

    int32_t m_progress;

    /** progress data, holding progress calculation related info */
    ProgressData m_progData;

    typedef std::map<std::string, SourceStatus> SourceStatuses_t;
    SourceStatuses_t m_sourceStatus;

    uint32_t m_error;
    typedef std::map<std::string, SourceProgress> SourceProgresses_t;
    SourceProgresses_t m_sourceProgress;

    // syncProgress() and sourceProgress() turn raw data from helper
    // into usable information on D-Bus server side
    void syncProgress(sysync::TProgressEventEnum type,
                      int32_t extra1, int32_t extra2, int32_t extra3);
    void sourceProgress(sysync::TProgressEventEnum type,
                        const std::string &sourceName,
                        SyncMode sourceSyncMode,
                        int32_t extra1, int32_t extra2, int32_t extra3);

    /** timer for fire status/progress usages */
    Timer m_statusTimer;
    Timer m_progressTimer;

    /** the total number of sources to be restored */
    int m_restoreSrcTotal;
    /** the number of sources that have been restored */
    int m_restoreSrcEnd;

    /**
     * Wrapper around useHelperAsync() which sets up the session
     * to execute a specific operation (sync, command line, ...).
     */
    void runOperationAsync(SessionCommon::RunOperation op,
                           const SuccessCb_t &helperReady);

    /**
     * A Session can be used for exactly one of the operations.  This
     * is the one. This gets set by the D-Bus method implementation
     * which triggers the operation. All other D-Bus method
     * implementations need to check it before allowing an operation
     * or method call which would conflict or be illegal.
     */
    SessionCommon::RunOperation m_runOperation;

    /**
     * If m_runOperation == OP_CMDLINE, then we need further information
     * from the helper about the actual operation. We get that information
     * via a sync progress signal with event == PEV_CUSTOM_START.
     */
    SessionCommon::RunOperation m_cmdlineOp;

    /** Session.Attach() */
    void attach(const GDBusCXX::Caller_t &caller);

    /** Session.Detach() */
    void detach(const GDBusCXX::Caller_t &caller);

    /** Session.GetStatus() */
    void getStatus(std::string &status,
                   uint32_t &error,
                   SourceStatuses_t &sources);
    /** Session.GetProgress() */
    void getProgress(int32_t &progress,
                     SourceProgresses_t &sources);

    /** Session.Restore() */
    void restore(const string &dir, bool before, const std::vector<std::string> &sources);
    void restore2(const string &dir, bool before, const std::vector<std::string> &sources);

    /** Session.checkPresence() */
    void checkPresence (string &status);

    /** Session.Execute() */
    void execute(const vector<string> &args, const map<string, string> &vars);
    void execute2(const vector<string> &args, const map<string, string> &vars);

    /**
     * Must be called each time that properties changing the
     * overall status are changed (m_syncStatus, m_error, m_sourceStatus).
     * Ensures that the corresponding D-Bus signal is sent.
     *
     * Doesn't always send the signal immediately, because often it is
     * likely that more status changes will follow shortly. To ensure
     * that the "final" status is sent, call with flush=true.
     *
     * @param flush      force sending the current status
     */
    void fireStatus(bool flush = false);
    /** like fireStatus() for progress information */
    void fireProgress(bool flush = false);

    /** Session.StatusChanged */
    GDBusCXX::EmitSignal3<const std::string &,
                          uint32_t,
                          const SourceStatuses_t &> emitStatus;
    /** Session.ProgressChanged */
    GDBusCXX::EmitSignal2<int32_t,
                          const SourceProgresses_t &> emitProgress;

public:
    /**
     * Sessions must always be held in a shared pointer
     * because some operations depend on that. This
     * constructor function here ensures that and
     * also adds a weak pointer to the instance itself,
     * so that it can create more shared pointers as
     * needed.
     */
    static boost::shared_ptr<Session> createSession(Server &server,
                                                    const std::string &peerDeviceID,
                                                    const std::string &config_name,
                                                    const std::string &session,
                                                    const std::vector<std::string> &flags = std::vector<std::string>());

    /**
     * automatically marks the session as completed before deleting it
     */
    ~Session();

    /**
     * explicitly mark an idle session as completed, even if it doesn't
     * get deleted yet (exceptions not expected by caller)
     */
    void done() throw () { doneCb(); }

private:
    Session(Server &server,
            const std::string &peerDeviceID,
            const std::string &config_name,
            const std::string &session,
            const std::vector<std::string> &flags = std::vector<std::string>());
    boost::weak_ptr<Session> m_me;
    boost::shared_ptr<InfoReq> m_passwordRequest;
    void passwordRequest(const std::string &descr, const ConfigPasswordKey &key);
    void sendViaConnection(const GDBusCXX::DBusArray<uint8_t> buffer,
                           const std::string &type,
                           const std::string &url);
    void shutdownConnection();
    void storeMessage(const GDBusCXX::DBusArray<uint8_t> &message,
                      const std::string &type);
    void connectionState(const std::string &error);

public:
    enum {
        PRI_CMDLINE = -10,
        PRI_DEFAULT = 0,
        PRI_CONNECTION = 10,
        PRI_AUTOSYNC = 20
    };

    /**
     * Default priority is 0. Higher means less important.
     */
    void setPriority(int priority) { m_priority = priority; }
    int getPriority() const { return m_priority; }

    bool isServerAlerted() const { return m_serverAlerted; }
    void setServerAlerted(bool serverAlerted) { m_serverAlerted = serverAlerted; }

    void initServer(SharedBuffer data, const std::string &messageType);
    void setStubConnection(const boost::shared_ptr<Connection> c) { m_connection = c; m_useConnection = c; }
    boost::weak_ptr<Connection> getStubConnection() { return m_connection; }
    bool useStubConnection() { return m_useConnection; }

    /**
     * After the connection closes, the Connection instance is
     * destructed immediately. This is necessary so that the
     * corresponding cleanup can remove all other classes
     * only referenced by the Connection.
     *
     * This leads to the problem that an active sync cannot
     * query the final error code of the connection. This
     * is solved by setting a generic error code here when
     * the sync starts and overwriting it when the connection
     * closes.
     */
    void setStubConnectionError(const std::string error) { m_connectionError = error; }
    std::string getStubConnectionError() { return m_connectionError; }

    Server &getServer() { return m_server; }
    std::string getConfigName() { return m_configName; }
    std::string getSessionID() const { return m_sessionID; }
    std::string getPeerDeviceID() const { return m_peerDeviceID; }

    /** Session.GetFlags() */
    std::vector<std::string> getFlags() { return m_flags; }

    /** Session.GetConfigName() */
    std::string getNormalConfigName() { return SyncConfig::normalizeConfigString(m_configName); }

    /** Session.SetConfig() */
    void setConfig(bool update, bool temporary,
                   const ReadOperations::Config_t &config);

    /** Session.SetNamedConfig() */
    void setNamedConfig(const std::string &configName,
                        bool update, bool temporary,
                        const ReadOperations::Config_t &config);

    /** Session.Sync() */
    void sync(const std::string &mode, const SessionCommon::SourceModes_t &sourceModes);

    /**
     * finish the work started by sync once helper is ready (invoked
     * by useHelperAsync() and thus may throw exceptions)
     */
    void sync2(const std::string &mode, const SessionCommon::SourceModes_t &sourceModes);

    /** Session.Abort() */
    void abort();

    /** abort active session, trigger result once done */
    void abortAsync(const SimpleResult &result);

    /** Session.Suspend() */
    void suspend();

     /**
     * step info for engine: whether the engine is blocked by something
     * If yes, 'waiting' will be appended as specifiers in the status string.
     * see GetStatus documentation.
     */
    void setWaiting(bool isWaiting);

    /** session was just activated */
    typedef boost::signals2::signal<void ()> SessionActiveSignal_t;
    SessionActiveSignal_t m_sessionActiveSignal;

    /** sync is successfully started */
    typedef boost::signals2::signal<void ()> SyncSuccessStartSignal_t;
    SyncSuccessStartSignal_t m_syncSuccessStartSignal;

    /** sync completed (may have failed) */
    typedef boost::signals2::signal<void (SyncMLStatus)> DoneSignal_t;
    DoneSignal_t m_doneSignal;

    /**
     * Called by server when the session is ready to run.
     * Only the session itself can deactivate itself.
     */
    void activateSession();

    /**
     * Called by server when it has a password response for the
     * session. The session ensures that it only has one pending
     * request at a time, so these parameters are enough to identify
     * the request.
     */
    void passwordResponse(bool timedOut, bool aborted, const std::string &password);

    void setRemoteInitiated (bool remote) { m_remoteInitiated = remote;}
private:
    /** set m_syncFilter and m_sourceFilters to config */
    virtual bool setFilters(SyncConfig &config);

    void dbusResultCb(const std::string &operation, bool success, const std::string &error) throw();

    /**
     * to be called inside a catch() clause: returns error for any
     * pending D-Bus method and then calls doneCb()
     */
    void failureCb() throw();

    /**
     * explicitly mark the session as completed, even if it doesn't
     * get deleted yet (invoked directly or indirectly from event
     * loop and thus must not throw exceptions)
     *
     * @param success    if false, then ensure that m_error is set
     *                   before finalizing the session
     */
    void doneCb(bool success = true) throw();
};

SE_END_CXX

#endif // SESSION_H
