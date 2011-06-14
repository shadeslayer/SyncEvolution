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

#ifndef SYNCEVO_DBUS_SERVER_H
#define SYNCEVO_DBUS_SERVER_H

#include "syncevo-exceptions.h"
#include "common.h"

#include "bluez-manager.h"
#include "timer.h"
#include "auto-term.h"
#include "timeout.h"

using namespace GDBusCXX;
using namespace SyncEvo;

SE_BEGIN_CXX

/**
 * Encapsulates startup environment from main() and can do execve()
 * with it later on. Assumes that argv[0] is the executable to run.
 */
class Restart
{
    vector<string> m_argv;
    vector<string> m_env;

    void saveArray(vector<string> &array, char **p)
    {
        while(*p) {
            array.push_back(*p);
            p++;
        }
    }

    const char **createArray(const vector<string> &array)
    {
        const char **res = new const char *[(array.size() + 1)];
        size_t i;
        for (i = 0; i < array.size(); i++) {
            res[i] = array[i].c_str();
        }
        res[i] = NULL;
        return res;
    }

public:
    Restart(char **argv, char **env)
    {
        saveArray(m_argv, argv);
        saveArray(m_env, env);
    }

    void restart()
    {
        const char **argv = createArray(m_argv);
        const char **env = createArray(m_env);
        LogRedirect::reset();
        if (execve(argv[0], (char *const *)argv, (char *const *)env)) {
            SE_THROW(StringPrintf("restarting syncevo-dbus-server failed: %s", strerror(errno)));
        }
    }
};

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
class DBusUserInterface;
class DBusServer;

/**
 * Implements the read-only methods in a Session and the Server.
 * Only data is the server configuration name, everything else
 * is created and destroyed inside the methods.
 */
class ReadOperations
{
public:
    const std::string m_configName;

    DBusServer &m_server;

    ReadOperations(const std::string &config_name, DBusServer &server);

    /** the double dictionary used to represent configurations */
    typedef std::map< std::string, StringMap > Config_t;

    /** the array of reports filled by getReports() */
    typedef std::vector< StringMap > Reports_t;

    /** the array of databases used by getDatabases() */
    typedef SyncSource::Database SourceDatabase;
    typedef SyncSource::Databases SourceDatabases_t;

    /** implementation of D-Bus GetConfigs() */
    void getConfigs(bool getTemplates, std::vector<std::string> &configNames);

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
     * Return true if filters exists and have been set.
     * Otherwise, nothing is set to config
     */
    virtual bool setFilters(SyncConfig &config) { return false; }

    /**
     * utility method which constructs a SyncConfig which references a local configuration (never a template)
     *
     * In general, the config must exist, except in two cases:
     * - configName = @default (considered always available)
     * - mustExist = false (used when reading a templates for a context which might not exist yet)
     */
    boost::shared_ptr<DBusUserInterface> getLocalConfig(const std::string &configName, bool mustExist = true);
};

SE_END_CXX
namespace GDBusCXX {

/**
 * dbus_traits for SourceDatabase. Put it here for
 * avoiding polluting gxx-dbus-bridge.h
 */
template<> struct dbus_traits<ReadOperations::SourceDatabase> :
    public dbus_struct_traits<ReadOperations::SourceDatabase,
                              dbus_member<ReadOperations::SourceDatabase, std::string, &ReadOperations::SourceDatabase::m_name,
                              dbus_member<ReadOperations::SourceDatabase, std::string, &ReadOperations::SourceDatabase::m_uri,
                              dbus_member_single<ReadOperations::SourceDatabase, bool, &ReadOperations::SourceDatabase::m_isDefault> > > >{};

}
SE_BEGIN_CXX

class InfoReq;

/**
 * a listener to listen changes of session
 * currently only used to track changes of running a sync in a session
 */
class SessionListener
{
public:
    /**
     * method is called when a sync is successfully started.
     * Here 'successfully started' means the synthesis engine starts
     * to access the sources.
     */
    virtual void syncSuccessStart() {}

    /**
     * method is called when a sync is done. Also
     * sync status are passed.
     */
    virtual void syncDone(SyncMLStatus status) {}

    virtual ~SessionListener() {}
};

/**
 * Manager to manage automatic sync.
 * Once a configuration is enabled with automatic sync, possibly http or obex-bt or both, one or more
 * tasks for different URLs are added in the task map, grouped by their intervals.
 * A task have to be checked whether there is an existing same task in the working queue. Once actived,
 * it is put in the working queue.
 *
 * At any time, there is at most one session for the first task. Once it is active by DBusServer,
 * we prepare it and make it ready to run. After completion, a new session is created again for the
 * next task. And so on.
 *
 * The DBusServer is in charge of dispatching requests from dbus clients and automatic sync tasks.
 * See DBusServer::run().
 *
 * Here there are 3 scenarios which have been considered to do automatic sync right now:
 * 1) For a config enables autosync, an interval has passed.
 * 2) Once users log in or resume and an interval has passed. Not implemented yet.
 * 3) Evolution data server notify any changes. Not implemented yet.
 */
class AutoSyncManager : public SessionListener
{
    DBusServer &m_server;

    public:
    /**
     * A single task for automatic sync.
     * Each task maintain one task for only one sync URL, which never combines
     * more than one sync URLs. The difference from 'syncURL' property here is
     * that different URLs may have different transports with different statuses.
     * Another reason is that SyncContext only use the first URL if it has many sync
     * URLs when running. So we split, schedule and process them one by one.
     * Each task contains one peer name, peer duration and peer url.
     * It is created in initialization and may be updated due to config change.
     * It is scheduled by AutoSyncManager to be put in the working queue.
     */
    class AutoSyncTask
    {
     public:
        /** the peer name of a config */
        string m_peer;
        /** the time that the peer must at least have been around (seconds) */
        unsigned int m_delay;
        /** each task matches with exactly one transport supported for a peer */
        enum Transport {
            NEEDS_HTTP,
            NEEDS_BT,
            NEEDS_OTHER
        } m_transport;
        /** individual sync URL for which this task was created, matches m_transport */
        std::string m_url;

        AutoSyncTask(const string &peer, unsigned int delay, Transport transport, const std::string &url)
            : m_peer(peer), m_delay(delay), m_transport(transport), m_url(url)
        {
        }

        /** compare whether two tasks are the same. May refine it later with more information */
        bool operator==(const AutoSyncTask &right) const
        {
            return boost::iequals(m_peer, right.m_peer) &&
                m_url == right.m_url;
        }
    };

    /**
     * AutoSyncTaskList is used to manage sync tasks which are grouped by the
     * interval. Each list has one timeout gsource.
     */
    class AutoSyncTaskList : public list<AutoSyncTask>
    {
        AutoSyncManager &m_manager;
        /** the interval used to create timeout source (seconds) */
        unsigned int m_interval;

        /** timeout gsource */
        GLibEvent m_source;

        /** callback of timeout source */
        static gboolean taskListTimeoutCb(gpointer data);

     public:
        AutoSyncTaskList(AutoSyncManager &manager, unsigned int interval)
            : m_manager(manager), m_interval(interval), m_source(0)
        {}
        ~AutoSyncTaskList() {
            if(m_source) {
                g_source_remove(m_source);
            }
        }

        /** create timeout source once all tasks are added */
        void createTimeoutSource();

        /** check task list and put task into working queue */
        void scheduleTaskList();
    };

    /** init a config and set up auto sync task for it */
    void initConfig(const string &configName);

    /** remove tasks from m_peerMap and m_workQueue created from the config */
    void remove(const string &configName);

    /** a map to contain all auto sync tasks. All initialized tasks are stored here.
     * Tasks here are grouped by auto sync interval */
    typedef std::map<unsigned int, boost::shared_ptr<AutoSyncTaskList> > PeerMap;
    PeerMap m_peerMap;

    /**
     * a working queue that including tasks which are pending for doing sync.
     * Tasks here are picked from m_peerMap and scheduled to do auto sync */
    list<AutoSyncTask> m_workQueue;

    /**
     * the current active task, which may own a session
     */
    boost::shared_ptr<AutoSyncTask> m_activeTask;

    /**
     * the only session created for active task and is put in the session queue.
     * at most one session at any time no matter how many tasks we actually have
     */
    boost::shared_ptr<Session> m_session;

    /** the current sync of session is successfully started */
    bool m_syncSuccessStart;

    /** used to send notifications */
    boost::shared_ptr<NotificationManagerBase> m_notificationManager;

    /**
     * It reads all peers which are enabled to do auto sync and store them in
     * the m_peerMap and then add timeout sources in the main loop to schedule
     * auto sync tasks.
     */
    void init();

    /** operations on tasks queue */
    void clearAllTasks() { m_workQueue.clear(); }

    /** check m_peerMap and put all tasks in it to working queue */
    void scheduleAll();

    /**
     * add an auto sync task in the working queue
     * Do check before adding a task in the working queue
     * Return true if the task is added in the list.
     */
    bool addTask(const AutoSyncTask &syncTask);

    /** find an auto sync task in the working queue or is running */
    bool findTask(const AutoSyncTask &syncTask);

    /**
     * check whether a task is suitable to put in the working queue
     * Manager has the information needed to make the decision
     */
    bool taskLikelyToRun(const AutoSyncTask &syncTask);

 public:
    AutoSyncManager(DBusServer &server)
        : m_server(server), m_syncSuccessStart(false)
    {
        init();
    }

    /**
     * prevent dbus server automatic termination when it has
     * any auto sync task enabled in the configs.
     * If returning true, prevent automatic termination.
     */
    bool preventTerm() { return !m_peerMap.empty(); }

    /**
     * called when a config is changed. This causes re-loading the config
     */
    void update(const string &configName);

    /* Is there anything ready to run? */
    bool hasTask() { return !m_workQueue.empty(); }

    /* Is there anything with automatic syncing waiting for its time to run? */
    bool hasAutoConfigs() { return !m_peerMap.empty(); }

    /**
     * pick the front task from the working queue and create a session for it.
     * The session won't be used to do sync until it is active so 'prepare' is
     * for calling 'sync' to make the session ready to run
     * If there has been a session for the front task, do nothing
     */
    void startTask();

    /** check whether the active session is owned by Automatic Sync Manger */
    bool hasActiveSession();

    /** set config and run sync to make the session ready to run */
    void prepare();

    /**
     * Acts as a session listener to track sync statuses if the session is
     * belonged to auto sync manager to do auto sync.
     * Two methods to listen to session sync changes.
     */
    virtual void syncSuccessStart();
    virtual void syncDone(SyncMLStatus status);
};

class PresenceStatus {
    bool m_httpPresence;
    bool m_btPresence;
    bool m_initiated;
    DBusServer &m_server;

    /** two timers to record when the statuses of network and bt are changed */
    Timer m_httpTimer;
    Timer m_btTimer;

    enum PeerStatus {
        /* The transport is not available (local problem) */
        NOTRANSPORT,
        /* The peer is not contactable (remote problem) */
        UNREACHABLE,
        /* Not for sure whether the peer is presence but likely*/
        MIGHTWORK,

        INVALID
    };

    typedef map<string, vector<pair <string, PeerStatus> > > StatusMap;
    typedef pair<const string, vector<pair <string, PeerStatus> > > StatusPair;
    typedef pair <string, PeerStatus> PeerStatusPair;
    StatusMap m_peers;

    static std::string status2string (PeerStatus status) {
        switch (status) {
            case NOTRANSPORT:
                return "no transport";
                break;
            case UNREACHABLE:
                return "not present";
                break;
            case MIGHTWORK:
                return "";
                break;
            case INVALID:
                return "invalid transport status";
        }
        // not reached, keep compiler happy
        return "";
    }

    public:
    PresenceStatus (DBusServer &server)
        :m_httpPresence (false), m_btPresence (false), m_initiated (false), m_server (server),
        m_httpTimer(), m_btTimer()
    {
    }

    enum TransportType{
        HTTP_TRANSPORT,
        BT_TRANSPORT,
        INVALID_TRANSPORT
    };

    void init();

    /* Implement DBusServer::checkPresence*/
    void checkPresence (const string &peer, string& status, std::vector<std::string> &transport);

    void updateConfigPeers (const std::string &peer, const ReadOperations::Config_t &config);

    void updatePresenceStatus (bool httpPresence, bool btPresence);
    void updatePresenceStatus (bool newStatus, TransportType type);

    bool getHttpPresence() { return m_httpPresence; }
    bool getBtPresence() { return m_btPresence; }
    Timer& getHttpTimer() { return m_httpTimer; }
    Timer& getBtTimer() { return m_btTimer; }
};

/*
 * Implements org.connman.Manager
 * GetProperty  : getPropCb
 * PropertyChanged: propertyChanged
 **/
class ConnmanClient : public DBusRemoteObject
{
public:
    ConnmanClient (DBusServer &server);
    virtual const char *getDestination() const {return "net.connman";}
    virtual const char *getPath() const {return "/";}
    virtual const char *getInterface() const {return "net.connman.Manager";}
    virtual DBusConnection *getConnection() const {return m_connmanConn.get();}

    void propertyChanged(const string &name,
                         const boost::variant<vector<string>, string> &prop);

    void getPropCb(const std::map <std::string, boost::variant <std::vector <std::string> > >& props, const string &error);

    /** TRUE if watching ConnMan status */
    bool isAvailable() { return m_connmanConn; }

private:
    DBusServer &m_server;
    DBusConnectionPtr m_connmanConn;

    SignalWatch2 <string,boost::variant<vector<string>, string> > m_propertyChanged;
};

/**
 * Client for org.freedesktop.NetworkManager
 * The initial state of NetworkManager is queried via
 * org.freedesktop.DBus.Properties. Dynamic changes are listened via
 * org.freedesktop.NetworkManager - StateChanged signal
 */
class NetworkManagerClient : public DBusRemoteObject
{
public:
    enum NM_State
      {
        NM_STATE_UNKNOWN = 0,

        /* following values for NM < 0.9 */
        NM_STATE_ASLEEP_DEPRECATED = 1,
        NM_STATE_CONNECTING_DEPRECATED = 2,
        NM_STATE_CONNECTED_DEPRECATED = 3,
        NM_STATE_DISCONNECTED_DEPRECATED = 4,

        /* following values for NM >= 0.9 */
        NM_STATE_ASLEEP = 10,
        NM_STATE_DISCONNECTED = 20,
        NM_STATE_DISCONNECTING = 30,
        NM_STATE_CONNECTING = 40,
        NM_STATE_CONNECTED_LOCAL = 50,
        NM_STATE_CONNECTED_SITE = 60,
        NM_STATE_CONNECTED_GLOBAL = 70,
      };
public:
    NetworkManagerClient(DBusServer& server);

    virtual const char *getDestination() const {
        return "org.freedesktop.NetworkManager";
    }
    virtual const char *getPath() const {
        return "/org/freedesktop/NetworkManager";
    }
    virtual const char *getInterface() const {
        return "org.freedesktop.NetworkManager";
    }
    virtual DBusConnection *getConnection() const {
        return m_networkManagerConn.get();
    }

    void stateChanged(uint32_t uiState);

    /** TRUE if watching Network Manager status */
    bool isAvailable() { return m_networkManagerConn; }

private:

    class NetworkManagerProperties : public DBusRemoteObject
    {
    public:
        NetworkManagerProperties(NetworkManagerClient& manager);

        virtual const char *getDestination() const {
            return "org.freedesktop.NetworkManager";
        }
        virtual const char *getPath() const {
            return "/org/freedesktop/NetworkManager";
        }
        virtual const char *getInterface() const {
            return "org.freedesktop.DBus.Properties";
        }
        virtual DBusConnection* getConnection() const {
            return m_manager.getConnection();
        }
        void get();
        void getCallback(const boost::variant<uint32_t, std::string> &prop,
                         const std::string &error);
    private:
        NetworkManagerClient &m_manager;
    };

    DBusServer &m_server;
    DBusConnectionPtr m_networkManagerConn;
    SignalWatch1<uint32_t> m_stateChanged;
    NetworkManagerProperties m_properties;
};

class BluezManager;

/**
 * Implements the main org.syncevolution.Server interface.
 *
 * All objects created by it get a reference to the creating
 * DBusServer instance so that they can call some of its
 * methods. Because that instance holds references to all
 * of these objects and deletes them before destructing itself,
 * that reference is guaranteed to remain valid.
 */
class DBusServer : public DBusObjectHelper,
                   public LoggerBase
{
    GMainLoop *m_loop;
    bool &m_shutdownRequested;
    boost::shared_ptr<Restart> &m_restart;

    uint32_t m_lastSession;
    typedef std::list< std::pair< boost::shared_ptr<Watch>, boost::shared_ptr<Client> > > Clients_t;
    Clients_t m_clients;

    /**
     * Watch all files mapped into our address space. When
     * modifications are seen (as during a package upgrade), queue a
     * high priority session. This prevents running other sessions,
     * which might not be able to execute correctly. For example, a
     * sync with libsynthesis from 1.1 does not work with
     * SyncEvolution XML files from 1.2. The dummy session then waits
     * for the changes to settle (see SHUTDOWN_QUIESENCE_SECONDS) and
     * either shuts down or restarts.  The latter is necessary if the
     * daemon has automatic syncing enabled in a config.
     */
    list< boost::shared_ptr<GLibNotify> > m_files;
    void fileModified();

    /**
     * session handling the shutdown in response to file modifications
     */
    boost::shared_ptr<Session> m_shutdownSession;

    /* Event source that regurally pool network manager
     * */
    GLibEvent m_pollConnman;
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
     * a hash of pending InfoRequest
     */
    typedef std::map<string, boost::weak_ptr<InfoReq> > InfoReqMap;

    // hash map of pending info requests
    InfoReqMap m_infoReqMap;

    // the index of last info request
    uint32_t m_lastInfoReq;

    // a hash to represent matched templates for devices, the key is
    // the peer name
    typedef std::map<string, boost::shared_ptr<SyncConfig::TemplateDescription> > MatchedTemplates;

    MatchedTemplates m_matchedTempls;

    boost::shared_ptr<BluezManager> m_bluezManager;

    /** devices which have sync services */
    SyncConfig::DeviceList m_syncDevices;

    /**
     * Watch callback for a specific client or connection.
     */
    void clientGone(Client *c);

    /** Server.GetCapabilities() */
    vector<string> getCapabilities();

    /** Server.GetVersions() */
    StringMap getVersions();

    /** Server.Attach() */
    void attachClient(const Caller_t &caller,
                      const boost::shared_ptr<Watch> &watch);

    /** Server.Detach() */
    void detachClient(const Caller_t &caller);

    /** Server.DisableNotifications() */
    void disableNotifications(const Caller_t &caller,
                              const string &notifications) {
        setNotifications(false, caller, notifications);
    }

    /** Server.EnableNotifications() */
    void enableNotifications(const Caller_t &caller,
                             const string &notifications) {
        setNotifications(true, caller, notifications);
    }

    /** Server.NotificationAction() */
    void notificationAction(const Caller_t &caller) {
        pid_t pid;
        if((pid = fork()) == 0) {
            // search sync-ui from $PATH
            execlp("sync-ui", "sync-ui", (const char*)0);

            // Failing that, try meego-ux-settings/Sync
            execlp("meego-qml-launcher",
              "meego-qml-launcher",
              "--opengl", "--fullscreen", "--app", "meego-ux-settings",
              "--cmd", "showPage", "--cdata", "Sync", (const char*)0);

            // Failing that, simply exit
            exit(0);
        }
    }

    /** actual implementation of enable and disable */
    void setNotifications(bool enable,
                          const Caller_t &caller,
                          const string &notifications);

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
                      DBusObject_t &object) {
        startSessionWithFlags(caller, watch, server, std::vector<std::string>(), object);
    }

    /** Server.StartSessionWithFlags() */
    void startSessionWithFlags(const Caller_t &caller,
                               const boost::shared_ptr<Watch> &watch,
                               const std::string &server,
                               const std::vector<std::string> &flags,
                               DBusObject_t &object);

    /** Server.GetConfig() */
    void getConfig(const std::string &config_name,
                   bool getTemplate,
                   ReadOperations::Config_t &config)
    {
        ReadOperations ops(config_name, *this);
        ops.getConfig(getTemplate , config);
    }

    /** Server.GetReports() */
    void getReports(const std::string &config_name,
                    uint32_t start, uint32_t count,
                    ReadOperations::Reports_t &reports)
    {
        ReadOperations ops(config_name, *this);
        ops.getReports(start, count, reports);
    }

    /** Server.CheckSource() */
    void checkSource(const std::string &configName,
                     const std::string &sourceName)
    {
        ReadOperations ops(configName, *this);
        ops.checkSource(sourceName);
    }

    /** Server.GetDatabases() */
    void getDatabases(const std::string &configName,
                      const string &sourceName,
                      ReadOperations::SourceDatabases_t &databases)
    {
        ReadOperations ops(configName, *this);
        ops.getDatabases(sourceName, databases);
    }

    void getConfigs(bool getTemplates,
                    std::vector<std::string> &configNames)
    {
        ReadOperations ops("", *this);
        ops.getConfigs(getTemplates, configNames);
    }

    /** Server.CheckPresence() */
    void checkPresence(const std::string &server,
                       std::string &status,
                       std::vector<std::string> &transports);

    /** Server.GetSessions() */
    void getSessions(std::vector<DBusObject_t> &sessions);

    /** Server.InfoResponse() */
    void infoResponse(const Caller_t &caller,
                      const std::string &id,
                      const std::string &state,
                      const std::map<string, string> &response);

    friend class InfoReq;

    /** emit InfoRequest */
    void emitInfoReq(const InfoReq &);

    /** get the next id of InfoRequest */
    std::string getNextInfoReq();

    /** remove InfoReq from hash map */
    void removeInfoReq(const InfoReq &req);

    /** Server.SessionChanged */
    EmitSignal2<const DBusObject_t &,
                bool> sessionChanged;

    /** Server.PresenceChanged */
    EmitSignal3<const std::string &,
                const std::string &,
                const std::string &> presence;

    /**
     * Server.TemplatesChanged, triggered each time m_syncDevices, the
     * input for the templates, is changed
     */
    EmitSignal0 templatesChanged;

    /**
     * Server.ConfigChanged, triggered each time a session ends
     * which modified its configuration
     */
    EmitSignal0 configChanged;

    /** Server.InfoRequest */
    EmitSignal6<const std::string &,
                const DBusObject_t &,
                const std::string &,
                const std::string &,
                const std::string &,
                const std::map<string, string> &> infoRequest;

    /** Server.LogOutput */
    EmitSignal3<const DBusObject_t &,
                string,
                const std::string &> logOutput;

    friend class Session;

    PresenceStatus m_presence;
    ConnmanClient m_connman;
    NetworkManagerClient m_networkManager;

    /** manager to automatic sync */
    AutoSyncManager m_autoSync;

    //automatic termination
    AutoTerm m_autoTerm;

    //records the parent logger, dbus server acts as logger to
    //send signals to clients and put logs in the parent logger.
    LoggerBase &m_parentLogger;

    /**
     * All active timeouts created by addTimeout().
     * Each timeout which requests to be not called
     * again will be removed from this list.
     */
    list< boost::shared_ptr<Timeout> > m_timeouts;

    /**
     * called each time a timeout triggers,
     * removes those which are done
     */
    bool callTimeout(const boost::shared_ptr<Timeout> &timeout, const boost::function<bool ()> &callback);

    /** called 1 minute after last client detached from a session */
    static bool sessionExpired(const boost::shared_ptr<Session> &session);

public:
    DBusServer(GMainLoop *loop,
               bool &shutdownRequested,
               boost::shared_ptr<Restart> &restart,
               const DBusConnectionPtr &conn,
               int duration);
    ~DBusServer();

    /** access to the GMainLoop reference used by this DBusServer instance */
    GMainLoop *getLoop() { return m_loop; }

    /** process D-Bus calls until the server is ready to quit */
    void run(LogRedirect &redirect);

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

    /**
     * Special behavior for sessions: keep them around for another
     * minute after the are no longer needed. Must be called by the
     * creator of the session right before it would normally cause the
     * destruction of the session.
     *
     * This allows another client to attach and/or get information
     * about the session.
     *
     * This is implemented as a timeout which holds a reference to the
     * session. Once the timeout fires, it is called and then removed,
     * which removes the reference.
     */
    void delaySessionDestruction(const boost::shared_ptr<Session> &session);

    /**
     * Invokes the given callback once in the given amount of seconds.
     * Keeps a copy of the callback. If the DBusServer is destructed
     * before that time, then the callback will be deleted without
     * being called.
     */
    void addTimeout(const boost::function<bool ()> &callback,
                    int seconds);

    boost::shared_ptr<InfoReq> createInfoReq(const string &type,
                                             const std::map<string, string> &parameters,
                                             const Session *session);
    void autoTermRef(int counts = 1) { m_autoTerm.ref(counts); }

    void autoTermUnref(int counts = 1) { m_autoTerm.unref(counts); }

    /** callback to reset for auto termination checking */
    void autoTermCallback() { m_autoTerm.reset(); }

    /** poll_nm callback for connman, used for presence detection*/
    void connmanCallback(const std::map <std::string, boost::variant <std::vector <std::string> > >& props, const string &error);

    PresenceStatus& getPresenceStatus() {return m_presence;}

    void clearPeerTempls() { m_matchedTempls.clear(); }
    void addPeerTempl(const string &templName, const boost::shared_ptr<SyncConfig::TemplateDescription> peerTempl);

    boost::shared_ptr<SyncConfig::TemplateDescription> getPeerTempl(const string &peer);

    /**
     * methods to operate device list. See DeviceList definition.
     * The device id here is the identifier of device, the same as  definition in DeviceList.
     * In bluetooth devices, it refers to actually the mac address of the bluetooth.
     * The finger print and match mode is used to match templates.
     */
    /** get sync devices */
    void getDeviceList(SyncConfig::DeviceList &devices);
    /** get a device according to device id. If not found, return false. */
    bool getDevice(const string &deviceId, SyncConfig::DeviceDescription &device);
    /** add a device */
    void addDevice(const SyncConfig::DeviceDescription &device);
    /** remove a device by device id. If not found, do nothing */
    void removeDevice(const string &deviceId);
    /** update a device with the given device information. If not found, do nothing */
    void updateDevice(const string &deviceId, const SyncConfig::DeviceDescription &device);

    /** emit a presence signal */
    void emitPresence(const string &server, const string &status, const string &transport)
    {
        presence(server, status, transport);
    }

    /**
     * Returns new unique session ID. Implemented with a running
     * counter. Checks for overflow, but not currently for active
     * sessions.
     */
    std::string getNextSession();

    /**
     * Number of seconds to wait after file modifications are observed
     * before shutting down or restarting. Shutting down could be done
     * immediately, but restarting might not work right away. 10
     * seconds was chosen because every single package is expected to
     * be upgraded on disk in that interval. If a long-running system
     * upgrade replaces additional packages later, then the server
     * might restart multiple times during a system upgrade. Because it
     * never runs operations directly after starting, that shouldn't
     * be a problem.
     */
    static const int SHUTDOWN_QUIESENCE_SECONDS = 10;

    AutoSyncManager &getAutoSyncManager() { return m_autoSync; }

    /**
     * false if any client requested suppression of notifications
     */
    bool notificationsEnabled();

    /**
     * implement virtual method from LogStdout.
     * Not only print the message in the console
     * but also send them as signals to clients
     */
    virtual void messagev(Level level,
                          const char *prefix,
                          const char *file,
                          int line,
                          const char *function,
                          const char *format,
                          va_list args);

    virtual bool isProcessSafe() const { return false; }
};

/**
 * Tracks a single client and all sessions and connections that it is
 * connected to. Referencing them ensures that they stay around as
 * long as needed.
 */
class Client
{
    DBusServer &m_server;

    typedef std::list< boost::shared_ptr<Resource> > Resources_t;
    Resources_t m_resources;

    /** counts how often a client has called Attach() without Detach() */
    int m_attachCount;

    /** current client setting for notifications (see HAS_NOTIFY) */
    bool m_notificationsEnabled;

public:
    const Caller_t m_ID;

    Client(DBusServer &server,
           const Caller_t &ID) :
        m_server(server),
        m_attachCount(0),
        m_notificationsEnabled(true),
        m_ID(ID)
    {}
    ~Client();

    void increaseAttachCount() { ++m_attachCount; }
    void decreaseAttachCount() { --m_attachCount; }
    int getAttachCount() const { return m_attachCount; }

    void setNotificationsEnabled(bool enabled) { m_notificationsEnabled = enabled; }
    bool getNotificationsEnabled() const { return m_notificationsEnabled; }

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
    void detach(Resource *resource);

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

SE_END_CXX
namespace GDBusCXX {
template<> struct dbus_traits<SourceStatus> :
    public dbus_struct_traits<SourceStatus,
                              dbus_member<SourceStatus, std::string, &SourceStatus::m_mode,
                              dbus_member<SourceStatus, std::string, &SourceStatus::m_status,
                              dbus_member_single<SourceStatus, uint32_t, &SourceStatus::m_error> > > >
{};
}
SE_BEGIN_CXX

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

SE_END_CXX
namespace GDBusCXX {
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
}
SE_BEGIN_CXX

/**
 * This class is mainly to implement two virtual functions 'askPassword'
 * and 'savePassword' of ConfigUserInterface. The main functionality is
 * to only get and save passwords in the gnome keyring.
 */
class DBusUserInterface : public SyncContext
{
public:
    DBusUserInterface(const std::string &config);

    /*
     * Ask password from gnome keyring, if not found, empty string
     * is returned
     */
    string askPassword(const string &passwordName,
                       const string &descr,
                       const ConfigPasswordKey &key);

    //save password to gnome keyring, if not successful, false is returned.
    bool savePassword(const string &passwordName,
                      const string &password,
                      const ConfigPasswordKey &key);

    /**
     * Read stdin via InfoRequest/Response.
     */
    void readStdin(string &content);
};

/**
 * A running sync engine which keeps answering on D-Bus whenever
 * possible and updates the Session while the sync runs.
 */
class DBusSync : public DBusUserInterface
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

    virtual void reportStepCmd(sysync::uInt16 stepCmd);

    /** called when a sync is successfully started */
    virtual void syncSuccessStart();

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

    /**
     * Implement askPassword to retrieve password in gnome-keyring.
     * If not found, then ask it from dbus clients.
     */
    string askPassword(const string &passwordName,
                       const string &descr,
                       const ConfigPasswordKey &key);
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
    static const float PRO_SYNC_PREPARE_RATIO;
    /** data prepare for data items to standard unit. All are combined by profiling data */
    static const float DATA_PREPARE_RATIO;
    /** one data item send's ratio to standard unit */
    static const float ONEITEM_SEND_RATIO;
    /** one data item receive&parse's ratio to standard unit */
    static const float ONEITEM_RECEIVE_RATIO;
    /** connection setup to standard unit */
    static const float CONN_SETUP_RATIO;
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

class CmdlineWrapper;

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
    std::vector<std::string> m_flags;
    const std::string m_sessionID;
    std::string m_peerDeviceID;

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
    typedef std::map<std::string, FilterConfigNode::ConfigFilter> SourceFilters_t;
    SourceFilters_t m_sourceFilters;

    /** whether dbus clients set temporary configs */
    bool m_tempConfig;

    /**
     * whether the dbus clients updated, removed or cleared configs,
     * ignoring temporary configuration changes
     */
    bool m_setConfig;

    /**
     * True while clients are allowed to make calls other than Detach(),
     * which is always allowed. Some calls are not allowed while this
     * session runs a sync, which is indicated by a non-NULL m_sync
     * pointer.
     */
    bool m_active;

    /**
     * True once done() was called.
     */
    bool m_done;

    /**
     * Indicates whether this session was initiated by the peer or locally.
     */
    bool m_remoteInitiated;

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

    /** timer for fire status/progress usages */
    Timer m_statusTimer;
    Timer m_progressTimer;

    /** restore used */
    string m_restoreDir;
    bool m_restoreBefore;
    /** the total number of sources to be restored */
    int m_restoreSrcTotal;
    /** the number of sources that have been restored */
    int m_restoreSrcEnd;

    /**
     * status of the session
     */
    enum RunOperation {
        OP_SYNC,            /**< running a sync */
        OP_RESTORE,         /**< restoring data */
        OP_CMDLINE,         /**< executing command line */
        OP_SHUTDOWN,        /**< will shutdown server as soon as possible */
        OP_NULL             /**< idle, accepting commands via D-Bus */
    };

    static string runOpToString(RunOperation op);

    RunOperation m_runOperation;

    /** listener to listen to changes of sync */
    SessionListener *m_listener;

    /** Cmdline to execute command line args */
    boost::shared_ptr<CmdlineWrapper> m_cmdline;

    /**
     * time of latest file modification relevant for shutdown
     */
    Timespec m_shutdownLastMod;

    /**
     * timer which counts seconds until server is meant to shut down:
     * set only while the session is active and thus shutdown is allowed
     */
    Timeout m_shutdownTimer;

    /**
     * Called Server::SHUTDOWN_QUIESENCE_SECONDS after last file modification,
     * while shutdown session is active and thus ready to shut down the server.
     * Then either triggers the shutdown or restarts.
     *
     * @return always false to disable timer
     */
    bool shutdownServer();

    /** Session.Attach() */
    void attach(const Caller_t &caller);

    /** Session.Detach() */
    void detach(const Caller_t &caller);

    /** Session.GetStatus() */
    void getStatus(std::string &status,
                   uint32_t &error,
                   SourceStatuses_t &sources);
    /** Session.GetProgress() */
    void getProgress(int32_t &progress,
                     SourceProgresses_t &sources);

    /** Session.Restore() */
    void restore(const string &dir, bool before,const std::vector<std::string> &sources);

    /** Session.checkPresence() */
    void checkPresence (string &status);

    /** Session.Execute() */
    void execute(const vector<string> &args, const map<string, string> &vars);

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
    /**
     * Sessions must always be held in a shared pointer
     * because some operations depend on that. This
     * constructor function here ensures that and
     * also adds a weak pointer to the instance itself,
     * so that it can create more shared pointers as
     * needed.
     */
    static boost::shared_ptr<Session> createSession(DBusServer &server,
                                                    const std::string &peerDeviceID,
                                                    const std::string &config_name,
                                                    const std::string &session,
                                                    const std::vector<std::string> &flags = std::vector<std::string>());

    /**
     * automatically marks the session as completed before deleting it
     */
    ~Session();

    /** explicitly mark the session as completed, even if it doesn't get deleted yet */
    void done();

private:
    Session(DBusServer &server,
            const std::string &peerDeviceID,
            const std::string &config_name,
            const std::string &session,
            const std::vector<std::string> &flags = std::vector<std::string>());
    boost::weak_ptr<Session> m_me;

public:
    enum {
        PRI_CMDLINE = -10,
        PRI_DEFAULT = 0,
        PRI_CONNECTION = 10,
        PRI_AUTOSYNC = 20,
        PRI_SHUTDOWN = 256  // always higher than anything else
    };

    /**
     * Default priority is 0. Higher means less important.
     */
    void setPriority(int priority) { m_priority = priority; }
    int getPriority() const { return m_priority; }

    /**
     * Turns session into one which will shut down the server, must
     * be called before enqueing it. Will wait for a certain idle period
     * after file modifications before claiming to be ready for running
     * (see Server::SHUTDOWN_QUIESENCE_SECONDS).
     */
    void startShutdown();

    /**
     * Called by server to tell shutdown session that a file was modified.
     * Session uses that to determine when the quiesence period is over.
     */
    void shutdownFileModified();

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


    DBusServer &getServer() { return m_server; }
    std::string getConfigName() { return m_configName; }
    std::string getSessionID() const { return m_sessionID; }
    std::string getPeerDeviceID() const { return m_peerDeviceID; }

    /**
     * TRUE if the session is ready to take over control
     */
    bool readyToRun() { return (m_syncStatus != SYNC_DONE) && (m_runOperation != OP_NULL); }

    /**
     * transfer control to the session for the duration of the sync,
     * returns when the sync is done (successfully or unsuccessfully)
     */
    void run(LogRedirect &redirect);

    /**
     * called when the session is ready to run (true) or
     * lost the right to make changes (false)
     */
    void setActive(bool active);

    bool getActive() { return m_active; }

    void syncProgress(sysync::TProgressEventEnum type,
                      int32_t extra1, int32_t extra2, int32_t extra3);
    void sourceProgress(sysync::TProgressEventEnum type,
                        SyncSource &source,
                        int32_t extra1, int32_t extra2, int32_t extra3);
    string askPassword(const string &passwordName,
                       const string &descr,
                       const ConfigPasswordKey &key);

    /** Session.GetFlags() */
    std::vector<std::string> getFlags() { return m_flags; }

    /** Session.GetConfigName() */
    std::string getNormalConfigName() { return SyncConfig::normalizeConfigString(m_configName); }

    /** Session.SetConfig() */
    void setConfig(bool update, bool temporary,
                   const ReadOperations::Config_t &config);

    typedef StringMap SourceModes_t;
    /** Session.Sync() */
    void sync(const std::string &mode, const SourceModes_t &source_modes);
    /** Session.Abort() */
    void abort();
    /** Session.Suspend() */
    void suspend();

    bool isSuspend() { return m_syncStatus == SYNC_SUSPEND; }
    bool isAbort() { return m_syncStatus == SYNC_ABORT; }

    /**
     * step info for engine: whether the engine is blocked by something
     * If yes, 'waiting' will be appended as specifiers in the status string.
     * see GetStatus documentation.
     */
    void setStepInfo(bool isWaiting);

    /** sync is successfully started */
    void syncSuccessStart();

    /**
     * add a listener of the session. Old set listener is returned
     */
    SessionListener* addListener(SessionListener *listener);

    void setRemoteInitiated (bool remote) { m_remoteInitiated = remote;}
private:
    /** set m_syncFilter and m_sourceFilters to config */
    virtual bool setFilters(SyncConfig &config);
};

/**
 * a wrapper to maintain the execution of command line
 * arguments from dbus clients. It is in charge of
 * redirecting output of cmd line to logging system.
 */
class CmdlineWrapper
{
    /**
     * inherit from stream buf to redirect the output.
     * Set a log until we gets a '\n' separator since we know
     * the command line message often ends with '\n'. The reason
     * is to avoid setting less characters in one log and thus
     * sending many signals to dbus clients.
     */
    class CmdlineStreamBuf : public std::streambuf
    {
    public:
        virtual ~CmdlineStreamBuf()
        {
            //flush cached characters
            if(!m_str.empty()) {
                SE_LOG(LoggerBase::SHOW, NULL, NULL, "%s", m_str.c_str());
            }
        }
    protected:
        /**
         * inherit from std::streambuf, all characters are cached in m_str
         * until a character '\n' is reached.
         */
        virtual int_type overflow (int_type ch) {
            if(ch == '\n') {
                //don't append this character for logging system will append it
                SE_LOG(LoggerBase::SHOW, NULL, NULL, "%s", m_str.c_str());
                m_str.clear();
            } else if (ch != EOF) {
                m_str += ch;
            }
            return ch;
        }

        /** the cached output characters */
        string m_str;
    };

    /** streambuf used for m_cmdlineOutStream */
    CmdlineStreamBuf m_outStreamBuf;

    /** stream for command line out and err arguments */
    std::ostream m_cmdlineOutStream;

    /**
     * implement factory method to create DBusSync instances
     * This can check 'abort' and 'suspend' command from clients.
     */
    class DBusCmdline : public Cmdline {
        Session &m_session;
    public:
        DBusCmdline(Session &session,
                    const vector<string> &args,
                    ostream &out,
                    ostream &err)
            :Cmdline(args, out, err), m_session(session)
        {}

        SyncContext* createSyncClient() {
            return new DBusSync(m_server, m_session);
        }
    };

    /** instance to run command line arguments */
    DBusCmdline m_cmdline;

    /** environment variables passed from client */
    map<string, string> m_envVars;

public:
    /**
     * constructor to create cmdline instance.
     * Here just one stream is used and error message in
     * command line is output to this stream for it is
     * different from Logger::ERROR.
     */
    CmdlineWrapper(Session &session,
                   const vector<string> &args,
                   const map<string, string> &vars)
        : m_cmdlineOutStream(&m_outStreamBuf),
        m_cmdline(session, args, m_cmdlineOutStream, m_cmdlineOutStream),
        m_envVars(vars)
    {}

    bool parse() { return m_cmdline.parse(); }
    void run(LogRedirect &redirect)
    {
        //temporarily set environment variables and restore them after running
        list<boost::shared_ptr<ScopedEnvChange> > changes;
        BOOST_FOREACH(const StringPair &var, m_envVars) {
            changes.push_back(boost::shared_ptr<ScopedEnvChange>(new ScopedEnvChange(var.first, var.second)));
        }
        // exceptions must be handled (= printed) before returning,
        // so that our client gets the output
        try {
            if (!m_cmdline.run()) {
                SE_THROW_EXCEPTION(DBusSyncException, "command line execution failure");
            }

        } catch (...) {
            redirect.flush();
            throw;
        }
        // always forward all currently pending redirected output
        // before closing the session
        redirect.flush();
    }

    bool configWasModified() { return m_cmdline.configWasModified(); }
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

    struct SANContent {
        std::vector <string> m_syncType;
        std::vector <uint32_t> m_contentType;
        std::vector <string> m_serverURI;
    };

    /**
     * The content of a parsed SAN package to be processed via
     * connection.ready
     */
    boost::shared_ptr <SANContent> m_SANContent;
    std::string m_peerBtAddr;

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

    /*
     * When the timeout occurs, we always abort the current
     * transmission.  If it is invoked while we are not in the wait()
     * of this transport, then we remember that in m_eventTriggered
     * and return from wait() right away. The main loop is only
     * quit when the transport is waiting in it. This is a precaution
     * to not interfere with other parts of the code.
     */
    int m_timeoutSeconds;
    GLibEvent m_eventSource;
    bool m_eventTriggered;
    bool m_waiting;

    SharedBuffer m_incomingMsg;
    std::string m_incomingMsgType;

    void doWait(boost::shared_ptr<Connection> &connection);
    static gboolean timeoutCallback(gpointer transport);

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
    virtual void setTimeout(int seconds)
    {
        m_timeoutSeconds = seconds;
        m_eventSource = 0;
    }
    virtual void getReply(const char *&data, size_t &len, std::string &contentType);
};

SE_END_CXX

#endif // SYNCEVO_DBUS_SERVER_H
