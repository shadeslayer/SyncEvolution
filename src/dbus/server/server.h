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

#include <set>

#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/signals2.hpp>

#include "exceptions.h"
#include "auto-term.h"
#include "connman-client.h"
#include "network-manager-client.h"
#include "presence-status.h"
#include "timeout.h"
#include "dbus-callbacks.h"

#include <syncevo/declarations.h>
SE_BEGIN_CXX

class Resource;
class Session;
class Server;
class InfoReq;
class BluezManager;
class Restart;
class Client;
class GLibNotify;
class AutoSyncManager;

/**
 * Implements the main org.syncevolution.Server interface.
 *
 * The Server class is responsible for listening to clients and
 * spinning of sync sessions as requested by clients.
 */
class Server : public GDBusCXX::DBusObjectHelper,
               public LoggerBase
{
    GMainLoop *m_loop;
    bool &m_shutdownRequested;
    Timespec m_lastFileMod;
    boost::shared_ptr<SyncEvo::Restart> &m_restart;

    uint32_t m_lastSession;
    typedef std::list< std::pair< boost::shared_ptr<GDBusCXX::Watch>, boost::shared_ptr<Client> > > Clients_t;
    Clients_t m_clients;

    /**
     * Watch all files mapped into our address space. When
     * modifications are seen (as during a package upgrade), sets
     * m_shutdownRequested. This prevents adding new sessions and
     * prevents running already queued ones, because future sessions
     * might not be able to execute correctly without a restart. For example, a
     * sync with libsynthesis from 1.1 does not work with
     * SyncEvolution XML files from 1.2. The daemon then waits
     * for the changes to settle (see SHUTDOWN_QUIESENCE_SECONDS) and either shuts
     * down or restarts.  The latter is necessary if the daemon has
     * automatic syncing enabled in a config.
     */
    list< boost::shared_ptr<GLibNotify> > m_files;
    void fileModified();
    bool shutdown();

    /**
     * timer which counts seconds until server is meant to shut down
     */
    Timeout m_shutdownTimer;


    /**
     * The session which currently holds the main lock on the server.
     * To avoid issues with concurrent modification of data or configs,
     * only one session may make such modifications at a time. A
     * plain pointer which is reset by the session's deconstructor.
     *
     * The server doesn't hold a shared pointer to the session so
     * that it can be deleted when the last client detaches from it.
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
     *
     * The session itself needs to request this special treatment with
     * addSyncSession() and remove itself with removeSyncSession() when
     * done.
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


 public:
    // D-Bus API, also usable directly

    /** Server.GetCapabilities() */
    vector<string> getCapabilities();

    /** Server.GetVersions() */
    StringMap getVersions();

    /** Server.Attach() */
    void attachClient(const GDBusCXX::Caller_t &caller,
                      const boost::shared_ptr<GDBusCXX::Watch> &watch);

    /** Server.Detach() */
    void detachClient(const GDBusCXX::Caller_t &caller);

    /** Server.DisableNotifications() */
    void disableNotifications(const GDBusCXX::Caller_t &caller,
                              const string &notifications) {
        setNotifications(false, caller, notifications);
    }

    /** Server.EnableNotifications() */
    void enableNotifications(const GDBusCXX::Caller_t &caller,
                             const string &notifications) {
        setNotifications(true, caller, notifications);
    }

    /** Server.NotificationAction() */
    void notificationAction(const GDBusCXX::Caller_t &caller) {
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
                          const GDBusCXX::Caller_t &caller,
                          const string &notifications);

    /** Server.Connect() */
    void connect(const GDBusCXX::Caller_t &caller,
                 const boost::shared_ptr<GDBusCXX::Watch> &watch,
                 const StringMap &peer,
                 bool must_authenticate,
                 const std::string &session,
                 GDBusCXX::DBusObject_t &object);

    /** Server.StartSession() */
    void startSession(const GDBusCXX::Caller_t &caller,
                      const boost::shared_ptr<GDBusCXX::Watch> &watch,
                      const std::string &server,
                      GDBusCXX::DBusObject_t &object) {
        startSessionWithFlags(caller, watch, server, std::vector<std::string>(), object);
    }

    /** Server.StartSessionWithFlags() */
    void startSessionWithFlags(const GDBusCXX::Caller_t &caller,
                               const boost::shared_ptr<GDBusCXX::Watch> &watch,
                               const std::string &server,
                               const std::vector<std::string> &flags,
                               GDBusCXX::DBusObject_t &object);

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

    /** Server.GetConfigs() */
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
    void getSessions(std::vector<GDBusCXX::DBusObject_t> &sessions);

    /** Server.InfoResponse() */
    void infoResponse(const GDBusCXX::Caller_t &caller,
                      const std::string &id,
                      const std::string &state,
                      const std::map<string, string> &response);

    /** Server.SessionChanged */
    GDBusCXX::EmitSignal2<const GDBusCXX::DBusObject_t &,
                bool> sessionChanged;

    /** Server.PresenceChanged */
    GDBusCXX::EmitSignal3<const std::string &,
                const std::string &,
                const std::string &> presence;

    /**
     * Server.TemplatesChanged, triggered each time m_syncDevices, the
     * input for the templates, is changed
     */
    GDBusCXX::EmitSignal0 templatesChanged;

    /**
     * Server.ConfigChanged, triggered each time a session ends
     * which modified its configuration
     */
    GDBusCXX::EmitSignal0 configChanged;

    /** Server.InfoRequest */
    GDBusCXX::EmitSignal6<const std::string &,
                          const GDBusCXX::DBusObject_t &,
                          const std::string &,
                          const std::string &,
                          const std::string &,
                          const std::map<string, string> &> infoRequest;

    /** Server.LogOutput */
    GDBusCXX::EmitSignal3<const GDBusCXX::DBusObject_t &,
                          string,
                          const std::string &> logOutput;

 private:
    friend class InfoReq;

    /** emit InfoRequest */
    void emitInfoReq(const InfoReq &);

    /** get the next id of InfoRequest */
    std::string getNextInfoReq();

    /** remove InfoReq from hash map */
    void removeInfoReq(const std::string &infoReqId);

    PresenceStatus m_presence;
    ConnmanClient m_connman;
    NetworkManagerClient m_networkManager;

    /** Manager to automatic sync */
    boost::shared_ptr<AutoSyncManager> m_autoSync;

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
    Server(GMainLoop *loop,
           bool &shutdownRequested,
           boost::shared_ptr<Restart> &restart,
           const GDBusCXX::DBusConnectionPtr &conn,
           int duration);
    ~Server();

    /** access to the GMainLoop reference used by this Server instance */
    GMainLoop *getLoop() { return m_loop; }

    /** process D-Bus calls until the server is ready to quit */
    void run();

    /** true iff no work is pending */
    bool isIdle() const { return !m_activeSession && m_workQueue.empty(); }

    /** isIdle() might have changed its value, current value included */
    typedef boost::signals2::signal<void (bool isIdle)> IdleSignal_t;
    IdleSignal_t m_idleSignal;

    /**
     * More specific "config changed signal", called with normalized
     * config name as parameter. Config name is empty if all configs
     * were affected.
     */
    typedef boost::signals2::signal<void (const std::string &configName)> ConfigChangedSignal_t;
    ConfigChangedSignal_t m_configChangedSignal;

    /**
     * Called when a session starts its real work (= calls addSyncSession()).
     */
    typedef boost::signals2::signal<void (const boost::shared_ptr<Session> &)> NewSyncSessionSignal_t;
    NewSyncSessionSignal_t m_newSyncSessionSignal;

    /**
     * look up client by its ID
     */
    boost::shared_ptr<Client> findClient(const GDBusCXX::Caller_t &ID);

    /**
     * find client by its ID or create one anew
     */
    boost::shared_ptr<Client> addClient(const GDBusCXX::Caller_t &ID,
                                        const boost::shared_ptr<GDBusCXX::Watch> &watch);

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
     *
     * Has to be asynchronous because it might involve ensuring that
     * there is no running helper for this device ID, which requires
     * communicating with the helper.
     */
    void killSessionsAsync(const std::string &peerDeviceID,
                           const SimpleResult &result);

    /**
     * Remove a session from the work queue. If it is running a sync,
     * it will keep running and nothing will change. Otherwise, if it
     * is "ready" (= holds a lock on its configuration), then release
     * that lock.
     */
    void dequeue(Session *session);

    /**
     * Remember that the session is running a sync (or some other
     * important operation) and keeps a pointer to it, to prevent
     * deleting it. Currently can only called by the active sync
     * session. Will fail if all clients have detached already.
     *
     * If successful, it triggers m_newSyncSessionSignal.
     */
    void addSyncSession(Session *session);

    /**
     * Session is done, ready to be deleted again.
     */
    void removeSyncSession(Session *session);

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
     * Works for any kind of object: keep shared pointer until the
     * event loop is idle, then unref it inside. Useful for instances
     * which need to delete themselves.
     */
    template <class T> static void delayDeletion(const boost::shared_ptr<T> &t) {
        g_idle_add(delayDeletionCb<T>, new boost::shared_ptr<T>(t));
    }

    template <class T> static gboolean delayDeletionCb(gpointer userData) throw () {
        boost::shared_ptr<T> *t = static_cast<boost::shared_ptr<T> *>(userData);

        try {
            t->reset();
            delete t;
        } catch (...) {
            // Something unexpected went wrong, can only shut down.
            Exception::handle(HANDLE_EXCEPTION_FATAL);
        }

        return false;
    }

    /**
     * Handle the password request from a specific session. Ask our
     * clients, relay answer to session if it is still around at the
     * time when we get the response.
     *
     * Server does not keep a strong reference to info request,
     * caller must do that or the request will automatically be
     * deleted.
     */
    boost::shared_ptr<InfoReq> passwordRequest(const std::string &descr,
                                               const ConfigPasswordKey &key,
                                               const boost::weak_ptr<Session> &session);

    /** got response for earlier request, need to extract password and tell session */
    void passwordResponse(const StringMap &response,
                          const boost::weak_ptr<Session> &session);

    /**
     * Invokes the given callback once in the given amount of seconds.
     * Keeps a copy of the callback. If the Server is destructed
     * before that time, then the callback will be deleted without
     * being called.
     */
    void addTimeout(const boost::function<bool ()> &callback,
                    int seconds);

    /**
     * InfoReq will be added to map automatically and removed again
     * when it completes or times out. Caller is responsible for
     * calling removeInfoReq() when the request becomes obsolete
     * sooner than that.
     */
    boost::shared_ptr<InfoReq> createInfoReq(const string &type,
                                             const std::map<string, string> &parameters,
                                             const Session &session);
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

SE_END_CXX

#endif // SYNCEVO_DBUS_SERVER_H
