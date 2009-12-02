/*
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


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <syncevo/Logging.h>
#include <syncevo/util.h>
#include <syncevo/SyncContext.h>
#include <syncevo/SoupTransportAgent.h>
#include <syncevo/SyncSource.h>
#include <syncevo/SyncML.h>
#include <syncevo/FileConfigNode.h>

#include <synthesis/san.h>

#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <sys/time.h>

#include <list>
#include <map>
#include <memory>
#include <iostream>
#include <limits>
#include <cmath>
#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/noncopyable.hpp>

#include <glib-object.h>

class DBusMessage;
static DBusMessage *SyncEvoHandleException(DBusMessage *msg);
#define DBUS_CXX_EXCEPTION_HANDLER SyncEvoHandleException
#include "gdbus-cxx-bridge.h"

using namespace SyncEvo;

static GMainLoop *loop = NULL;
static bool shutdownRequested = false;

/**
 * Anything that can be owned by a client, like a connection
 * or session.
 */
class Resource {
public:
    virtual ~Resource() {}
};

class Session;
class Connection;
class Client;
class DBusTransportAgent;

class DBusSyncException : public DBusCXXException, public Exception
{
 public:
    DBusSyncException(const std::string &file,
                  int line,
                  const std::string &what) : Exception(file, line, what)
    {}
    /**
     * get exception name, used to convert to dbus error name
     * subclasses should override it
     */
    virtual std::string getName() const { return "org.syncevolution.Exception"; }

    virtual const char* getMessage() const { return Exception::what(); }
};

/**
 * exceptions classes deriving from DBusException
 * org.syncevolution.NoSuchConfig 
 */
class NoSuchConfig: public DBusSyncException
{
 public:
    NoSuchConfig(const std::string &file,
                 int line,
                 const std::string &error): DBusSyncException(file, line, error)
    {}
    virtual std::string getName() const { return "org.syncevolution.NoSuchConfig";}
};

/**
 * org.syncevolution.NoSuchSource 
 */
class NoSuchSource : public DBusSyncException
{
 public:
    NoSuchSource(const std::string &file,
                 int line,
                 const std::string &error): DBusSyncException(file, line, error)
    {}
    virtual std::string getName() const { return "org.syncevolution.NoSuchSource";}
};

/**
 * org.syncevolution.InvalidCall 
 */
class InvalidCall : public DBusSyncException
{
 public:
    InvalidCall(const std::string &file,
                 int line,
                 const std::string &error): DBusSyncException(file, line, error)
    {}
    virtual std::string getName() const { return "org.syncevolution.InvalidCall";}
};

/**
 * org.syncevolution.SourceUnusable
 * CheckSource will use this when the source cannot be used for whatever reason 
 */
class SourceUnusable : public DBusSyncException
{
 public:
    SourceUnusable(const std::string &file,
                   int line,
                   const std::string &error): DBusSyncException(file, line, error)
    {}
    virtual std::string getName() const { return "org.syncevolution.SourceUnusable";}
};

/**
 * implement syncevolution exception handler
 * to cover its default implementation
 */
static DBusMessage* SyncEvoHandleException(DBusMessage *msg)
{
    /** give an opportunity to let syncevolution handle exception */
    Exception::handle();
    try {
        throw;
    } catch (const dbus_error &ex) {
        return g_dbus_create_error(msg, ex.dbusName().c_str(), "%s", ex.what());
    } catch (const DBusCXXException &ex) {
        return g_dbus_create_error(msg, ex.getName().c_str(), "%s", ex.getMessage());
    } catch (const std::runtime_error &ex) {
        return g_dbus_create_error(msg, "org.syncevolution.Exception", "%s", ex.what());
    } catch (...) {
        return g_dbus_create_error(msg, "org.syncevolution.Exception", "unknown");
    }
}

/**
 * Implements the read-only methods in a Session and the Server.
 * Only data is the server configuration name, everything else
 * is created and destroyed inside the methods.
 */
class ReadOperations
{
public:
    const std::string m_configName;

    ReadOperations(const std::string &config_name);

    /** the double dictionary used to represent configurations */
    typedef std::map< std::string, StringMap > Config_t;

    /** the array of reports filled by getReports() */
    typedef std::vector< StringMap > Reports_t;

    /** the array of databases used by getDatabases() */
    typedef SyncSource::Database SourceDatabase;
    typedef SyncSource::Databases SourceDatabases_t;

    /** implementation of D-Bus GetConfigs() */
    static void getConfigs(bool getTemplates, std::vector<std::string> &configNames);

    /** implementation of D-Bus GetConfig() for m_configName as server configuration */
    void getConfig(bool getTemplate,
                   Config_t &config);

    /** implementation of D-Bus GetReports() for m_configName as server configuration */
    void getReports(uint32_t start, uint32_t count,
                    Reports_t &reports);

    /** Session.CheckSource() */
    void checkSource(const string &sourceName);

    /** Session.GetDatabases() */
    void getDatabases(const string &sourceName, SourceDatabases_t &databases);

private:
    /** 
     * This virtual function is used to let subclass set
     * filters to config. Only used internally.
     */
    virtual void setFilters(SyncConfig &config) {}

    /** utility method which constructs a SyncConfig which references a local configuration (never a template) */
    boost::shared_ptr<SyncConfig> getLocalConfig(const std::string &configName);
};

/**
 * dbus_traits for SourceDatabase. Put it here for 
 * avoiding polluting gxx-dbus-bridge.h
 */
template<> struct dbus_traits<ReadOperations::SourceDatabase> :
    public dbus_struct_traits<ReadOperations::SourceDatabase,
                              dbus_member<ReadOperations::SourceDatabase, std::string, &ReadOperations::SourceDatabase::m_name,
                              dbus_member<ReadOperations::SourceDatabase, std::string, &ReadOperations::SourceDatabase::m_uri,
                              dbus_member_single<ReadOperations::SourceDatabase, bool, &ReadOperations::SourceDatabase::m_isDefault> > > >{}; 


/**
 * Implements the main org.syncevolution.Server interface.
 *
 * All objects created by it get a reference to the creating
 * DBusServer instance so that they can call some of its
 * methods. Because that instance holds references to all
 * of these objects and deletes them before destructing itself,
 * that reference is guaranteed to remain valid.
 */
class DBusServer : public DBusObjectHelper
{
    GMainLoop *m_loop;
    uint32_t m_lastSession;
    typedef std::list< std::pair< boost::shared_ptr<Watch>, boost::shared_ptr<Client> > > Clients_t;
    Clients_t m_clients;

    /**
     * The session which currently holds the main lock on the server.
     * To avoid issues with concurrent modification of data or configs,
     * only one session may make such modifications at a time. A
     * plain pointer which is reset by the session's deconstructor.
     *
     * A weak pointer alone did not work because it does not provide access
     * to the underlying pointer after the last corresponding shared
     * pointer is gone (which triggers the deconstructing of the session).
     */
    Session *m_activeSession;

    /**
     * The weak pointer that corresponds to m_activeSession.
     */
    boost::weak_ptr<Session> m_activeSessionRef;

    /**
     * The running sync session. Having a separate reference to it
     * ensures that the object won't go away prematurely, even if all
     * clients disconnect.
     */
    boost::shared_ptr<Session> m_syncSession;

    typedef std::list< boost::weak_ptr<Session> > WorkQueue_t;
    /**
     * A queue of pending, idle Sessions. Sorted by priority, most
     * important one first. Currently this is used to give client
     * requests a boost over remote connections and (in the future)
     * automatic syncs.
     *
     * Active sessions are removed from this list and then continue
     * to exist as long as a client in m_clients references it or
     * it is the currently running sync session (m_syncSession).
     */
    WorkQueue_t m_workQueue;

    /**
     * Watch callback for a specific client or connection.
     */
    void clientGone(Client *c);

    /**
     * Returns new unique session ID. Implemented with a running
     * counter. Checks for overflow, but not currently for active
     * sessions.
     */
    std::string getNextSession();

    /** Server.Attach() */
    void attachClient(const Caller_t &caller,
                      const boost::shared_ptr<Watch> &watch);

    /** Server.Detach() */
    void detachClient(const Caller_t &caller);

    /** Server.Connect() */
    void connect(const Caller_t &caller,
                 const boost::shared_ptr<Watch> &watch,
                 const StringMap &peer,
                 bool must_authenticate,
                 const std::string &session,
                 DBusObject_t &object);

    /** Server.StartSession() */
    void startSession(const Caller_t &caller,
                      const boost::shared_ptr<Watch> &watch,
                      const std::string &server,
                      DBusObject_t &object);

    /** Server.GetConfig() */
    void getConfig(const std::string &config_name,
                   bool getTemplate,
                   ReadOperations::Config_t &config)
    {
        ReadOperations ops(config_name);
        ops.getConfig(getTemplate , config);
    }

    /** Server.GetReports() */
    void getReports(const std::string &config_name,
                    uint32_t start, uint32_t count,
                    ReadOperations::Reports_t &reports)
    {
        ReadOperations ops(config_name);
        ops.getReports(start, count, reports);
    }

    /** Server.CheckSource() */
    void checkSource(const std::string &configName,
                     const std::string &sourceName)
    {
        ReadOperations ops(configName);
        ops.checkSource(sourceName);
    }

    /** Server.GetDatabases() */
    void getDatabases(const std::string &configName,
                      const string &sourceName,
                      ReadOperations::SourceDatabases_t &databases)
    {
        ReadOperations ops(configName);
        ops.getDatabases(sourceName, databases);
    }

    /** Server.CheckPresence() */
    void checkPresence(const std::string &server,
                       std::string &status,
                       std::vector<std::string> &transports);

    /** Server.GetSessions() */
    void getSessions(std::vector<std::string> &sessions);

    /** Server.SessionChanged */
    EmitSignal2<const DBusObject_t &,
                bool> sessionChanged;

    /** Server.PresenceChanged */
    EmitSignal3<const std::string &,
                const std::string &,
                const std::string &> presence;

public:
    DBusServer(GMainLoop *loop, const DBusConnectionPtr &conn);
    ~DBusServer();

    /** access to the GMainLoop reference used by this DBusServer instance */
    GMainLoop *getLoop() { return m_loop; }

    /** process D-Bus calls until the server is ready to quit */
    void run();

    /**
     * look up client by its ID
     */
    boost::shared_ptr<Client> findClient(const Caller_t &ID);

    /**
     * find client by its ID or create one anew
     */
    boost::shared_ptr<Client> addClient(const DBusConnectionPtr &conn,
                                        const Caller_t &ID,
                                        const boost::shared_ptr<Watch> &watch);

    /** detach this resource from all clients which own it */
    void detach(Resource *resource);

    /**
     * Enqueue a session. Might also make it ready immediately,
     * if nothing else is first in the queue. To be called
     * by the creator of the session, *after* the session is
     * ready to run.
     */
    void enqueue(const boost::shared_ptr<Session> &session);

    /**
     * Remove all sessions with this device ID from the
     * queue. If the active session also has this ID,
     * the session will be aborted and/or deactivated.
     */
    int killSessions(const std::string &peerDeviceID);

    /**
     * Remove a session from the work queue. If it is running a sync,
     * it will keep running and nothing will change. Otherwise, if it
     * is "ready" (= holds a lock on its configuration), then release
     * that lock.
     */
    void dequeue(Session *session);

    /**
     * Checks whether the server is ready to run another session
     * and if so, activates the first one in the queue.
     */
    void checkQueue();
};


/**
 * Tracks a single client and all sessions and connections that it is
 * connected to. Referencing them ensures that they stay around as
 * long as needed.
 */
class Client
{
    typedef std::list< boost::shared_ptr<Resource> > Resources_t;
    Resources_t m_resources;

public:
    const Caller_t m_ID;

    Client(const Caller_t &ID) :
        m_ID(ID)
    {}

    ~Client()
    {
        SE_LOG_DEBUG(NULL, NULL, "D-Bus client %s is destructing", m_ID.c_str());
    }
        

    /**
     * Attach a specific resource to this client. As long as the
     * resource is attached, it cannot be freed. Can be called
     * multiple times, which means that detach() also has to be called
     * the same number of times to finally detach the resource.
     */
    void attach(boost::shared_ptr<Resource> resource)
    {
        m_resources.push_back(resource);
    }

    /**
     * Detach once from the given resource. Has to be called as
     * often as attach() to really remove all references to the
     * session. It's an error to call detach() more often than
     * attach().
     */
    void detach(Resource *resource)
    {
        for (Resources_t::iterator it = m_resources.begin();
             it != m_resources.end();
             ++it) {
            if (it->get() == resource) {
                // got it
                m_resources.erase(it);
                return;
            }
        }

        SE_THROW_EXCEPTION(InvalidCall, "cannot detach from resource that client is not attached to");
    }
    void detach(boost::shared_ptr<Resource> resource)
    {
        detach(resource.get());
    }

    /**
     * Remove all references to the given resource, regardless whether
     * it was referenced not at all or multiple times.
     */
    void detachAll(Resource *resource) {
        Resources_t::iterator it = m_resources.begin();
        while (it != m_resources.end()) {
            if (it->get() == resource) {
                it = m_resources.erase(it);
            } else {
                ++it;
            }
        }
    }
    void detachAll(boost::shared_ptr<Resource> resource)
    {
        detachAll(resource.get());
    }

    /**
     * return corresponding smart pointer for a certain resource,
     * empty pointer if not found
     */
    boost::shared_ptr<Resource> findResource(Resource *resource)
    {
        for (Resources_t::iterator it = m_resources.begin();
             it != m_resources.end();
             ++it) {
            if (it->get() == resource) {
                // got it
                return *it;
            }
        }
        return boost::shared_ptr<Resource>();
    }
};

struct SourceStatus
{
    SourceStatus() :
        m_mode("none"),
        m_status("idle"),
        m_error(0)
    {}
    void set(const std::string &mode, const std::string &status, uint32_t error)
    {
        m_mode = mode;
        m_status = status;
        m_error = error;
    }

    std::string m_mode;
    std::string m_status;
    uint32_t m_error;
};

template<> struct dbus_traits<SourceStatus> :
    public dbus_struct_traits<SourceStatus,
                              dbus_member<SourceStatus, std::string, &SourceStatus::m_mode,
                              dbus_member<SourceStatus, std::string, &SourceStatus::m_status,
                              dbus_member_single<SourceStatus, uint32_t, &SourceStatus::m_error> > > >
{};

struct SourceProgress
{
    SourceProgress() :
        m_phase(""),
        m_prepareCount(-1), m_prepareTotal(-1),
        m_sendCount(-1), m_sendTotal(-1),
        m_receiveCount(-1), m_receiveTotal(-1)
    {}

    std::string m_phase;
    int32_t m_prepareCount, m_prepareTotal;
    int32_t m_sendCount, m_sendTotal;
    int32_t m_receiveCount, m_receiveTotal;
};

template<> struct dbus_traits<SourceProgress> :
    public dbus_struct_traits<SourceProgress,
                              dbus_member<SourceProgress, std::string, &SourceProgress::m_phase,
                              dbus_member<SourceProgress, int32_t, &SourceProgress::m_prepareCount,
                              dbus_member<SourceProgress, int32_t, &SourceProgress::m_prepareTotal,
                              dbus_member<SourceProgress, int32_t, &SourceProgress::m_sendCount,
                              dbus_member<SourceProgress, int32_t, &SourceProgress::m_sendTotal,
                              dbus_member<SourceProgress, int32_t, &SourceProgress::m_receiveCount,
                              dbus_member_single<SourceProgress, int32_t, &SourceProgress::m_receiveTotal> > > > > > > >
{};

/**
 * A running sync engine which keeps answering on D-Bus whenever
 * possible and updates the Session while the sync runs.
 */
class DBusSync : public SyncContext
{
    Session &m_session;

public:
    DBusSync(const std::string &config,
             Session &session);
    ~DBusSync() {}

protected:
    virtual boost::shared_ptr<TransportAgent> createTransportAgent();
    virtual void displaySyncProgress(sysync::TProgressEventEnum type,
                                     int32_t extra1, int32_t extra2, int32_t extra3);
    virtual void displaySourceProgress(sysync::TProgressEventEnum type,
                                       SyncSource &source,
                                       int32_t extra1, int32_t extra2, int32_t extra3);

    /**
     * Implement checkForSuspend and checkForAbort.
     * They will check whether dbus clients suspend
     * or abort the session in addition to checking
     * whether suspend/abort were requested via
     * signals, using SyncContext's signal handling.
     */
    virtual bool checkForSuspend(); 
    virtual bool checkForAbort();
    virtual int sleep(int intervals);
};

/**
 * A timer helper to check whether now is timeout according to
 * user's setting. Timeout is calculated in milliseconds
 */ 
class Timer {
    timeval m_startTime;  ///< start time
    unsigned long m_timeoutMs; ///< timeout in milliseconds, set by user

    /**
     * calculate duration between now and start time
     * return value is in milliseconds
     */
    unsigned long duration(const timeval &minuend, const timeval &subtrahend)
    {
        unsigned long result = 0;
        if(minuend.tv_sec > subtrahend.tv_sec || 
                (minuend.tv_sec == subtrahend.tv_sec && minuend.tv_usec > subtrahend.tv_usec)) {
            result = minuend.tv_sec - subtrahend.tv_sec;
            result *= 1000;
            result += (minuend.tv_usec - subtrahend.tv_usec) / 1000;
        }
        return result;
    }

 public:
    /**
     * constructor
     * @param timeoutMs timeout in milliseconds
     */
    Timer(unsigned long timeoutMs) : m_timeoutMs(timeoutMs)
    {
        reset();
    }

    /**
     * reset the timer and mark start time as current time
     */
    void reset() { gettimeofday(&m_startTime, NULL); }

    /**
     * check whether it is timeout
     */
    bool timeout() 
    {
        timeval now;
        gettimeofday(&now, NULL);
        return duration(now, m_startTime) >= m_timeoutMs;
    }
};

/**
 * Hold progress info and try to estimate current progress
 */
class ProgressData {
public:
    /**
     * big steps, each step contains many operations, such as
     * data prepare, message send/receive.
     * The partitions of these steps are based on profiling data
     * for many usage scenarios and different sync modes
     */
    enum ProgressStep {
        /** an invalid step */
        PRO_SYNC_INVALID = 0,
        /** 
         * sync prepare step: do some preparations and checkings, 
         * such as source preparation, engine preparation
         */
        PRO_SYNC_PREPARE,
        /** 
         * session init step: transport connection set up, 
         * start a session, authentication and dev info generation
         * normally it needs one time syncML messages send-receive.
         * Sometimes it may need messages send/receive many times to 
         * handle authentication
         */
        PRO_SYNC_INIT,
        /** 
         * prepare sync data and send data, also receive data from server.
         * Also may need send/receive messages more than one time if too 
         * much data.
         * assume 5 items to be sent by default
         */
        PRO_SYNC_DATA,
        /** 
         * item receive handling, send client's status to server and 
         * close the session 
         * assume 5 items to be received by default
         */
        PRO_SYNC_UNINIT,
        /** number of sync steps */
        PRO_SYNC_TOTAL
    };
    /**
     * internal mode to represent whether it is possible that data is sent to 
     * server or received from server. This could help remove some incorrect
     * hypothesis. For example, if only to client, then it is no data item 
     * sending to server. 
     */
    enum InternalMode {
        INTERNAL_NONE = 0,
        INTERNAL_ONLY_TO_CLIENT = 1,
        INTERNAL_ONLY_TO_SERVER = 1 << 1,
        INTERNAL_TWO_WAY = 1 + (1 << 1)
    };

    /**
     * treat a one-time send-receive without data items
     * as an internal standard unit.
     * below are ratios of other operations compared to one 
     * standard unit.
     * These ratios might be dynamicall changed in the future.
     */
    /** PRO_SYNC_PREPARE step ratio to standard unit */
    static const float PRO_SYNC_PREPARE_RATIO = 0.2;
    /** data prepare for data items to standard unit. All are combined by profiling data */
    static const float DATA_PREPARE_RATIO = 0.10;
    /** one data item send's ratio to standard unit */
    static const float ONEITEM_SEND_RATIO = 0.05;
    /** one data item receive&parse's ratio to standard unit */
    static const float ONEITEM_RECEIVE_RATIO = 0.05;
    /** connection setup to standard unit */
    static const float CONN_SETUP_RATIO = 0.5;
    /** assume the number of data items */
    static const int DEFAULT_ITEMS = 5;
    /** default times of message send/receive in each step */
    static const int MSG_SEND_RECEIVE_TIMES = 1;

    ProgressData(int32_t &progress);

    /**
     * change the big step 
     */
    void setStep(ProgressStep step); 

    /**
     * calc progress when a message is sent
     */
    void sendStart();

    /**
     * calc progress when a message is received from server 
     */
    void receiveEnd();

    /** 
     * re-calc progress proportions according to syncmode hint
     * typically, if only refresh-from-client, then
     * client won't receive data items.
     */
    void addSyncMode(SyncMode mode);

    /**
     * calc progress when data prepare for sending 
     */
    void itemPrepare();

    /**
     * calc progress when a data item is received
     */
    void itemReceive(const string &source, int count, int total);

private:

    /** update progress data */
    void updateProg(float ratio);

    /** dynamically adapt the proportion of each step by their current units */
    void recalc();

    /** internally check sync mode */
    void checkInternalMode();

    /** get total units of current step and remaining steps */
    float getRemainTotalUnits();

    /** get default units of given step */
    static float getDefaultUnits(ProgressStep step);

private:
    /** a reference of progress percentage */
    int32_t &m_progress;
    /** current big step */
    ProgressStep m_step;
    /** count of message send/receive in current step. Cleared in the start of a new step */
    int m_sendCounts;
    /** internal sync mode combinations */ 
    int m_internalMode;
    /** proportions when each step is end */
    float m_syncProp[PRO_SYNC_TOTAL];
    /** remaining units of each step according to current step */
    float m_syncUnits[PRO_SYNC_TOTAL];
    /** proportion of a standard unit, may changes dynamically */
    float m_propOfUnit;
    /** current sync source */
    string m_source;
};

/**
 * Represents and implements the Session interface.  Use
 * boost::shared_ptr to track it and ensure that there are references
 * to it as long as the connection is needed.
 */
class Session : public DBusObjectHelper,
                public Resource,
                private ReadOperations,
                private boost::noncopyable
{
    DBusServer &m_server;
    const std::string m_sessionID;
    std::string m_peerDeviceID;

    bool m_serverMode;
    SharedBuffer m_initialMessage;
    string m_initialMessageType;

    boost::weak_ptr<Connection> m_connection;
    std::string m_connectionError;
    bool m_useConnection;

    /** temporary config changes */
    FilterConfigNode::ConfigFilter m_syncFilter;
    FilterConfigNode::ConfigFilter m_sourceFilter;
    typedef std::map<std::string, FilterConfigNode::ConfigFilter> SourceFilters_t;
    SourceFilters_t m_sourceFilters;

    /** whether dbus clients set temporary configs */
    bool m_tempConfig;

    /**
     * True while clients are allowed to make calls other than Detach(),
     * which is always allowed. Some calls are not allowed while this
     * session runs a sync, which is indicated by a non-NULL m_sync
     * pointer.
     */
    bool m_active;

    /**
     * The SyncEvolution instance which currently prepares or runs a sync.
     */
    boost::shared_ptr<DBusSync> m_sync;

    /**
     * the sync status for session
     */
    enum SyncStatus {
        SYNC_QUEUEING,    ///< waiting to become ready for use
        SYNC_IDLE,        ///< ready, session is initiated but sync not started
        SYNC_RUNNING, ///< sync is running
        SYNC_ABORT, ///< sync is aborting
        SYNC_SUSPEND, ///< sync is suspending
        SYNC_DONE, ///< sync is done
        SYNC_ILLEGAL 
    };

    /** current sync status */
    SyncStatus m_syncStatus;

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

    /** timer for fire status/progress usages */
    Timer m_statusTimer;
    Timer m_progressTimer;

    /** Session.Detach() */
    void detach(const Caller_t &caller);

    /** Session.SetConfig() */
    void setConfig(bool update, bool temporary,
                   const ReadOperations::Config_t &config);

    /** Session.GetStatus() */
    void getStatus(std::string &status,
                   uint32_t &error,
                   SourceStatuses_t &sources);
    /** Session.GetProgress() */
    void getProgress(int32_t &progress,
                     SourceProgresses_t &sources);

    /**
     * Must be called each time that properties changing the
     * overall status are changed. Ensures that the corresponding
     * D-Bus signal is sent.
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
    EmitSignal3<const std::string &,
                uint32_t,
                const SourceStatuses_t &> emitStatus;
    /** Session.ProgressChanged */
    EmitSignal2<int32_t,
                const SourceProgresses_t &> emitProgress;

    static string syncStatusToString(SyncStatus state);

public:
    Session(DBusServer &server,
            const std::string &peerDeviceID,
            const std::string &config_name,
            const std::string &session);
    ~Session();

    enum {
        PRI_DEFAULT = 0,
        PRI_CONNECTION = 10
    };

    /**
     * Default priority is 0. Higher means less important.
     */
    void setPriority(int priority) { m_priority = priority; }
    int getPriority() const { return m_priority; }

    void initServer(SharedBuffer data, const std::string &messageType);
    void setConnection(const boost::shared_ptr<Connection> c) { m_connection = c; m_useConnection = c; }
    boost::weak_ptr<Connection> getConnection() { return m_connection; }
    bool useConnection() { return m_useConnection; }

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
    void setConnectionError(const std::string error) { m_connectionError = error; }
    std::string getConnectionError() { return m_connectionError; }


    DBusServer &getServer() { return m_server; }
    std::string getConfigName() { return m_configName; }
    std::string getSessionID() const { return m_sessionID; }
    std::string getPeerDeviceID() const { return m_peerDeviceID; }

    /**
     * TRUE if the session is ready to take over control
     */
    bool readyToRun() { return (m_syncStatus != SYNC_DONE) && m_sync; }

    /**
     * transfer control to the session for the duration of the sync,
     * returns when the sync is done (successfully or unsuccessfully)
     */
    void run();

    /**
     * called when the session is ready to run (true) or
     * lost the right to make changes (false)
     */
    void setActive(bool active);

    void syncProgress(sysync::TProgressEventEnum type,
                      int32_t extra1, int32_t extra2, int32_t extra3);
    void sourceProgress(sysync::TProgressEventEnum type,
                        SyncSource &source,
                        int32_t extra1, int32_t extra2, int32_t extra3);

    typedef StringMap SourceModes_t;
    /** Session.Sync() */
    void sync(const std::string &mode, const SourceModes_t &source_modes);
    /** Session.Abort() */
    void abort();
    /** Session.Suspend() */
    void suspend();

    bool isSuspend() { return m_syncStatus == SYNC_SUSPEND; }
    bool isAbort() { return m_syncStatus == SYNC_ABORT; }

private:
    /** set m_syncFilter and m_sourceFilters to config */
    virtual void setFilters(SyncConfig &config);
};


/**
 * Represents and implements the Connection interface.
 *
 * The connection interacts with a Session by creating the Session and
 * exchanging data with it. For that, the connection registers itself
 * with the Session and unregisters again when it goes away.
 *
 * In contrast to clients, the Session only keeps a weak_ptr, which
 * becomes invalid when the referenced object gets deleted. Typically
 * this means the Session has to abort, unless reconnecting is
 * supported.
 */
class Connection : public DBusObjectHelper, public Resource
{
    DBusServer &m_server;
    StringMap m_peer;
    bool m_mustAuthenticate;
    enum {
        SETUP,          /**< ready for first message */
        PROCESSING,     /**< received message, waiting for engine's reply */
        WAITING,        /**< waiting for next follow-up message */
        FINAL,          /**< engine has sent final reply, wait for ACK by peer */
        DONE,           /**< peer has closed normally after the final reply */
        FAILED          /**< in a failed state, no further operation possible */
    } m_state;
    std::string m_failure;

    /** first parameter for Session::sync() */
    std::string m_syncMode;
    /** second parameter for Session::sync() */
    Session::SourceModes_t m_sourceModes;

    const std::string m_sessionID;
    boost::shared_ptr<Session> m_session;

    /**
     * main loop that our DBusTransportAgent is currently waiting in,
     * NULL if not waiting
     */
    GMainLoop *m_loop;

    /**
     * get our peer session out of the DBusTransportAgent,
     * if it is currently waiting for us (indicated via m_loop)
     */
    void wakeupSession();

    /**
     * buffer for received data, waiting here for engine to ask
     * for it via DBusTransportAgent::getReply().
     */
    SharedBuffer m_incomingMsg;
    std::string m_incomingMsgType;

    /**
     * records the reason for the failure, sends Abort signal and puts
     * the connection into the FAILED state.
     */
    void failed(const std::string &reason);

    /**
     * returns "<description> (<ID> via <transport> <transport_description>)"
     */
    static std::string buildDescription(const StringMap &peer);

    /** Connection.Process() */
    void process(const Caller_t &caller,
                 const std::pair<size_t, const uint8_t *> &message,
                 const std::string &message_type);
    /** Connection.Close() */
    void close(const Caller_t &caller,
               bool normal,
               const std::string &error);
    /** wrapper around sendAbort */
    void abort();
    /** Connection.Abort */
    EmitSignal0 sendAbort;
    bool m_abortSent;
    /** Connection.Reply */
    EmitSignal5<const std::pair<size_t, const uint8_t *> &,
                const std::string &,
                const StringMap &,
                bool,
                const std::string &> reply;

    friend class DBusTransportAgent;

public:
    const std::string m_description;

    Connection(DBusServer &server,
               const DBusConnectionPtr &conn,
               const std::string &session_num,
               const StringMap &peer,
               bool must_authenticate);

    ~Connection();

    /** session requested by us is ready to run a sync */
    void ready();

    /** connection is no longer needed, ensure that it gets deleted */
    void shutdown();

    /** peer is not trusted, must authenticate as part of SyncML */
    bool mustAuthenticate() const { return m_mustAuthenticate; }
};

/**
 * A proxy for a Connection instance. The Connection instance can go
 * away (weak pointer, must be locked and and checked each time it is
 * needed). The agent must remain available as long as the engine
 * needs and basically becomes unusuable once the connection dies.
 *
 * Reconnecting is not currently supported.
 */ 
class DBusTransportAgent : public TransportAgent
{
    GMainLoop *m_loop;
    Session &m_session;
    boost::weak_ptr<Connection> m_connection;

    std::string m_url;
    std::string m_type;
    TransportCallback m_callback;
    void *m_callbackData;
    int m_callbackInterval;

    SharedBuffer m_incomingMsg;
    std::string m_incomingMsgType;

    void doWait(boost::shared_ptr<Connection> &connection);

 public:
    DBusTransportAgent(GMainLoop *loop,
                       Session &session,
                       boost::weak_ptr<Connection> connection);
    ~DBusTransportAgent();

    virtual void setURL(const std::string &url) { m_url = url; }
    virtual void setContentType(const std::string &type) { m_type = type; }
    virtual void send(const char *data, size_t len);
    virtual void cancel() {}
    virtual void shutdown();
    virtual Status wait(bool noReply = false);
    virtual void setCallback (TransportCallback cb, void * udata, int interval)
    {
        m_callback = cb;
        m_callbackData = udata;
        m_callbackInterval = interval;
    }
    virtual void getReply(const char *&data, size_t &len, std::string &contentType);
};

/***************** ReadOperations implementation ****************/

ReadOperations::ReadOperations(const std::string &config_name) :
    m_configName(config_name)
{}

void ReadOperations::getConfigs(bool getTemplates, std::vector<std::string> &configNames)
{
    SyncConfig::ServerList list;
    if (getTemplates) {
        list = SyncConfig::getServerTemplates();
    } else {
        list = SyncConfig::getServers();
    }
    BOOST_FOREACH(const SyncConfig::ServerList::value_type &server, list) {
        configNames.push_back(server.first);
    }
}

boost::shared_ptr<SyncConfig> ReadOperations::getLocalConfig(const string &configName)
{
    string peer, context;
    SyncConfig::splitConfigString(SyncConfig::normalizeConfigString(configName),
                                  peer, context);

    boost::shared_ptr<SyncConfig> syncConfig(new SyncConfig(configName));
    // the default configuration can always be opened for reading,
    // everything else must exist
    if ((context != "default" || peer != "") &&
        !syncConfig->exists()) {
        SE_THROW_EXCEPTION(NoSuchConfig, "No configuration '" + configName + "' found");
    }

    // TODO: handle temporary configs (MB #8116)
    // - if config was set temporarily, it doesn't have to exist on disk =>
    //   the check above is too strict
    // - set temporary properties as filters

    return syncConfig;
}

void ReadOperations::getConfig(bool getTemplate,
                               Config_t &config)
{
    map<string, string> localConfigs;
    boost::shared_ptr<SyncConfig> syncConfig;
    /** get server template */
    if(getTemplate) {
        string peer, context;
        SyncConfig::splitConfigString(SyncConfig::normalizeConfigString(m_configName),
                                      peer, context);

        syncConfig = SyncConfig::createServerTemplate(peer);
        if(!syncConfig.get()) {
            SE_THROW_EXCEPTION(NoSuchConfig, "No template '" + m_configName + "' found");
        }

        // use the shared properties from the right context as filter
        // so that the returned template preserves existing properties
        boost::shared_ptr<SyncConfig> shared = getLocalConfig(string("@") + context);

        ConfigProps props;
        shared->getProperties()->readProperties(props);
        syncConfig->setConfigFilter(true, "", props);
        BOOST_FOREACH(std::string source, shared->getSyncSources()) {
            SyncSourceNodes nodes = shared->getSyncSourceNodes(source, "");
            props.clear();
            nodes.getProperties()->readProperties(props);
            syncConfig->setConfigFilter(false, source, props);
        }
    } else {
        syncConfig = getLocalConfig(m_configName);
    }
    setFilters(*syncConfig);

    /** get sync properties and their values */
    ConfigPropertyRegistry &syncRegistry = SyncConfig::getRegistry();
    BOOST_FOREACH(const ConfigProperty *prop, syncRegistry) {
        bool isDefault = false;
        string value = prop->getProperty(*syncConfig->getProperties(), &isDefault);
        if(!isDefault) {
            localConfigs.insert(pair<string, string>(prop->getName(), value));
        }
    }

    config.insert(pair<string,map<string, string> >("", localConfigs));

    /* get configurations from sources */
    list<string> sources = syncConfig->getSyncSources();
    BOOST_FOREACH(const string &name, sources) {
        localConfigs.clear();
        SyncSourceNodes sourceNodes = syncConfig->getSyncSourceNodes(name);
        ConfigPropertyRegistry &sourceRegistry = SyncSourceConfig::getRegistry();
        BOOST_FOREACH(const ConfigProperty *prop, sourceRegistry) {
            bool isDefault = false;
            string value = prop->getProperty(*sourceNodes.getProperties(), &isDefault);
            if(!isDefault) {
                localConfigs.insert(pair<string, string>(prop->getName(), value));
            }
        }
        config.insert(pair<string, map<string, string> >( "source/" + name, localConfigs));
    }
}

void ReadOperations::getReports(uint32_t start, uint32_t count,
                                Reports_t &reports)
{
    if(m_configName.empty()) {
        // TODO: an empty config name should return reports for
        // all peers (MB#8049)
        SE_THROW_EXCEPTION(NoSuchConfig,
                           "listing reports without peer name not implemented yet");
    }
    SyncContext client(m_configName, false);
    std::vector<string> dirs;
    client.getSessions(dirs);

    uint32_t index = 0;
    BOOST_FOREACH( const string &dir, dirs) {
        /** if start plus count is bigger than actual size, then return actual - size reports */
        if(index >= start && index - start < count) {
            std::map<string, string> aReport;
            SyncReport report;
            client.readSessionInfo(dir,report);

            /** serialize report to ConfigProps and then copy them to reports */
            HashFileConfigNode node("/dev/null","",true);
            node << report;
            ConfigProps props;
            node.readProperties(props);

            BOOST_FOREACH(const ConfigProps::value_type &entry, props) {
                aReport.insert(entry);
            }
            reports.push_back(aReport);
        }
        index++;
    }
}

void ReadOperations::checkSource(const std::string &sourceName)
{
    boost::shared_ptr<SyncConfig> config(new SyncConfig(m_configName));
    setFilters(*config);

    list<std::string> sourceNames = config->getSyncSources();
    list<std::string>::iterator it;
    for(it = sourceNames.begin(); it != sourceNames.end(); ++it) {
        if(*it == sourceName) {
            break;
        }
    }
    if(it == sourceNames.end()) {
        SE_THROW_EXCEPTION(NoSuchSource, "'" + m_configName + "' has no '" + sourceName + "' source");
    }
    bool checked = false;
    try {
        // this can already throw exceptions when the config is invalid
        SyncSourceParams params(sourceName, config->getSyncSourceNodes(sourceName));
        auto_ptr<SyncSource> syncSource(SyncSource::createSource(params, false));

        if (syncSource.get()) {
            syncSource->open();
            // success!
            checked = true;
        }
    } catch (...) {
        Exception::handle();
    }

    if (!checked) {
        SE_THROW_EXCEPTION(SourceUnusable, "The source '" + sourceName + "' is not usable");
    }
}
void ReadOperations::getDatabases(const string &sourceName, SourceDatabases_t &databases)
{
    boost::shared_ptr<SyncConfig> config(new SyncConfig(m_configName));
    setFilters(*config);

    SyncSourceParams params(sourceName, config->getSyncSourceNodes(sourceName));
    const SourceRegistry &registry(SyncSource::getSourceRegistry());
    BOOST_FOREACH(const RegisterSyncSource *sourceInfo, registry) {
        SyncSource *source = sourceInfo->m_create(params);
        if (!source) {
            continue;
        } else if (source == RegisterSyncSource::InactiveSource) {
            SE_THROW_EXCEPTION(NoSuchSource, "'" + m_configName + "' backend of source '" + sourceName + "' is not supported");
        } else {
            auto_ptr<SyncSource> autoSource(source);
            databases = autoSource->getDatabases();
            return;
        }
    }

    SE_THROW_EXCEPTION(NoSuchSource, "'" + m_configName + "' has no '" + sourceName + "' source");
}

/***************** DBusSync implementation **********************/

DBusSync::DBusSync(const std::string &config,
                   Session &session) :
    SyncContext(config, true),
    m_session(session)
{
}

boost::shared_ptr<TransportAgent> DBusSync::createTransportAgent()
{
    if (m_session.useConnection()) {
        // use the D-Bus Connection to send and receive messages
        return boost::shared_ptr<TransportAgent>(new DBusTransportAgent(m_session.getServer().getLoop(),
                                                                        m_session,
                                                                        m_session.getConnection()));
    } else {
        // no connection, use HTTP via libsoup/GMainLoop
        GMainLoop *loop = m_session.getServer().getLoop();
        g_main_loop_ref(loop);
        boost::shared_ptr<HTTPTransportAgent> agent(new SoupTransportAgent(loop));
        agent->setConfig(*this);
        return agent;
    }
}

void DBusSync::displaySyncProgress(sysync::TProgressEventEnum type,
                                   int32_t extra1, int32_t extra2, int32_t extra3)
{
    SyncContext::displaySyncProgress(type, extra1, extra2, extra3);
    m_session.syncProgress(type, extra1, extra2, extra3);
}

void DBusSync::displaySourceProgress(sysync::TProgressEventEnum type,
                                     SyncSource &source,
                                     int32_t extra1, int32_t extra2, int32_t extra3)
{
    SyncContext::displaySourceProgress(type, source, extra1, extra2, extra3);
    m_session.sourceProgress(type, source, extra1, extra2, extra3);
}

bool DBusSync::checkForSuspend()
{
    return m_session.isSuspend() || SyncContext::checkForSuspend();
}

bool DBusSync::checkForAbort()
{
    return m_session.isAbort() || SyncContext::checkForAbort();
}

int DBusSync::sleep(int intervals)
{
    time_t start = time(NULL);
    while (true) {
        g_main_context_iteration(NULL, false);
        time_t now = time(NULL);
        if (checkForSuspend() || checkForAbort()) {
            return  (intervals - now + start);
        } 
        if (intervals - now + start <= 0) {
            return intervals - now +start;
        }
    }
}

/***************** Session implementation ***********************/

void Session::detach(const Caller_t &caller)
{
    boost::shared_ptr<Client> client(m_server.findClient(caller));
    if (!client) {
        throw runtime_error("unknown client");
    }
    client->detach(this);
}

static void setSyncFilters(const ReadOperations::Config_t &config,FilterConfigNode::ConfigFilter &syncFilter,std::map<std::string, FilterConfigNode::ConfigFilter> &sourceFilters)
{
    ReadOperations::Config_t::const_iterator it;
    for (it = config.begin(); it != config.end(); it++) {
        map<string, string>::const_iterator sit;
        if(it->first.empty()) {
            for (sit = it->second.begin(); sit != it->second.end(); sit++) {
                syncFilter.insert(*sit);
            }
        } else {
            string name = it->first;
            if(name.find("source/") == 0) {
                name = name.substr(7); ///> 7 is the length of "source/"
                FilterConfigNode::ConfigFilter &sourceFilter = sourceFilters[name];
                for (sit = it->second.begin(); sit != it->second.end(); sit++) {
                    sourceFilter.insert(*sit);
                }
            }
        }
    }
}
void Session::setConfig(bool update, bool temporary,
                        const ReadOperations::Config_t &config)
{
    if (!m_active) {
        SE_THROW_EXCEPTION(InvalidCall, "session is not active, call not allowed at this time");
    }
    if (m_sync) {
        SE_THROW_EXCEPTION(InvalidCall, "sync started, cannot change configuration at this time");
    }
    if (!update && temporary) {
        throw std::runtime_error("Clearing existing configuration and temporary configuration changes which only affects the duration of the session are mutually exclusive");
    }

    /** check whether we need remove the entire configuration */
    if(!update && config.empty()) {
        boost::shared_ptr<SyncConfig> syncConfig(new SyncConfig(getConfigName()));
        if(syncConfig.get()) {
            syncConfig->remove();
        }
        return;
    }
    if(temporary) {
        /* save temporary configs in session filters */
        setSyncFilters(config, m_syncFilter, m_sourceFilters);
        m_tempConfig = true;
    } else {
        FilterConfigNode::ConfigFilter syncFilter;
        std::map<std::string, FilterConfigNode::ConfigFilter> sourceFilters;
        setSyncFilters(config, syncFilter, sourceFilters);
        /* need to save configurations */
        boost::shared_ptr<SyncConfig> from(new SyncConfig(getConfigName()));
        /* if it is not clear mode and config does not exist, an error throws */
        if(update && !from->exists()) {
            SE_THROW_EXCEPTION(NoSuchConfig, "The configuration '" + getConfigName() + "' doesn't exist" );
        }
        if(!update) {
            list<string> sources = from->getSyncSources();
            list<string>::iterator it;
            for(it = sources.begin(); it != sources.end(); ++it) {
                string source = "source/";
                source += *it;
                ReadOperations::Config_t::const_iterator configIt = config.find(source);
                if(configIt == config.end()) {
                    /** if no config for this source, we remove it */
                    from->removeSyncSource(*it);
                } else {
                    /** just clear visiable properties, remove them and their values */
                    from->clearSyncSourceProperties(*it);
                }
            }
            from->clearSyncProperties();
        }
        /** generate new sources in the config map */
        for (ReadOperations::Config_t::const_iterator it = config.begin(); it != config.end(); ++it) {
            string sourceName = it->first;
            if(sourceName.find("source/") == 0) {
                sourceName = sourceName.substr(7); ///> 7 is the length of "source/"
                from->getSyncSourceNodes(sourceName);
            }
        }
        /* apply user settings */
        from->setConfigFilter(true, "", syncFilter);
        map<string, FilterConfigNode::ConfigFilter>::iterator it;
        for ( it = sourceFilters.begin(); it != sourceFilters.end(); it++ ) {
            from->setConfigFilter(false, it->first, it->second);
        }
        boost::shared_ptr<DBusSync> syncConfig(new DBusSync(getConfigName(), *this));
        syncConfig->copy(*from, NULL);

        syncConfig->preFlush(*syncConfig);
        syncConfig->flush();
    }
}

void Session::initServer(SharedBuffer data, const std::string &messageType)
{
    m_serverMode = true;
    m_initialMessage = data;
    m_initialMessageType = messageType;
}

void Session::sync(const std::string &mode, const SourceModes_t &source_modes)
{
    if (!m_active) {
        SE_THROW_EXCEPTION(InvalidCall, "session is not active, call not allowed at this time");
    }
    if (m_sync) {
        SE_THROW_EXCEPTION(InvalidCall, "sync started, cannot start again");
    }

    m_sync.reset(new DBusSync(getConfigName(), *this));
    if (m_serverMode) {
        m_sync->initServer(m_sessionID,
                           m_initialMessage,
                           m_initialMessageType);
        boost::shared_ptr<Connection> c = m_connection.lock();
        if (c && !c->mustAuthenticate()) {
            // unsetting username/password disables checking them
            m_syncFilter["password"] = "";
            m_syncFilter["username"] = "";
        }
    }

    // Apply temporary config filters. The parameters of this function
    // override the source filters, if set.
    m_sync->setConfigFilter(true, "", m_syncFilter);
    FilterConfigNode::ConfigFilter filter;
    filter = m_sourceFilter;
    if (!mode.empty()) {
        filter[SyncSourceConfig::m_sourcePropSync.getName()] = mode;
    }
    m_sync->setConfigFilter(false, "", filter);
    BOOST_FOREACH(const std::string &source,
                  m_sync->getSyncSources()) {
        filter = m_sourceFilters[source];
        SourceModes_t::const_iterator it = source_modes.find(source);
        if (it != source_modes.end()) {
            filter[SyncSourceConfig::m_sourcePropSync.getName()] = it->second;
        }
        m_sync->setConfigFilter(false, source, filter);
    }

    // Update status and progress. From now on, all configured sources
    // have their default entry (referencing them by name creates the
    // entry).
    BOOST_FOREACH(const std::string source,
                  m_sync->getSyncSources()) {
        m_sourceStatus[source];
        m_sourceProgress[source];
    }
    fireProgress(true);
    fireStatus(true);

    // now that we have a DBusSync object, return from the main loop
    // and once that is done, transfer control to that object
    g_main_loop_quit(loop);
}

void Session::abort()
{
    if (!m_sync) {
        SE_THROW_EXCEPTION(InvalidCall, "sync not started, cannot abort at this time");
    }
    m_syncStatus = SYNC_ABORT;
    fireStatus(true);

    // state change, return to caller so that it can react
    g_main_loop_quit(m_server.getLoop());
}

void Session::suspend()
{
    if (!m_sync) {
        SE_THROW_EXCEPTION(InvalidCall, "sync not started, cannot suspend at this time");
    }
    m_syncStatus = SYNC_SUSPEND;
    fireStatus(true);
    g_main_loop_quit(m_server.getLoop());
}

void Session::getStatus(std::string &status,
                        uint32_t &error,
                        SourceStatuses_t &sources)
{
    status = syncStatusToString(m_syncStatus);
    // TODO: append ";processing" or ";waiting"

    error = m_error;
    sources = m_sourceStatus;
}

void Session::getProgress(int32_t &progress,
                          SourceProgresses_t &sources)
{
    progress = m_progress;
    sources = m_sourceProgress;
}

void Session::fireStatus(bool flush)
{
    std::string status;
    uint32_t error;
    SourceStatuses_t sources;

    /** not force flushing and not timeout, return */
    if(!flush && !m_statusTimer.timeout()) {
        return;
    }
    m_statusTimer.reset();

    getStatus(status, error, sources);
    emitStatus(status, error, sources);
}

void Session::fireProgress(bool flush)
{
    int32_t progress;
    SourceProgresses_t sources;

    /** not force flushing and not timeout, return */
    if(!flush && !m_progressTimer.timeout()) {
        return;
    }
    m_progressTimer.reset();

    getProgress(progress, sources);
    emitProgress(progress, sources);
}
string Session::syncStatusToString(SyncStatus state)
{
    switch(state) {
    case SYNC_QUEUEING:
        return "queueing";
    case SYNC_IDLE:
        return "idle";
    case SYNC_RUNNING:
        return "running";
    case SYNC_ABORT:
        return "aborting";
    case SYNC_SUSPEND:
        return "suspending";
    case SYNC_DONE:
        return "done";
    default:
        return "";
    };
}

Session::Session(DBusServer &server,
                 const std::string &peerDeviceID,
                 const std::string &config_name,
                 const std::string &session) :
    DBusObjectHelper(server.getConnection(),
                     std::string("/org/syncevolution/Session/") + session,
                     "org.syncevolution.Session"),
    ReadOperations(config_name),
    m_server(server),
    m_sessionID(session),
    m_peerDeviceID(peerDeviceID),
    m_serverMode(false),
    m_useConnection(false),
    m_tempConfig(false),
    m_active(false),
    m_syncStatus(SYNC_QUEUEING),
    m_priority(PRI_DEFAULT),
    m_progress(0),
    m_progData(m_progress),
    m_error(0),
    m_statusTimer(100),
    m_progressTimer(50),
    emitStatus(*this, "StatusChanged"),
    emitProgress(*this, "ProgressChanged")
{
    add(this, &Session::detach, "Detach");
    add(&ReadOperations::getConfigs, "GetConfigs");
    add(static_cast<ReadOperations *>(this), &ReadOperations::getConfig, "GetConfig");
    add(this, &Session::setConfig, "SetConfig");
    add(static_cast<ReadOperations *>(this), &ReadOperations::getReports, "GetReports");
    add(static_cast<ReadOperations *>(this), &ReadOperations::checkSource, "CheckSource");
    add(static_cast<ReadOperations *>(this), &ReadOperations::getDatabases, "GetDatabases");
    add(this, &Session::sync, "Sync");
    add(this, &Session::abort, "Abort");
    add(this, &Session::suspend, "Suspend");
    add(this, &Session::getStatus, "GetStatus");
    add(this, &Session::getProgress, "GetProgress");
    add(emitStatus);
    add(emitProgress);
}

Session::~Session()
{
    m_server.dequeue(this);
}
    

void Session::setActive(bool active)
{
    m_active = active;
    if (active) {
        if (m_syncStatus == SYNC_QUEUEING) {
            m_syncStatus = SYNC_IDLE;
            fireStatus(true);
        }

        boost::shared_ptr<Connection> c = m_connection.lock();
        if (c) {
            c->ready();
        }
    }
}

void Session::syncProgress(sysync::TProgressEventEnum type,
                           int32_t extra1, int32_t extra2, int32_t extra3)
{
    switch(type) {
    case sysync::PEV_SESSIONSTART:
        m_progData.setStep(ProgressData::PRO_SYNC_INIT);
        fireProgress(true);
        break;
    case sysync::PEV_SESSIONEND:
        if((uint32_t)extra1 != m_error) {
            m_error = extra1;
            fireStatus(true);
        }
        m_progData.setStep(ProgressData::PRO_SYNC_INVALID);
        fireProgress(true);
        break;
    case sysync::PEV_SENDSTART:
        m_progData.sendStart();
        break;
    case sysync::PEV_SENDEND:
    case sysync::PEV_RECVSTART:
    case sysync::PEV_RECVEND:
        m_progData.receiveEnd();
        fireProgress();
        break;
    case sysync::PEV_DISPLAY100:
    case sysync::PEV_SUSPENDCHECK:
    case sysync::PEV_DELETING:
        break;
    case sysync::PEV_SUSPENDING:
        m_syncStatus = SYNC_SUSPEND;
        fireStatus(true);
        break;
    default:
        ;
    }
}

void Session::sourceProgress(sysync::TProgressEventEnum type,
                             SyncSource &source,
                             int32_t extra1, int32_t extra2, int32_t extra3)
{
    SourceProgress &progress = m_sourceProgress[source.getName()];
    SourceStatus &status = m_sourceStatus[source.getName()];
    switch(type) {
    case sysync::PEV_SYNCSTART:
        if(source.getFinalSyncMode() != SYNC_NONE) {
            m_progData.setStep(ProgressData::PRO_SYNC_UNINIT);
            fireProgress();
        }
        break;
    case sysync::PEV_SYNCEND:
        if(source.getFinalSyncMode() != SYNC_NONE) {
            status.set(PrettyPrintSyncMode(source.getFinalSyncMode()), "done", extra1);
            fireStatus(true);
        }
        break;
    case sysync::PEV_PREPARING:
        if(source.getFinalSyncMode() != SYNC_NONE) {
            progress.m_phase        = "preparing";
            progress.m_prepareCount = extra1;
            progress.m_prepareTotal = extra2;
            m_progData.itemPrepare();
            fireProgress(true);
        }
        break;
    case sysync::PEV_ITEMSENT:
        if(source.getFinalSyncMode() != SYNC_NONE) {
            progress.m_phase     = "sending";
            progress.m_sendCount = extra1;
            progress.m_sendTotal = extra2;
            fireProgress(true);
        }
        break;
    case sysync::PEV_ITEMRECEIVED:
        if(source.getFinalSyncMode() != SYNC_NONE) {
            progress.m_phase        = "receiving";
            progress.m_receiveCount = extra1;
            progress.m_receiveTotal = extra2;
            m_progData.itemReceive(source.getName(), extra1, extra2);
            fireProgress(true);
        }
        break;
    case sysync::PEV_ALERTED:
        if(source.getFinalSyncMode() != SYNC_NONE) {
            status.set(PrettyPrintSyncMode(source.getFinalSyncMode()), "running", 0);
            fireStatus(true);
            m_progData.setStep(ProgressData::PRO_SYNC_DATA);
            m_progData.addSyncMode(source.getFinalSyncMode());
            fireProgress();
        }
        break;
    default:
        ;
    }
}

void Session::run()
{
    if (m_sync) {
        try {
            m_syncStatus = SYNC_RUNNING;
            fireStatus(true);
            SyncMLStatus status;
            m_progData.setStep(ProgressData::PRO_SYNC_PREPARE);
            try {
                status = m_sync->sync();
            } catch (...) {
                status = m_sync->handleException();
            }
            if (!m_error) {
                m_error = status;
            }

            // if there is a connection, then it is no longer needed
            boost::shared_ptr<Connection> c = m_connection.lock();
            if (c) {
                c->shutdown();
            }
        } catch (...) {
            // we must enter SYNC_DONE under all circumstances,
            // even when failing during connection shutdown
            m_syncStatus = SYNC_DONE;
            fireStatus(true);
            throw;
        }
        m_syncStatus = SYNC_DONE;
        fireStatus(true);
    }
}

void Session::setFilters(SyncConfig &config)
{
    /** apply temporary configs to config */
    config.setConfigFilter(true, "", m_syncFilter);
    // set all sources in the filter to config
    BOOST_FOREACH(const SourceFilters_t::value_type &value, m_sourceFilters) {
        config.setConfigFilter(false, value.first, value.second);
    }
}

/************************ ProgressData implementation *****************/
ProgressData::ProgressData(int32_t &progress) 
    : m_progress(progress),
    m_step(PRO_SYNC_INVALID),
    m_sendCounts(0),
    m_internalMode(INTERNAL_NONE)
{
    /**
     * init default units of each step 
     */
    float totalUnits = 0.0;
    for(int i = 0; i < PRO_SYNC_TOTAL; i++) {
        float units = getDefaultUnits((ProgressStep)i);
        m_syncUnits[i] = units;
        totalUnits += units;
    }
    m_propOfUnit = 1.0 / totalUnits;

    /** 
     * init default sync step proportions. each step stores proportions of
     * its previous steps and itself.
     */
    m_syncProp[0] = 0;
    for(int i = 1; i < PRO_SYNC_TOTAL - 1; i++) {
        m_syncProp[i] = m_syncProp[i - 1] + m_syncUnits[i] / totalUnits;
    }
    m_syncProp[PRO_SYNC_TOTAL - 1] = 1.0;
}

void ProgressData::setStep(ProgressStep step) 
{
    if(m_step != step) {
        /** if state is changed, progress is set as the end of current step*/
        m_progress = 100.0 * m_syncProp[(int)m_step];
        m_step = step; ///< change to new state
        m_sendCounts = 0; ///< clear send/receive counts 
        m_source = ""; ///< clear source
    }
}

void ProgressData::sendStart()
{
    checkInternalMode();
    m_sendCounts++;

    /* self adapts. If a new send and not default, we need re-calculate proportions */
    if(m_sendCounts > MSG_SEND_RECEIVE_TIMES) {
        m_syncUnits[(int)m_step] += 1;
        recalc();
    }
    /** 
     * If in the send operation of PRO_SYNC_UNINIT, it often takes extra time
     * to send message due to items handling 
     */
    if(m_step == PRO_SYNC_UNINIT && m_syncUnits[(int)m_step] != MSG_SEND_RECEIVE_TIMES) {
        updateProg(DATA_PREPARE_RATIO);
    }
}

void ProgressData::receiveEnd()
{
    /** 
     * often receiveEnd is the last operation of each step by default.
     * If more send/receive, then we need expand proportion of current 
     * step and re-calc them
     */
    updateProg(m_syncUnits[(int)m_step]);
}

void ProgressData::addSyncMode(SyncMode mode)
{
    switch(mode) {
        case SYNC_TWO_WAY:
        case SYNC_SLOW:
            m_internalMode |= INTERNAL_TWO_WAY;
            break;
        case SYNC_ONE_WAY_FROM_CLIENT:
        case SYNC_REFRESH_FROM_CLIENT:
            m_internalMode |= INTERNAL_ONLY_TO_CLIENT;
            break;
        case SYNC_ONE_WAY_FROM_SERVER:
        case SYNC_REFRESH_FROM_SERVER:
            m_internalMode |= INTERNAL_ONLY_TO_SERVER;
            break;
        default:
            ;
    };
}

void ProgressData::itemPrepare()
{
    checkInternalMode();
    /**
     * only the first PEV_ITEMPREPARE event takes some time
     * due to data access, other events don't according to
     * profiling data
     */
    if(m_source.empty()) {
        m_source = "source"; ///< use this to check whether itemPrepare occurs
        updateProg(DATA_PREPARE_RATIO);
    }
}

void ProgressData::itemReceive(const string &source, int count, int total)
{
    /** 
     * source is used to check whether a new source is received
     * If the first source, we compare its total number and default number
     * then re-calc sync units
     */
    if(m_source.empty()) {
        m_source = source;
        if(total != 0) {
            m_syncUnits[PRO_SYNC_UNINIT] += ONEITEM_RECEIVE_RATIO * (total - DEFAULT_ITEMS);
            recalc();
        }
    /** if another new source, add them into sync units */
    } else if(m_source != source){
        m_source = source;
        if(total != 0) {
            m_syncUnits[PRO_SYNC_UNINIT] += ONEITEM_RECEIVE_RATIO * total;
            recalc();
        }
    } 
    updateProg(ONEITEM_RECEIVE_RATIO);
}

void ProgressData::updateProg(float ratio)
{
    m_progress += m_propOfUnit * 100 * ratio;
    m_syncUnits[(int)m_step] -= ratio;
}

/** dynamically adapt the proportion of each step by their current units */
void ProgressData::recalc()
{
    float units = getRemainTotalUnits();
    if(std::abs(units) < std::numeric_limits<float>::epsilon()) {
        m_propOfUnit = 0.0;
    } else {
        m_propOfUnit = ( 100.0 - m_progress ) / (100.0 * units);
    }
    if(m_step != PRO_SYNC_TOTAL -1 ) {
        m_syncProp[(int)m_step] = m_progress / 100.0 + m_syncUnits[(int)m_step] * m_propOfUnit;
        for(int i = ((int)m_step) + 1; i < PRO_SYNC_TOTAL - 1; i++) {
            m_syncProp[i] = m_syncProp[i - 1] + m_syncUnits[i] * m_propOfUnit;
        }
    }
}

void ProgressData::checkInternalMode() 
{
    if(!m_internalMode) {
        return;
    } else if(m_internalMode & INTERNAL_TWO_WAY) {
        // don't adjust
    } else if(m_internalMode & INTERNAL_ONLY_TO_CLIENT) {
        // only to client, remove units of prepare and send
        m_syncUnits[PRO_SYNC_DATA] -= (ONEITEM_RECEIVE_RATIO * DEFAULT_ITEMS + DATA_PREPARE_RATIO);
        recalc();
    } else if(m_internalMode & INTERNAL_ONLY_TO_SERVER) {
        // only to server, remove units of receive
        m_syncUnits[PRO_SYNC_UNINIT] -= (ONEITEM_RECEIVE_RATIO * DEFAULT_ITEMS + DATA_PREPARE_RATIO);
        recalc();
    }
    m_internalMode = INTERNAL_NONE;
}

float ProgressData::getRemainTotalUnits()
{
    float total = 0.0;
    for(int i = (int)m_step; i < PRO_SYNC_TOTAL; i++) {
        total += m_syncUnits[i];
    }
    return total;
}

float ProgressData::getDefaultUnits(ProgressStep step)
{
    switch(step) {
        case PRO_SYNC_PREPARE:
            return PRO_SYNC_PREPARE_RATIO;
        case PRO_SYNC_INIT:
            return CONN_SETUP_RATIO + MSG_SEND_RECEIVE_TIMES;
        case PRO_SYNC_DATA:
            return ONEITEM_SEND_RATIO * DEFAULT_ITEMS + DATA_PREPARE_RATIO + MSG_SEND_RECEIVE_TIMES;
        case PRO_SYNC_UNINIT:
            return ONEITEM_RECEIVE_RATIO * DEFAULT_ITEMS + DATA_PREPARE_RATIO + MSG_SEND_RECEIVE_TIMES;
        default:
            return 0;
    };
}

/************************ Connection implementation *****************/

void Connection::failed(const std::string &reason)
{
    if (m_failure.empty()) {
        m_failure = reason;
        if (m_session) {
            m_session->setConnectionError(reason);
        }
    }
    if (m_state != FAILED) {
        abort();
    }
    m_state = FAILED;
}

std::string Connection::buildDescription(const StringMap &peer)
{
    StringMap::const_iterator
        desc = peer.find("description"),
        id = peer.find("id"),
        trans = peer.find("transport"),
        trans_desc = peer.find("transport_description");
    std::string buffer;
    buffer.reserve(256);
    if (desc != peer.end()) {
        buffer += desc->second;
    }
    if (id != peer.end() || trans != peer.end()) {
        if (!buffer.empty()) {
            buffer += " ";
        }
        buffer += "(";
        if (id != peer.end()) {
            buffer += id->second;
            if (trans != peer.end()) {
                buffer += " via ";
            }
        }
        if (trans != peer.end()) {
            buffer += trans->second;
            if (trans_desc != peer.end()) {
                buffer += " ";
                buffer += trans_desc->second;
            }
        }
        buffer += ")";
    }
    return buffer;
}

void Connection::wakeupSession()
{
    if (m_loop) {
        g_main_loop_quit(m_loop);
        m_loop = NULL;
    }
}

void Connection::process(const Caller_t &caller,
             const std::pair<size_t, const uint8_t *> &message,
             const std::string &message_type)
{
    SE_LOG_DEBUG(NULL, NULL, "D-Bus client %s sends %lu bytes via connection %s, %s",
                 caller.c_str(),
                 (unsigned long)message.first,
                 getPath(),
                 message_type.c_str());

    boost::shared_ptr<Client> client(m_server.findClient(caller));
    if (!client) {
        throw runtime_error("unknown client");
    }

    boost::shared_ptr<Connection> myself =
        boost::static_pointer_cast<Connection, Resource>(client->findResource(this));
    if (!myself) {
        throw runtime_error("client does not own connection");
    }

    // any kind of error from now on terminates the connection
    try {
        switch (m_state) {
        case SETUP: {
            std::string config;
            std::string peerDeviceID;
            bool serverMode = false;
            // check message type, determine whether we act
            // as client or server, choose config
            if (message_type == "HTTP Config") {
                // type used for testing, payload is config name
                config.assign(reinterpret_cast<const char *>(message.second),
                              message.first);
            } else if (message_type == TransportAgent::m_contentTypeServerAlertedNotificationDS) {
            	sysync::SanPackage san;
            	if (san.PassSan(const_cast<uint8_t *>(message.second), message.first) || san.GetHeader()) {
                    // We are very tolerant regarding the content of the message.
                    // If it doesn't parse, try to do something useful anyway.
                    config = "default";
                    SE_LOG_DEBUG(NULL, NULL, "SAN parsing failed, falling back to 'default' config");
            	} else {
                    // Extract server ID and match it against a server
                    // configuration.  Multiple different peers might use the
                    // same serverID ("PC Suite"), so check properties of the
                    // of our configs first before going back to the name itself.
                    std::string serverID = san.fServerID;
                    SyncConfig::ServerList servers = SyncConfig::getServers();
                    BOOST_FOREACH(const SyncConfig::ServerList::value_type &server,
                    	          servers) {
                    	SyncContext context(server.first);
                    	if (context.getSyncURL() == serverID) {
                            config = server.first;
                    	    break;
                    	}

                    	// TODO: for other transports match against
                    	// transport specific properties, like Bluetooth MAC
                        // address
                    }
                    if (config.empty()) {
                        BOOST_FOREACH(const SyncConfig::ServerList::value_type &server,
                                      servers) {
                            if (server.first == serverID) {
                                config = serverID;
                                break;
                            }
                        }
                    }

                    // pick "default" as configuration name if none matched
                    if (config.empty()) {
                        config = "default";
                        SE_LOG_DEBUG(NULL, NULL, "SAN Server ID '%s' unknown, falling back to 'default' config", serverID.c_str());
                    }

                    // TODO: create a suitable configuration automatically?!

                    SE_LOG_DEBUG(NULL, NULL, "SAN sync with config %s", config.c_str());

                    // extract number of sources
                    int numSources = san.fNSync;
                    int syncType;
                    uint32_t contentType;
                    std::string serverURI;
                    if (!numSources) {
                        SE_LOG_DEBUG(NULL, NULL, "SAN message with no sources");
                        // Synchronize all known sources with the selected mode.
                        if (san.GetNthSync(0, syncType, contentType, serverURI)) {
                            SE_LOG_DEBUG(NULL, NULL, "SAN invalid header, using default modes");
                        } else if (syncType < SYNC_FIRST || syncType > SYNC_LAST) {
                            SE_LOG_DEBUG(NULL, NULL, "SAN invalid sync type %d, using default modes", syncType);
                        } else {
                            m_syncMode = PrettyPrintSyncMode(SyncMode(syncType), true);
                            SE_LOG_DEBUG(NULL, NULL, "SAN sync mode for all configured sources: %s", m_syncMode.c_str());
                        }
                    } else {
                        const SyncContext context(config);
                        std::list<std::string> sources = context.getSyncSources();

                        // check what the server wants us to synchronize
                        // and only synchronize that
                        m_syncMode = "disabled";
                        for (int sync = 1; sync <= numSources; sync++) {
                            if (san.GetNthSync(sync, syncType, contentType, serverURI)) {
                                SE_LOG_DEBUG(NULL, NULL, "SAN invalid sync entry #%d", sync);
                            } else if (syncType < SYNC_FIRST || syncType > SYNC_LAST) {
                                SE_LOG_DEBUG(NULL, NULL, "SAN invalid sync type %d for entry #%d, ignoring entry", syncType, sync);
                            } else {
                                std::string syncMode = PrettyPrintSyncMode(SyncMode(syncType), true);
                                bool found = false;
                                BOOST_FOREACH(const std::string &source, sources) {
                                    boost::shared_ptr<const PersistentSyncSourceConfig> sourceConfig(context.getSyncSourceConfig(source));
                                    // prefix match because the local
                                    // configuration might contain
                                    // additional parameters (like date
                                    // range selection for events)
                                    if (boost::starts_with(sourceConfig->getURI(), serverURI)) {
                                        SE_LOG_DEBUG(NULL, NULL,
                                                     "SAN entry #%d = source %s with mode %s",
                                                     sync, source.c_str(), syncMode.c_str());
                                        m_sourceModes[source] = syncMode;
                                        found = true;
                                        break;
                                    }
                                }
                                if (!found) {
                                    SE_LOG_DEBUG(NULL, NULL,
                                                 "SAN entry #%d with mode %s ignored because Server URI %s is unknown",
                                                 sync, syncMode.c_str(), serverURI.c_str());
                                }
                            }
                        }

                        if (m_sourceModes.empty()) {
                            SE_LOG_DEBUG(NULL, NULL,
                                         "SAN message with no known entries, falling back to default");
                            m_syncMode = "";
                        }
                    }
                }

                // TODO: use the session ID set by the server if non-null
            } else if (message_type == TransportAgent::m_contentTypeSyncML ||
                       message_type == TransportAgent::m_contentTypeSyncWBXML) {
                // run a new SyncML session as server
                serverMode = true;
                if (m_peer.find("config") == m_peer.end() &&
                    !m_peer["config"].empty()) {
                    SE_LOG_DEBUG(NULL, NULL, "ignoring pre-chosen config '%s'",
                                 m_peer["config"].c_str());
                }

                // peek into the data to extract the locURI = device ID,
                // then use it to find the configuration
                SyncContext::SyncMLMessageInfo info;
                info = SyncContext::analyzeSyncMLMessage(reinterpret_cast<const char *>(message.second),
                                                         message.first,
                                                         message_type);
                if (info.m_deviceID.empty()) {
                    // TODO: proper exception
                    throw runtime_error("could not extract LocURI=deviceID from initial message");
                }
                BOOST_FOREACH(const StringPair &entry,
                              SyncConfig::getServers()) {
                    SyncConfig peer(entry.first);
                    if (info.m_deviceID == peer.getRemoteDevID()) {
                        config = entry.first;
                        SE_LOG_DEBUG(NULL, NULL, "matched %s against config %s (%s)",
                                     info.toString().c_str(),
                                     entry.first.c_str(),
                                     entry.second.c_str());
                        break;
                    }
                }
                if (config.empty()) {
                    // TODO: proper exception
                    throw runtime_error(string("no configuration found for ") +
                                        info.toString());
                }

                // abort previous session of this client
                m_server.killSessions(info.m_deviceID);
                peerDeviceID = info.m_deviceID;
            } else {
                throw runtime_error("message type not supported for starting a sync");
            }

            // run session as client or server
            m_state = PROCESSING;
            m_session.reset(new Session(m_server,
                                        peerDeviceID,
                                        config,
                                        m_sessionID));
            if (serverMode) {
                m_session->initServer(SharedBuffer(reinterpret_cast<const char *>(message.second),
                                                   message.first),
                                      message_type);
            }
            m_session->setPriority(Session::PRI_CONNECTION);
            m_session->setConnection(myself);
            // this will be reset only when the connection shuts down okay
            // or overwritten with the error given to us in
            // Connection::close()
            m_session->setConnectionError("closed prematurely");
            m_server.enqueue(m_session);
            break;
        }
        case PROCESSING:
            throw std::runtime_error("protocol error: already processing a message");
            break;        
        case WAITING:
            m_incomingMsg = SharedBuffer(reinterpret_cast<const char *>(message.second),
                                         message.first);
            m_incomingMsgType = message_type;
            m_state = PROCESSING;
            // get out of DBusTransportAgent::wait()
            wakeupSession();
            break;
        case FINAL:
            wakeupSession();
            throw std::runtime_error("protocol error: final reply sent, no further message processing possible");
        case DONE:
            throw std::runtime_error("protocol error: connection closed, no further message processing possible");
            break;
        case FAILED:
            throw std::runtime_error(m_failure);
            break;
        default:
            throw std::runtime_error("protocol error: unknown internal state");
            break;
        }
    } catch (const std::exception &error) {
        failed(error.what());
        throw;
    } catch (...) {
        failed("unknown exception in Connection::process");
        throw;
    }
}

void Connection::close(const Caller_t &caller,
                       bool normal,
                       const std::string &error)
{
    SE_LOG_DEBUG(NULL, NULL, "D-Bus client %s closes connection %s %s%s%s",
                 caller.c_str(),
                 getPath(),
                 normal ? "normally" : "with error",
                 error.empty() ? "" : ": ",
                 error.c_str());

    boost::shared_ptr<Client> client(m_server.findClient(caller));
    if (!client) {
        throw runtime_error("unknown client");
    }

    if (!normal ||
        m_state != FINAL) {
        std::string err = error.empty() ?
            "connection closed unexpectedly" :
            error;
        if (m_session) {
            m_session->setConnectionError(err);
        }
        failed(err);
    } else {
        m_state = DONE;
        if (m_session) {
            m_session->setConnectionError("");
        }
    }

    // remove reference to us from client, will destruct *this*
    // instance!
    client->detach(this);
}

void Connection::abort()
{
    if (!m_abortSent) {
        sendAbort();
        m_abortSent = true;
        m_state = FAILED;
    }
}

void Connection::shutdown()
{
    // trigger removal of this connection by removing all
    // references to it
    m_server.detach(this);
}

Connection::Connection(DBusServer &server,
                       const DBusConnectionPtr &conn,
                       const std::string &sessionID,
                       const StringMap &peer,
                       bool must_authenticate) :
    DBusObjectHelper(conn.get(),
                     std::string("/org/syncevolution/Connection/") + sessionID,
                     "org.syncevolution.Connection"),
    m_server(server),
    m_peer(peer),
    m_mustAuthenticate(must_authenticate),
    m_state(SETUP),
    m_sessionID(sessionID),
    m_loop(NULL),
    sendAbort(*this, "Abort"),
    m_abortSent(false),
    reply(*this, "Reply"),
    m_description(buildDescription(peer))
{
    add(this, &Connection::process, "Process");
    add(this, &Connection::close, "Close");
    add(sendAbort);
    add(reply);
}

Connection::~Connection()
{
    SE_LOG_DEBUG(NULL, NULL, "done with connection to '%s'%s%s%s",
                 m_description.c_str(),
                 m_state == DONE ? ", normal shutdown" : " unexpectedly",
                 m_failure.empty() ? "" : ": ",
                 m_failure.c_str());
    try {
        if (m_state != DONE) {
            abort();
        }
        // DBusTransportAgent waiting? Wake it up.
        wakeupSession();
        m_session.use_count();
        m_session.reset();
    } catch (...) {
        // log errors, but do not propagate them because we are
        // destructing
        Exception::handle();
    }
}

void Connection::ready()
{
    // proceed with sync now that our session is ready
    m_session->sync(m_syncMode, m_sourceModes);
}

/****************** DBusTransportAgent implementation **************/

DBusTransportAgent::DBusTransportAgent(GMainLoop *loop,
                                       Session &session,
                                       boost::weak_ptr<Connection> connection) :
    m_loop(loop),
    m_session(session),
    m_connection(connection)
{
}

DBusTransportAgent::~DBusTransportAgent()
{
    boost::shared_ptr<Connection> connection = m_connection.lock();
    if (connection) {
        connection->shutdown();
    }
}

void DBusTransportAgent::send(const char *data, size_t len)
{
    boost::shared_ptr<Connection> connection = m_connection.lock();

    if (!connection) {
        SE_THROW_EXCEPTION(TransportException,
                           "D-Bus peer has disconnected");
    }

    if (connection->m_state != Connection::PROCESSING) {
        SE_THROW_EXCEPTION(TransportException,
                           "cannot send to our D-Bus peer");
    }

    // Change state in advance. If we fail while replying, then all
    // further resends will fail with the error above.
    connection->m_state = Connection::WAITING;
    connection->m_incomingMsg = SharedBuffer();

    // TODO: turn D-Bus exceptions into transport exceptions
    StringMap meta;
    meta["URL"] = m_url;
    connection->reply(std::make_pair(len, reinterpret_cast<const uint8_t *>(data)),
                      m_type, meta, false, connection->m_sessionID);
}

void DBusTransportAgent::shutdown()
{
    boost::shared_ptr<Connection> connection = m_connection.lock();

    if (!connection) {
        SE_THROW_EXCEPTION(TransportException,
                           "D-Bus peer has disconnected");
    }

    if (connection->m_state != Connection::FAILED) {
        // send final, empty message and wait for close
        connection->m_state = Connection::FINAL;
        connection->reply(std::pair<size_t, const uint8_t *>(0, 0),
                          "", StringMap(),
                          true, connection->m_sessionID);
    }
}

void DBusTransportAgent::doWait(boost::shared_ptr<Connection> &connection)
{
    // let Connection wake us up when it has a reply or
    // when it closes down
    connection->m_loop = m_loop;

    // release our reference so that the Connection instance can
    // be destructed when requested by the D-Bus peer
    connection.reset();

    // TODO: setup regular callback

    // now wait
    g_main_loop_run(m_loop);
}

DBusTransportAgent::Status DBusTransportAgent::wait(bool noReply)
{
    boost::shared_ptr<Connection> connection = m_connection.lock();

    if (!connection) {
        SE_THROW_EXCEPTION(TransportException,
                           "D-Bus peer has disconnected");
    }

    switch (connection->m_state) {
    case Connection::PROCESSING:
        m_incomingMsg = connection->m_incomingMsg;
        m_incomingMsgType = connection->m_incomingMsgType;
        return GOT_REPLY;
        break;
    case Connection::FINAL:
        doWait(connection);

        // if the connection is still available, then keep waiting
        connection = m_connection.lock();
        if (connection) {
            return ACTIVE;
        } else if (m_session.getConnectionError().empty()) {
            return INACTIVE;
        } else {
            SE_THROW_EXCEPTION(TransportException, m_session.getConnectionError());
            return FAILED;
        }
        break;
    case Connection::WAITING:
        if (noReply) {
            // message is sent as far as we know, so return
            return INACTIVE;
        }

        doWait(connection);

        // tell caller to check again
        return ACTIVE;
        break;
    case Connection::DONE:
        if (!noReply) {
            SE_THROW_EXCEPTION(TransportException,
                               "internal error: transport has shut down, can no longer receive reply");
        }
        
        return CLOSED;
    default:
        SE_THROW_EXCEPTION(TransportException,
                           "internal error: send() on connection which is not ready");
        break;
    }

    return FAILED;
}

void DBusTransportAgent::getReply(const char *&data, size_t &len, std::string &contentType)
{
    data = m_incomingMsg.get();
    len = m_incomingMsg.size();
    contentType = m_incomingMsgType;
}


/********************** DBusServer implementation ******************/

void DBusServer::clientGone(Client *c)
{
    for(Clients_t::iterator it = m_clients.begin();
        it != m_clients.end();
        ++it) {
        if (it->second.get() == c) {
            SE_LOG_DEBUG(NULL, NULL, "D-Bus client %s has disconnected",
                         c->m_ID.c_str());
            m_clients.erase(it);
            return;
        }
    }
    SE_LOG_DEBUG(NULL, NULL, "unknown client has disconnected?!");
}

std::string DBusServer::getNextSession()
{
    // Make the session ID somewhat random. This protects to
    // some extend against injecting unwanted messages into the
    // communication.
    m_lastSession++;
    if (!m_lastSession) {
        m_lastSession++;
    }
    return StringPrintf("%u%u", rand(), m_lastSession);
}

void DBusServer::attachClient(const Caller_t &caller,
                              const boost::shared_ptr<Watch> &watch)
{
    // TODO: implement idle detection and automatic shutdown of the server
}

void DBusServer::detachClient(const Caller_t &caller)
{
}

void DBusServer::connect(const Caller_t &caller,
                         const boost::shared_ptr<Watch> &watch,
                         const StringMap &peer,
                         bool must_authenticate,
                         const std::string &session,
                         DBusObject_t &object)
{
    if (!session.empty()) {
        // reconnecting to old connection is not implemented yet
        throw std::runtime_error("not implemented");
    }
    std::string new_session = getNextSession();

    boost::shared_ptr<Connection> c(new Connection(*this,
                                                   getConnection(),
                                                   new_session,
                                                   peer,
                                                   must_authenticate));
    SE_LOG_DEBUG(NULL, NULL, "connecting D-Bus client %s with connection %s '%s'",
                 caller.c_str(),
                 c->getPath(),
                 c->m_description.c_str());
        
    boost::shared_ptr<Client> client = addClient(getConnection(),
                                                 caller,
                                                 watch);
    client->attach(c);
    c->activate();

    object = c->getPath();
}

void DBusServer::startSession(const Caller_t &caller,
                              const boost::shared_ptr<Watch> &watch,
                              const std::string &server,
                              DBusObject_t &object)
{
    boost::shared_ptr<Client> client = addClient(getConnection(),
                                                 caller,
                                                 watch);
    std::string new_session = getNextSession();   
    boost::shared_ptr<Session> session(new Session(*this,
                                                   "is this a client or server session?",
                                                   server,
                                                   new_session));
    client->attach(session);
    session->activate();
    enqueue(session);
    object = session->getPath();
}

void DBusServer::checkPresence(const std::string &server,
                               std::string &status,
                               std::vector<std::string> &transports)
{
    // TODO: implement this, right now always return status = "" = available
}

void DBusServer::getSessions(std::vector<std::string> &sessions)
{
    sessions.reserve(m_workQueue.size() + 1);
    if (m_activeSession) {
        sessions.push_back(m_activeSession->getPath());
    }
    BOOST_FOREACH(boost::weak_ptr<Session> &session, m_workQueue) {
        boost::shared_ptr<Session> s = session.lock();
        if (s) {
            sessions.push_back(s->getPath());
        }
    }
}

DBusServer::DBusServer(GMainLoop *loop, const DBusConnectionPtr &conn) :
    DBusObjectHelper(conn.get(), "/org/syncevolution/Server", "org.syncevolution.Server"),
    m_loop(loop),
    m_lastSession(time(NULL)),
    m_activeSession(NULL),
    sessionChanged(*this, "SessionChanged"),
    presence(*this, "Presence")
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    srand(tv.tv_usec);
    add(this, &DBusServer::attachClient, "Attach");
    add(this, &DBusServer::detachClient, "Detach");
    add(this, &DBusServer::connect, "Connect");
    add(this, &DBusServer::startSession, "StartSession");
    add(&ReadOperations::getConfigs, "GetConfigs");
    add(this, &DBusServer::getConfig, "GetConfig");
    add(this, &DBusServer::getReports, "GetReports");
    add(this, &DBusServer::checkSource, "CheckSource");
    add(this, &DBusServer::getDatabases, "GetDatabases");
    add(this, &DBusServer::checkPresence, "CheckPresence");
    add(this, &DBusServer::getSessions, "GetSessions");
    add(sessionChanged);
    add(presence);
}

DBusServer::~DBusServer()
{
    // make sure all other objects are gone before destructing ourselves
    m_syncSession.reset();
    m_workQueue.clear();
    m_clients.clear();
}

void DBusServer::run()
{
    while (!shutdownRequested) {
        if (!m_activeSession ||
            !m_activeSession->readyToRun()) {
            g_main_loop_run(m_loop);
        }
        if (m_activeSession &&
            m_activeSession->readyToRun()) {
            // this session must be owned by someone, otherwise
            // it would not be set as active session
            boost::shared_ptr<Session> session = m_activeSessionRef.lock();
            if (!session) {
                throw runtime_error("internal error: session no longer available");
            }
            try {
                // ensure that the session doesn't go away
                m_syncSession.swap(session);
                m_activeSession->run();
            } catch (const std::exception &ex) {
                SE_LOG_ERROR(NULL, NULL, "%s", ex.what());
            } catch (...) {
                SE_LOG_ERROR(NULL, NULL, "unknown error");
            }
            session.swap(m_syncSession);
            dequeue(session.get());
        }
    }
}


/**
 * look up client by its ID
 */
boost::shared_ptr<Client> DBusServer::findClient(const Caller_t &ID)
{
    for(Clients_t::iterator it = m_clients.begin();
        it != m_clients.end();
        ++it) {
        if (it->second->m_ID == ID) {
            return it->second;
        }
    }
    return boost::shared_ptr<Client>();
}

boost::shared_ptr<Client> DBusServer::addClient(const DBusConnectionPtr &conn,
                                                const Caller_t &ID,
                                                const boost::shared_ptr<Watch> &watch)
{
    boost::shared_ptr<Client> client(findClient(ID));
    if (client) {
        return client;
    }
    client.reset(new Client(ID));
    // add to our list *before* checking that peer exists, so
    // that clientGone() can remove it if the check fails
    m_clients.push_back(std::make_pair(watch, client));
    watch->setCallback(boost::bind(&DBusServer::clientGone, this, client.get()));
    return client;
}


void DBusServer::detach(Resource *resource)
{
    BOOST_FOREACH(const Clients_t::value_type &client_entry,
                  m_clients) {
        client_entry.second->detachAll(resource);
    }
}

void DBusServer::enqueue(const boost::shared_ptr<Session> &session)
{
    WorkQueue_t::iterator it = m_workQueue.end();
    while (it != m_workQueue.begin()) {
        --it;
        if (it->lock()->getPriority() <= session->getPriority()) {
            ++it;
            break;
        }
    }
    m_workQueue.insert(it, session);

    checkQueue();
}

int DBusServer::killSessions(const std::string &peerDeviceID)
{
    int count = 0;

    WorkQueue_t::iterator it = m_workQueue.begin();
    while (it != m_workQueue.end()) {
        boost::shared_ptr<Session> session = it->lock();
        if (session && session->getPeerDeviceID() == peerDeviceID) {
            SE_LOG_DEBUG(NULL, NULL, "removing pending session %s because it matches deviceID %s",
                         session->getSessionID().c_str(),
                         peerDeviceID.c_str());
            // remove session and its corresponding connection
            boost::shared_ptr<Connection> c = session->getConnection().lock();
            if (c) {
                c->shutdown();
            }
            it = m_workQueue.erase(it);
            count++;
        } else {
            ++it;
        }
    }

    if (m_activeSession &&
        m_activeSession->getPeerDeviceID() == peerDeviceID) {
        SE_LOG_DEBUG(NULL, NULL, "aborting active session %s because it matches deviceID %s",
                     m_activeSession->getSessionID().c_str(),
                     peerDeviceID.c_str());
        try {
            // abort, even if not necessary right now
            m_activeSession->abort();
        } catch (...) {
            // TODO: catch only that exception which indicates
            // incorrect use of the function
        }
        dequeue(m_activeSession);
        count++;
    }

    return count;
}

void DBusServer::dequeue(Session *session)
{
    if (m_syncSession.get() == session) {
        // This is the running sync session.
        // It's not in the work queue and we have to
        // keep it active, so nothing to do.
        return;
    }

    for (WorkQueue_t::iterator it = m_workQueue.begin();
         it != m_workQueue.end();
         ++it) {
        if (it->lock().get() == session) {
            // remove from queue
            m_workQueue.erase(it);
            // session was idle, so nothing else to do
            return;
        }
    }

    if (m_activeSession == session) {
        // The session is releasing the lock, so someone else might
        // run now.
        session->setActive(false);
        sessionChanged(session->getPath(), false);
        m_activeSession = NULL;
        m_activeSessionRef.reset();
        checkQueue();
        return;
    }
}

void DBusServer::checkQueue()
{
    if (m_activeSession) {
        // still busy
        return;
    }

    while (!m_workQueue.empty()) {
        boost::shared_ptr<Session> session = m_workQueue.front().lock();
        m_workQueue.pop_front();
        if (session) {
            // activate the session
            m_activeSession = session.get();
            m_activeSessionRef = session;
            session->setActive(true);
            sessionChanged(session->getPath(), true);
            return;
        }
    }
}

/**************************** main *************************/

void niam(int sig)
{
    shutdownRequested = true;
    SyncContext::handleSignal(sig);
    g_main_loop_quit (loop);
}

int main(int argc, char **argv)
{
    try {
        g_type_init();
        g_thread_init(NULL);
        g_set_application_name("SyncEvolution");
        loop = g_main_loop_new (NULL, FALSE);

        setvbuf(stderr, NULL, _IONBF, 0);
        setvbuf(stdout, NULL, _IONBF, 0);

        signal(SIGTERM, niam);
        signal(SIGINT, niam);

        LoggerBase::instance().setLevel(LoggerBase::DEBUG);

        DBusErrorCXX err;
        DBusConnectionPtr conn = g_dbus_setup_bus(DBUS_BUS_SESSION,
                                                  "org.syncevolution",
                                                  &err);
        if (!conn) {
            err.throwFailure("g_dbus_setup_bus()");
        }

        DBusServer server(loop, conn);
        server.activate();

        std::cout << argv[0] << " ready to run\n" << std::flush;
        server.run();
	return 0;
    } catch ( const std::exception &ex ) {
        SE_LOG_ERROR(NULL, NULL, "%s", ex.what());
    } catch (...) {
        SE_LOG_ERROR(NULL, NULL, "unknown error");
    }

    return 1;
}
