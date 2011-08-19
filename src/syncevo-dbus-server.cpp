/*
 * Copyright (C) 2009 Intel Corporation
 * Copyright (C) 2011 Symbio, Ville Nummela
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

struct DBusMessage;
namespace SyncEvo {
static DBusMessage *SyncEvoHandleException(DBusMessage *msg);
}
#define DBUS_CXX_EXCEPTION_HANDLER SyncEvo::SyncEvoHandleException
#include "gdbus-cxx-bridge.h"

#include <syncevo/Logging.h>
#include <syncevo/LogStdout.h>
#include <syncevo/LogRedirect.h>
#include <syncevo/util.h>
#include <syncevo/SyncContext.h>
#include <syncevo/TransportAgent.h>
#include <syncevo/SoupTransportAgent.h>
#include <syncevo/SyncSource.h>
#include <syncevo/SyncML.h>
#include <syncevo/FileConfigNode.h>
#include <syncevo/TransportAgent.h>
#include <syncevo/Cmdline.h>
#include <syncevo/GLibSupport.h>

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
#include <fstream>
#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/noncopyable.hpp>

#include <glib-object.h>
#include <glib/gi18n.h>
#ifdef USE_GNOME_KEYRING
extern "C" {
#include <gnome-keyring.h>
}
#endif

// redefining "signals" clashes with the use of that word in gtkbindings.h,
// included via notify.h
#define QT_NO_KEYWORDS

#ifdef USE_KDE_KWALLET
#include <QtCore/QCoreApplication>
#include <QtCore/QString>
#include <QtCore/QLatin1String>
#include <QtCore/QByteArray>
#include <QtCore/QDebug>
#include <QtDBus/QDBusConnection>

#include <KApplication>
#include <KAboutData>
#include <KCmdLineArgs>

#include <kwallet.h>
#endif

#include "NotificationManagerFactory.h"

using namespace GDBusCXX;
using namespace SyncEvo;

SE_BEGIN_CXX

static GMainLoop *loop = NULL;
static bool shutdownRequested = false;
static LogRedirect *redirectPtr;

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

/** initialized in main() */
static boost::shared_ptr<Restart> restart;

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
        return b_dbus_create_error(msg, ex.dbusName().c_str(), "%s", ex.what());
    } catch (const DBusCXXException &ex) {
        return b_dbus_create_error(msg, ex.getName().c_str(), "%s", ex.getMessage());
    } catch (const std::runtime_error &ex) {
        return b_dbus_create_error(msg, "org.syncevolution.Exception", "%s", ex.what());
    } catch (...) {
        return b_dbus_create_error(msg, "org.syncevolution.Exception", "unknown");
    }
}

/**
 * Utility class which makes it easier to work with g_timeout_add_seconds().
 * Instantiate this class with a specific callback. Use boost::bind()
 * to attach specific parameters to that callback. Then activate
 * the timeout. Destructing this class will automatically remove
 * the timeout and thus ensure that it doesn't trigger without
 * valid parameters.
 */
class Timeout
{
    guint m_tag;
    boost::function<bool ()> m_callback;

public:
    Timeout() :
        m_tag(0)
    {
    }

    ~Timeout()
    {
        if (m_tag) {
            g_source_remove(m_tag);
        }
    }

    /**
     * call the callback at regular intervals until it returns false
     */
    void activate(int seconds,
                  const boost::function<bool ()> &callback)
    {
        deactivate();

        m_callback = callback;
        m_tag = g_timeout_add_seconds(seconds, triggered, static_cast<gpointer>(this));
        if (!m_tag) {
            SE_THROW("g_timeout_add_seconds() failed");
        }
    }

    /**
     * stop calling the callback, drop callback
     */
    void deactivate()
    {
        if (m_tag) {
            g_source_remove(m_tag);
            m_tag = 0;
        }
        m_callback = 0;
    }

private:       
    static gboolean triggered(gpointer data)
    {
        Timeout *me = static_cast<Timeout *>(data);
        return me->m_callback();
    }
};

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

/**
 * Automatic termination and track clients
 * The dbus server will automatic terminate once it is idle in a given time.
 * If any attached clients or connections, it never terminate. 
 * Once no actives, timer is started to detect the time of idle.
 * Note that there will be less-than TERM_INTERVAL inaccuracy in seconds,
 * that's because we do check every TERM_INTERVAL seconds.
 */
class AutoTerm {
    int m_refs;
    time_t m_interval;
    guint m_checkSource;
    time_t m_lastUsed;

    /**
     * This callback is called as soon as we might have to terminate.
     * If it finds that the server has been used in the meantime, it
     * will simply set another timeout and check again later.
     */
    static gboolean checkCallback(gpointer data) {
        AutoTerm *at = static_cast<AutoTerm*>(data);
        if (!at->m_refs) {
            // currently idle, but also long enough?
            time_t now = time(NULL);
            if (at->m_lastUsed + at->m_interval <= now) {
                // yes, shut down event loop and daemon
                SE_LOG_DEBUG(NULL, NULL, "terminating because not in use and idle for more than %ld seconds", (long)at->m_interval);
                shutdownRequested = true;
                g_main_loop_quit(loop);
            } else {
                // check again later
                SE_LOG_DEBUG(NULL, NULL, "not terminating because last used %ld seconds ago, check again in %ld seconds",
                             (long)(now - at->m_lastUsed),
                             (long)(at->m_lastUsed + at->m_interval - now));
                at->m_checkSource = g_timeout_add_seconds(at->m_lastUsed + at->m_interval - now,
                                                          checkCallback,
                                                          data);
            }
        } else {
            SE_LOG_DEBUG(NULL, NULL, "not terminating, not renewing timeout because busy");
        }
        // always remove the current timeout, its job is done
        return FALSE;
    }

 public:
    /**
     * constructor
     * If interval is less than 0, it means 'unlimited' and never terminate
     */
    AutoTerm(int interval) :
        m_refs(0),
        m_checkSource(0),
        m_lastUsed(0)
    {
        if (interval <= 0) {
            m_interval = 0;
            // increasing reference counts prevents shutdown forever
            ref();
        } else {
            m_interval = interval;
        }
        reset();
    }

    ~AutoTerm()
    {
        if (m_checkSource) {
            g_source_remove(m_checkSource);
        }
    }

    //increase the actives objects
    void ref(int refs = 1) {  
        m_refs += refs; 
        reset();
    }

    //decrease the actives objects
    void unref(int refs = 1) { 
        m_refs -= refs; 
        if(m_refs <= 0) {
           m_refs = 0;
        }
        reset();
    }

    /**
     * To be called each time the server interacts with a client,
     * which includes adding or removing a client. If necessary,
     * this installs a timeout to stop the daemon when it has been
     * idle long enough.
     */
    void reset()
    {
        if (m_refs > 0) {
            // in use, don't need timeout
            if (m_checkSource) {
                SE_LOG_DEBUG(NULL, NULL, "deactivating idle termination because in use");
                g_source_remove(m_checkSource);
                m_checkSource = 0;
            }
        } else {
            // An already active timeout will trigger at the chosen time,
            // then notice that the server has been used in the meantime and
            // reset the timer. Therefore we don't have to remove it.
            m_lastUsed = time(NULL);
            if (!m_checkSource) {
                SE_LOG_DEBUG(NULL, NULL, "activating idle termination in %ld seconds because idle", m_interval);
                m_checkSource = g_timeout_add_seconds(m_interval,
                                                      checkCallback,
                                                      static_cast<gpointer>(this));
            }
        }
    }
};

class InfoReq;

/**
 * Query bluetooth devices from org.bluez
 * The basic workflow is: 
 * 1) get default adapter from bluez by calling 'DefaultAdapter' method of org.bluez.Manager
 * 2) get all devices of the adapter by calling 'ListDevices' method of org.bluez.Adapter
 * 3) iterate all devices and get properties for each one by calling 'GetProperties' method of org.bluez.Device.
 *    Then check its UUIDs whether it contains sync services and put it in the sync device list if it is
 *
 * To track changes of devices dynamically, here also listen signals from bluez:
 * org.bluez.Manager - DefaultAdapterChanged: default adapter is changed and thus have to get its devices
 *                                            and update sync device list
 * org.bluez.Adapter - DeviceCreated, DeviceRemoved: device is created or removed and device list is updated
 * org.bluez.Device - PropertyChanged: property is changed and device information is changed and tracked
 *
 * This class is to manage querying bluetooth devices from org.bluez. Also
 * it acts a proxy to org.bluez.Manager.
 */
class BluezManager : public DBusRemoteObject {
public:
    BluezManager(DBusServer &server); 

    virtual const char *getDestination() const {return "org.bluez";}
    virtual const char *getPath() const {return "/";}
    virtual const char *getInterface() const {return "org.bluez.Manager";}
    virtual DBusConnection *getConnection() const {return m_bluezConn.get();}
    bool isDone() { return m_done; }

private:
    class BluezDevice;

    /**
     * This class acts a proxy to org.bluez.Adapter. 
     * Call methods of org.bluez.Adapter and listen signals from it
     * to get devices list and track its changes
     */
    class BluezAdapter: public DBusRemoteObject
    {
     public:
        BluezAdapter (BluezManager &manager, const string &path); 

        virtual const char *getDestination() const {return "org.bluez";}
        virtual const char *getPath() const {return m_path.c_str();}
        virtual const char *getInterface() const {return "org.bluez.Adapter";}
        virtual DBusConnection *getConnection() const {return m_manager.getConnection();}
        void checkDone(bool forceDone = false)
        {
            if(forceDone || m_devReplies >= m_devNo) {
                m_devReplies = m_devNo = 0;
                m_manager.setDone(true);
            } else {
                m_manager.setDone(false);
            }
        }

        std::vector<boost::shared_ptr<BluezDevice> >& getDevices() { return m_devices; }

     private:
        /** callback of 'ListDevices' signal. Used to get all available devices of the adapter */
        void listDevicesCb(const std::vector<DBusObject_t> &devices, const string &error);

        /** callback of 'DeviceRemoved' signal. Used to track a device is removed */
        void deviceRemoved(const DBusObject_t &object);

        /** callback of 'DeviceCreated' signal. Used to track a new device is created */
        void deviceCreated(const DBusObject_t &object);

        BluezManager &m_manager;
        /** the object path of adapter */
        std::string m_path;
        /** the number of device for the default adapter */
        int m_devNo;
        /** the number of devices having reply */
        int m_devReplies;

        /** all available devices */
        std::vector<boost::shared_ptr<BluezDevice> > m_devices;

        /** represents 'DeviceRemoved' signal of org.bluez.Adapter*/
        SignalWatch1<DBusObject_t> m_deviceRemoved;
        /** represents 'DeviceAdded' signal of org.bluez.Adapter*/
        SignalWatch1<DBusObject_t> m_deviceAdded;

        friend class BluezDevice;
    };

    /**
     * This class acts a proxy to org.bluez.Device. 
     * Call methods of org.bluez.Device and listen signals from it
     * to get properties of device and track its changes
     */
    class BluezDevice: public DBusRemoteObject
    {
     public:
        typedef map<string, boost::variant<vector<string>, string > > PropDict;

        BluezDevice (BluezAdapter &adapter, const string &path);

        virtual const char *getDestination() const {return "org.bluez";}
        virtual const char *getPath() const {return m_path.c_str();}
        virtual const char *getInterface() const {return "org.bluez.Device";}
        virtual DBusConnection *getConnection() const {return m_adapter.m_manager.getConnection();}
        string getMac() { return m_mac; }

        /**
         * check whether the current device has sync service
         * if yes, put it in the adapter's sync devices list
         */
        void checkSyncService(const std::vector<std::string> &uuids);

     private:
        /** callback of 'GetProperties' method. The properties of the device is gotten */
        void getPropertiesCb(const PropDict &props, const string &error);

        /** callback of 'PropertyChanged' signal. Changed property is tracked */
        void propertyChanged(const string &name, const boost::variant<vector<string>, string> &prop);

        BluezAdapter &m_adapter;
        /** the object path of the device */
        string m_path;
        /** name of the device */
        string m_name;
        /** mac address of the device */
        string m_mac;
        /** whether the calling of 'GetProperties' is returned */
        bool m_reply;

        typedef SignalWatch2<string, boost::variant<vector<string>, string> > PropertySignal;
        /** represents 'PropertyChanged' signal of org.bluez.Device */
        PropertySignal m_propertyChanged;

        friend class BluezAdapter;
    };

    /*
     * check whether the data is generated. If errors, force initilization done
     */
    void setDone(bool done) { m_done = done; }

    /** callback of 'DefaultAdapter' method to get the default bluetooth adapter  */
    void defaultAdapterCb(const DBusObject_t &adapter, const string &error);

    /** callback of 'DefaultAdapterChanged' signal to track changes of the default adapter */
    void defaultAdapterChanged(const DBusObject_t &adapter);

    DBusServer &m_server;
    DBusConnectionPtr m_bluezConn;
    boost::shared_ptr<BluezAdapter> m_adapter;

    /** represents 'DefaultAdapterChanged' signal of org.bluez.Adapter*/
    SignalWatch1<DBusObject_t> m_adapterChanged;

    /** flag to indicate whether the calls are all returned */
    bool m_done;
};

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
    Timer(unsigned long timeoutMs = 0) : m_timeoutMs(timeoutMs)
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
        return timeout(m_timeoutMs);
    }

    /** 
     * check whether the duration timer records is longer than the given duration 
     */
    bool timeout(unsigned long timeoutMs)
    {
        timeval now;
        gettimeofday(&now, NULL);
        return duration(now, m_startTime) >= timeoutMs;
    }
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

    BluezManager m_bluezManager;

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
    DBusServer(GMainLoop *loop, const DBusConnectionPtr &conn, int duration);
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

const float ProgressData::PRO_SYNC_PREPARE_RATIO = 0.2;
const float ProgressData::DATA_PREPARE_RATIO = 0.10;
const float ProgressData::ONEITEM_SEND_RATIO = 0.05;
const float ProgressData::ONEITEM_RECEIVE_RATIO = 0.05;
const float ProgressData::CONN_SETUP_RATIO = 0.5;

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
    void run();

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
    void run()
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
            redirectPtr->flush();
            throw;
        }
        // always forward all currently pending redirected output
        // before closing the session
        redirectPtr->flush();
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

/**
 * A wrapper for handling info request and response.
 */
class InfoReq {
public:
    typedef std::map<string, string> InfoMap;

    // status of current request
    enum Status {
        ST_RUN, // request is running
        ST_OK, // ok, response is gotten
        ST_TIMEOUT, // timeout
        ST_CANCEL // request is cancelled
    };

    /**
     * constructor
     * The default timeout is 120 seconds
     */
    InfoReq(DBusServer &server,
            const string &type,
            const InfoMap &parameters,
            const Session *session,
            uint32_t timeout = 120); 

    ~InfoReq();

    /**
     * check whether the request is ready. Also give an opportunity
     * to poll the sources and then check the response is ready
     * @return the state of the request
     */
    Status check();

    /**
     * wait the response until timeout, abort or suspend. It may be blocked.
     * The response is returned though the parameter 'response' when the Status is
     * 'ST_OK'. Otherwise, corresponding statuses are returned.
     * @param response the received response if gotten
     * @param interval the interval to check abort, suspend and timeout, in seconds
     * @return the current status
     */
    Status wait(InfoMap &response, uint32_t interval = 3);

    /**
     * get response when it is ready. If false, nothing will be set in response
     */
    bool getResponse(InfoMap &response);

    /** cancel the request. If request is done, cancel won't do anything */
    void cancel();

    /** get current status in string format */
    string getStatusStr() const { return statusToString(m_status); }

private:
    static string statusToString(Status status);

    enum InfoState {
        IN_REQ,  //request
        IN_WAIT, // waiting
        IN_DONE  // done
    };

    static string infoStateToString(InfoState state);

    /** callback for the timemout source */
    static gboolean checkCallback(gpointer data);

    /** check whether the request is timeout */
    bool checkTimeout();

    friend class DBusServer;

    /** set response from dbus clients */
    void setResponse(const Caller_t &caller, const string &state, const InfoMap &response);

    /** send 'done' state if needed */
    void done();

    string getId() const { return m_id; }
    string getSessionPath() const { return m_session ? m_session->getPath() : ""; }
    string getInfoStateStr() const { return infoStateToString(m_infoState); }
    string getHandler() const { return m_handler; }
    string getType() const { return m_type; }
    const InfoMap& getParam() const { return m_param; }

    DBusServer &m_server;

    /** caller's session, might be NULL */
    const Session *m_session;

    /** unique id of this info request */
    string m_id;

    /** info req state defined in dbus api */
    InfoState m_infoState;

    /** status to indicate the info request is timeout, ok, abort, etc */
    Status m_status;

    /** the handler of the responsed dbus client */
    Caller_t m_handler;

    /** the type of the info request */
    string m_type;

    /** parameters from info request callers */
    InfoMap m_param;

    /** response returned from dbus clients */
    InfoMap m_response;

    /** default timeout is 120 seconds */
    uint32_t m_timeout;

    /** a timer */
    Timer m_timer;
};


/***************** Client implementation ****************/

Client::~Client()
{
    SE_LOG_DEBUG(NULL, NULL, "D-Bus client %s is destructing", m_ID.c_str());

    // explicitly detach all resources instead of just freeing the
    // list, so that the special behavior for sessions in detach() is
    // triggered
    while (!m_resources.empty()) {
        detach(m_resources.front().get());
    }
}

void Client::detach(Resource *resource)
{
    for (Resources_t::iterator it = m_resources.begin();
         it != m_resources.end();
         ++it) {
        if (it->get() == resource) {
            if (it->unique()) {
                boost::shared_ptr<Session> session = boost::dynamic_pointer_cast<Session>(*it);
                if (session) {
                    // give clients a chance to query the session
                    m_server.delaySessionDestruction(session);
                    // allow other sessions to start
                    session->done();
                }
            }
            // this will trigger removal of the resource if
            // the client was the last remaining owner
            m_resources.erase(it);
            return;
        }
    }

    SE_THROW_EXCEPTION(InvalidCall, "cannot detach from resource that client is not attached to");
}


/***************** ReadOperations implementation ****************/

ReadOperations::ReadOperations(const std::string &config_name, DBusServer &server) :
    m_configName(config_name), m_server(server)
{}

void ReadOperations::getConfigs(bool getTemplates, std::vector<std::string> &configNames)
{
    if (getTemplates) {
        SyncConfig::DeviceList devices;

        // get device list from dbus server, currently only bluetooth devices
        m_server.getDeviceList(devices);

        // also include server templates in search
        devices.push_back(SyncConfig::DeviceDescription("", "", SyncConfig::MATCH_FOR_CLIENT_MODE));

        //clear existing templates in dbus server
        m_server.clearPeerTempls();

        SyncConfig::TemplateList list = SyncConfig::getPeerTemplates(devices);
        std::map<std::string, int> numbers;
        BOOST_FOREACH(const boost::shared_ptr<SyncConfig::TemplateDescription> peer, list) {
            //if it is not a template for device
            if(peer->m_fingerprint.empty()) {
                configNames.push_back(peer->m_templateId);
            } else {
                string templName = "Bluetooth_";
                templName += peer->m_deviceId;
                templName += "_";
                std::map<std::string, int>::iterator it = numbers.find(peer->m_deviceId);
                if(it == numbers.end()) {
                    numbers.insert(std::make_pair(peer->m_deviceId, 1));
                    templName += "1";
                } else {
                    it->second++;
                    stringstream seq;
                    seq << it->second;
                    templName += seq.str();
                }
                configNames.push_back(templName);
                m_server.addPeerTempl(templName, peer);
            }
        }
    } else {
        SyncConfig::ConfigList list = SyncConfig::getConfigs();
        BOOST_FOREACH(const SyncConfig::ConfigList::value_type &server, list) {
            configNames.push_back(server.first);
        }
    }
}

boost::shared_ptr<DBusUserInterface> ReadOperations::getLocalConfig(const string &configName, bool mustExist)
{
    string peer, context;
    SyncConfig::splitConfigString(SyncConfig::normalizeConfigString(configName),
                                  peer, context);

    boost::shared_ptr<DBusUserInterface> syncConfig(new DBusUserInterface(configName));

    /** if config was not set temporarily */
    if (!setFilters(*syncConfig)) {
        // the default configuration can always be opened for reading,
        // everything else must exist
        if ((context != "default" || peer != "") &&
            mustExist &&
            !syncConfig->exists()) {
            SE_THROW_EXCEPTION(NoSuchConfig, "No configuration '" + configName + "' found");
        }
    }
    return syncConfig;
}

void ReadOperations::getConfig(bool getTemplate,
                               Config_t &config)
{
    map<string, string> localConfigs;
    boost::shared_ptr<SyncConfig> dbusConfig;
    boost::shared_ptr<DBusUserInterface> dbusUI;
    SyncConfig *syncConfig;
    string syncURL;
    /** get server template */
    if(getTemplate) {
        string peer, context;

        boost::shared_ptr<SyncConfig::TemplateDescription> peerTemplate =
            m_server.getPeerTempl(m_configName);
        if(peerTemplate) {
            SyncConfig::splitConfigString(SyncConfig::normalizeConfigString(peerTemplate->m_templateId),
                    peer, context);
            dbusConfig = SyncConfig::createPeerTemplate(peerTemplate->m_path);
            // if we have cached template information, add match information for it
            localConfigs.insert(pair<string, string>("description", peerTemplate->m_description));

            stringstream score;
            score << peerTemplate->m_rank;
            localConfigs.insert(pair<string, string>("score", score.str()));
            // Actually this fingerprint is transferred by getConfigs, which refers to device name
            localConfigs.insert(pair<string, string>("deviceName", peerTemplate->m_fingerprint));
            // This is the fingerprint of the template
            localConfigs.insert(pair<string, string>("fingerPrint", peerTemplate->m_matchedModel));
            // This is the template name presented to UI (or device class)
            if (!peerTemplate->m_templateName.empty()) {
                localConfigs.insert(pair<string,string>("templateName", peerTemplate->m_templateName));
            }

            // if the peer is client, then replace syncURL with bluetooth
            // MAC address
            syncURL = "obex-bt://";
            syncURL += peerTemplate->m_deviceId;
        } else {
            SyncConfig::splitConfigString(SyncConfig::normalizeConfigString(m_configName),
                    peer, context);
            dbusConfig = SyncConfig::createPeerTemplate(peer);
        }

        if(!dbusConfig.get()) {
            SE_THROW_EXCEPTION(NoSuchConfig, "No template '" + m_configName + "' found");
        }

        // use the shared properties from the right context as filter
        // so that the returned template preserves existing properties
        boost::shared_ptr<DBusUserInterface> shared = getLocalConfig(string("@") + context, false);

        ConfigProps props;
        shared->getProperties()->readProperties(props);
        dbusConfig->setConfigFilter(true, "", props);
        BOOST_FOREACH(std::string source, shared->getSyncSources()) {
            SyncSourceNodes nodes = shared->getSyncSourceNodes(source, "");
            props.clear();
            nodes.getProperties()->readProperties(props);
            // Special case "type" property: the value in the context
            // is not preserved. Every new peer must ensure that
            // its own value is compatible (= same backend) with
            // the other peers.
            props.erase("type");
            dbusConfig->setConfigFilter(false, source, props);
        }
        syncConfig = dbusConfig.get();
    } else {
        dbusUI = getLocalConfig(m_configName);
        //try to check password and read password from gnome keyring if possible
        ConfigPropertyRegistry& registry = SyncConfig::getRegistry();
        BOOST_FOREACH(const ConfigProperty *prop, registry) {
            prop->checkPassword(*dbusUI, m_configName, *dbusUI->getProperties());
        }
        list<string> configuredSources = dbusUI->getSyncSources();
        BOOST_FOREACH(const string &sourceName, configuredSources) {
            ConfigPropertyRegistry& registry = SyncSourceConfig::getRegistry();
            SyncSourceNodes sourceNodes = dbusUI->getSyncSourceNodes(sourceName);

            BOOST_FOREACH(const ConfigProperty *prop, registry) {
                prop->checkPassword(*dbusUI, m_configName, *dbusUI->getProperties(),
                        sourceName, sourceNodes.getProperties());
            }
        }
        syncConfig = dbusUI.get();
    }

    /** get sync properties and their values */
    ConfigPropertyRegistry &syncRegistry = SyncConfig::getRegistry();
    BOOST_FOREACH(const ConfigProperty *prop, syncRegistry) {
        bool isDefault = false;
        string value = prop->getProperty(*syncConfig->getProperties(), &isDefault);
        if(boost::iequals(prop->getMainName(), "syncURL") && !syncURL.empty() ) {
            localConfigs.insert(pair<string, string>(prop->getMainName(), syncURL));
        } else if(!isDefault) {
            localConfigs.insert(pair<string, string>(prop->getMainName(), value));
        }
    }

    // Set ConsumerReady for existing SyncEvolution < 1.2 configs
    // if not set explicitly,
    // because in older releases all existing configurations where
    // shown. SyncEvolution 1.2 is more strict and assumes that
    // ConsumerReady must be set explicitly. The sync-ui always has
    // set the flag for configs created or modified with it, but the
    // command line did not. Matches similar code in the Cmdline.cpp
    // migration code.
    //
    // This does not apply to templates which always have ConsumerReady
    // set explicitly (to on or off) or not set (same as off).
    if (!getTemplate &&
        syncConfig->getConfigVersion(CONFIG_LEVEL_PEER, CONFIG_CUR_VERSION) == 0 /* SyncEvolution < 1.2 */) {
        localConfigs.insert(make_pair("ConsumerReady", "1"));
    }

    // insert 'configName' of the chosen config (m_configName is not normalized)
    localConfigs.insert(pair<string, string>("configName", syncConfig->getConfigName()));

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
                localConfigs.insert(pair<string, string>(prop->getMainName(), value));
            }
        }
        config.insert(pair<string, map<string, string> >( "source/" + name, localConfigs));
    }
}

void ReadOperations::getReports(uint32_t start, uint32_t count,
                                Reports_t &reports)
{
    SyncContext client(m_configName, false);
    std::vector<string> dirs;
    client.getSessions(dirs);

    uint32_t index = 0;
    // newest report firstly
    for( int i = dirs.size() - 1; i >= 0; --i) {
        /** if start plus count is bigger than actual size, then return actual - size reports */
        if(index >= start && index - start < count) {
            const string &dir = dirs[i];
            std::map<string, string> aReport;
            // insert a 'dir' as an ID for the current report
            aReport.insert(pair<string, string>("dir", dir));
            SyncReport report;
            // peerName is also extracted from the dir 
            string peerName = client.readSessionInfo(dir,report);
            boost::shared_ptr<SyncConfig> config(new SyncConfig(m_configName));
            string storedPeerName = config->getPeerName();
            //if can't find peer name, use the peer name from the log dir
            if(!storedPeerName.empty()) {
                peerName = storedPeerName;
            }

            /** serialize report to ConfigProps and then copy them to reports */
            HashFileConfigNode node("/dev/null","",true);
            node << report;
            ConfigProps props;
            node.readProperties(props);

            BOOST_FOREACH(const ConfigProps::value_type &entry, props) {
                aReport.insert(entry);
            }
            // a new key-value pair <"peer", [peer name]> is transferred
            aReport.insert(pair<string, string>("peer", peerName));
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
        SyncSourceParams params(sourceName, config->getSyncSourceNodes(sourceName), config);
        auto_ptr<SyncSource> syncSource(SyncSource::createSource(params, false, config.get()));

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

    SyncSourceParams params(sourceName, config->getSyncSourceNodes(sourceName), config);
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

/***************** DBusUserInterface implementation   **********************/
DBusUserInterface::DBusUserInterface(const std::string &config):
    SyncContext(config, true)
{
}

inline const char *passwdStr(const std::string &str)
{
    return str.empty() ? NULL : str.c_str();
}

string DBusUserInterface::askPassword(const string &passwordName, 
                                      const string &descr, 
                                      const ConfigPasswordKey &key) 
{
    string password;
    
#ifdef USE_KDE_KWALLET
    /** here we use server sync url without protocol prefix and
     * user account name as the key in the keyring */
     /* Also since the KWallet's API supports only storing (key,passowrd)
     * or Map<QString,QString> , the former is used */
    bool isKde=true;
    #ifdef USE_GNOME_KEYRING
    //When Both GNOME KEYRING and KWALLET are available, Check if this is a KDE Session 
    //and Call
    if(getenv("KDE_FULL_SESSION"))
      isKde=false;
    #endif   
    if (isKde){
	QString walletPassword;
        QString walletKey = QString(passwdStr(key.user)) + ',' +
			    QString(passwdStr(key.domain))+ ','+
			    QString(passwdStr(key.server))+','+
			    QString(passwdStr(key.object))+','+
			    QString(passwdStr(key.protocol))+','+
			    QString(passwdStr(key.authtype))+','+
			    QString::number(key.port);
                                                          
	    
	    QString wallet_name = KWallet::Wallet::NetworkWallet();
	    //QString folder = QString::fromUtf8("Syncevolution");
	    const QLatin1String folder("Syncevolution");
    	  	      
	    if (!KWallet::Wallet::keyDoesNotExist(wallet_name, folder, walletKey)){
	    KWallet::Wallet *wallet = KWallet::Wallet::openWallet(wallet_name, -1, KWallet::Wallet::Synchronous); 
	
	    if (wallet){
	      if (wallet->setFolder(folder))                
                if (wallet->readPassword(walletKey, walletPassword) == 0)
		  return walletPassword.toStdString();
		 }    
	  }        

    }    
#endif

#ifdef USE_GNOME_KEYRING
    /** here we use server sync url without protocol prefix and
     * user account name as the key in the keyring */
    /* It is possible to let CmdlineSyncClient decide which of fields in ConfigPasswordKey it would use
     * but currently only use passed key instead */
    GnomeKeyringResult result;
    GList* list;

    result = gnome_keyring_find_network_password_sync(passwdStr(key.user),
                                                      passwdStr(key.domain),
                                                      passwdStr(key.server),
                                                      passwdStr(key.object),
                                                      passwdStr(key.protocol),
                                                      passwdStr(key.authtype),
                                                      key.port,
                                                      &list);

    /** if find password stored in gnome keyring */
    if(result == GNOME_KEYRING_RESULT_OK && list && list->data ) {
        GnomeKeyringNetworkPasswordData *key_data;
        key_data = (GnomeKeyringNetworkPasswordData*)list->data;
        password = key_data->password;
        gnome_keyring_network_password_list_free(list);
        return password;
    }
#endif


    //if not found, return empty
    return "";
}

bool DBusUserInterface::savePassword(const string &passwordName, 
                                     const string &password, 
                                     const ConfigPasswordKey &key)
{


#ifdef USE_KDE_KWALLET
        /* It is possible to let CmdlineSyncClient decide which of fields in ConfigPasswordKey it would use
         * but currently only use passed key instead */
    bool isKde=true;
    #ifdef USE_GNOME_KEYRING
    //When Both GNOME KEYRING and KWALLET are available, Check if this is a KDE Session 
    //and Call
    if(getenv("KDE_FULL_SESSION"))
      isKde=false;
    #endif
    if(isKde){
        // write password to keyring
	QString walletKey = QString(passwdStr(key.user)) + ',' +
			    QString(passwdStr(key.domain))+ ','+
			    QString(passwdStr(key.server))+','+
			    QString(passwdStr(key.object))+','+
			    QString(passwdStr(key.protocol))+','+
			    QString(passwdStr(key.authtype))+','+
			    QString::number(key.port);
	QString walletPassword = password.c_str();
	
	 bool write_success = false;
	 QString wallet_name = KWallet::Wallet::NetworkWallet();
	 //QString folder = QString::fromUtf8("Syncevolution");
	 const QLatin1String folder("Syncevolution");
	 
	 KWallet::Wallet *wallet = KWallet::Wallet::openWallet(wallet_name, -1, 
                                            KWallet::Wallet::Synchronous);
	  if (wallet){
	    if (!wallet->hasFolder(folder))
	      wallet->createFolder(folder);
	    
	    if (wallet->setFolder(folder))	      
	      if (wallet->writePassword(walletKey, walletPassword) == 0)
                write_success = true;            
	    
	}
	
        if(!write_success) {
            SyncContext::throwError("Try to save " + passwordName + " in kde-wallet but got an error. ");
        } 
        
    return write_success;
    }
#endif

#ifdef USE_GNOME_KEYRING
    /* It is possible to let CmdlineSyncClient decide which of fields in ConfigPasswordKey it would use
     * but currently only use passed key instead */
    guint32 itemId;
    GnomeKeyringResult result;
    // write password to keyring
    result = gnome_keyring_set_network_password_sync(NULL,
                                                     passwdStr(key.user),
                                                     passwdStr(key.domain),
                                                     passwdStr(key.server),
                                                     passwdStr(key.object),
                                                     passwdStr(key.protocol),
                                                     passwdStr(key.authtype),
                                                     key.port,
                                                     password.c_str(),
                                                     &itemId);
    /* if set operation is failed */
    if(result != GNOME_KEYRING_RESULT_OK) {
#ifdef GNOME_KEYRING_220
        SyncContext::throwError("Try to save " + passwordName + " in gnome-keyring but get an error. " + gnome_keyring_result_to_message(result));
#else
        /** if gnome-keyring version is below 2.20, it doesn't support 'gnome_keyring_result_to_message'. */
        stringstream value;
        value << (int)result;
        SyncContext::throwError("Try to save " + passwordName + " in gnome-keyring but get an error. The gnome-keyring error code is " + value.str() + ".");
#endif
    } 
    return true;
#else
#endif

    /** if no support of gnome-keyring, don't save anything */
    return false;
}

void DBusUserInterface::readStdin(string &content)
{
    throwError("reading stdin in D-Bus server not supported, use --daemon=no in command line");
}

/***************** DBusSync implementation **********************/

DBusSync::DBusSync(const std::string &config,
                   Session &session) :
    DBusUserInterface(config),
    m_session(session)
{
    #ifdef USE_KDE_KWALLET
    //QCoreApplication *app;
    //if (!qApp) {
        //int argc = 1;
        //app = new QCoreApplication(argc, (char *[1]){ (char*) "syncevolution"});
    //}
    int argc = 1;
    static const char *prog = "syncevolution";
    static char *argv[] = { (char *)&prog, NULL };
    //if (!qApp) {
        //new QCoreApplication(argc, argv);
    //}
    KAboutData aboutData(// The program name used internally.
                         "syncevolution",
                         // The message catalog name
                         // If null, program name is used instead.
                         0,
                         // A displayable program name string.
                         ki18n("Syncevolution"),
                         // The program version string.
                         "1.0",
                         // Short description of what the app does.
                         ki18n("Lets Akonadi synchronize with a SyncML Peer"),
                         // The license this code is released under
                         KAboutData::License_GPL,
                         // Copyright Statement
                         ki18n("(c) 2010"),
                         // Optional text shown in the About box.
                         // Can contain any information desired.
                         ki18n(""),
                         // The program homepage string.
                         "http://www.syncevolution.org/",
                         // The bug report email address
                         "syncevolution@syncevolution.org");

    KCmdLineArgs::init(argc, argv, &aboutData);
    if (!kapp) {
        new KApplication;
        //To stop KApplication from spawning it's own DBus Service ... Will have to patch KApplication about this
        QDBusConnection::sessionBus().unregisterService("org.syncevolution.syncevolution-"+QString::number(getpid()));
    }
    #endif
  
}

boost::shared_ptr<TransportAgent> DBusSync::createTransportAgent()
{
    if (m_session.useStubConnection()) {
        // use the D-Bus Connection to send and receive messages
        boost::shared_ptr<TransportAgent> agent(new DBusTransportAgent(m_session.getServer().getLoop(),
                                                                       m_session,
                                                                       m_session.getStubConnection()));
        // We don't know whether we'll run as client or server.
        // But we as we cannot resend messages via D-Bus even if running as
        // client (API not designed for it), let's use the hard timeout
        // from RetryDuration here.
        int timeout = getRetryDuration();
        agent->setTimeout(timeout);
        return agent;
    } else {
        // no connection, use HTTP via libsoup/GMainLoop
        GMainLoop *loop = m_session.getServer().getLoop();
        boost::shared_ptr<TransportAgent> agent = SyncContext::createTransportAgent(loop);
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

void DBusSync::reportStepCmd(sysync::uInt16 stepCmd)
{
    switch(stepCmd) {
        case sysync::STEPCMD_SENDDATA:
        case sysync::STEPCMD_RESENDDATA:
        case sysync::STEPCMD_NEEDDATA:
            //sending or waiting data
            m_session.setStepInfo(true);
            break;
        default:
            // otherwise, processing
            m_session.setStepInfo(false);
            break;
    }
}

void DBusSync::syncSuccessStart()
{
    m_session.syncSuccessStart();
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

string DBusSync::askPassword(const string &passwordName, 
                             const string &descr, 
                             const ConfigPasswordKey &key) 
{
    string password = DBusUserInterface::askPassword(passwordName, descr, key);

    if(password.empty()) {
        password = m_session.askPassword(passwordName, descr, key);
    }
    return password;
}

/***************** Session implementation ***********************/

void Session::attach(const Caller_t &caller)
{
    boost::shared_ptr<Client> client(m_server.findClient(caller));
    if (!client) {
        throw runtime_error("unknown client");
    }
    boost::shared_ptr<Session> me = m_me.lock();
    if (!me) {
        throw runtime_error("session already deleted?!");
    }
    client->attach(me);
}

void Session::detach(const Caller_t &caller)
{
    boost::shared_ptr<Client> client(m_server.findClient(caller));
    if (!client) {
        throw runtime_error("unknown client");
    }
    client->detach(this);
}

/**
 * validate key/value property and copy it to the filter
 * if okay
 */
static void copyProperty(const StringPair &keyvalue,
                         ConfigPropertyRegistry &registry,
                         FilterConfigNode::ConfigFilter &filter)
{
    const std::string &name = keyvalue.first;
    const std::string &value = keyvalue.second;
    const ConfigProperty *prop = registry.find(name);
    if (!prop) {
        SE_THROW_EXCEPTION(InvalidCall, StringPrintf("unknown property '%s'", name.c_str()));
    }
    std::string error;
    if (!prop->checkValue(value, error)) {
        SE_THROW_EXCEPTION(InvalidCall, StringPrintf("invalid value '%s' for property '%s': '%s'",
                                                     value.c_str(), name.c_str(), error.c_str()));
    }
    filter.insert(keyvalue);
}                        

static void setSyncFilters(const ReadOperations::Config_t &config,FilterConfigNode::ConfigFilter &syncFilter,std::map<std::string, FilterConfigNode::ConfigFilter> &sourceFilters)
{
    ReadOperations::Config_t::const_iterator it;
    for (it = config.begin(); it != config.end(); it++) {
        map<string, string>::const_iterator sit;
        string name = it->first;
        if (name.empty()) {
            ConfigPropertyRegistry &registry = SyncConfig::getRegistry();
            for (sit = it->second.begin(); sit != it->second.end(); sit++) {
                // read-only properties can (and have to be) ignored
                static const char *init[] = {
                    "configName",
                    "description",
                    "score",
                    "deviceName",
                    "templateName",
                    "fingerprint"
                };
                static const set< std::string, Nocase<std::string> >
                    special(init,
                            init + (sizeof(init) / sizeof(*init)));
                if (special.find(sit->first) == special.end()) {
                    copyProperty(*sit, registry, syncFilter);
                }
            }
        } else if (boost::starts_with(name, "source/")) {
            name = name.substr(strlen("source/"));
            FilterConfigNode::ConfigFilter &sourceFilter = sourceFilters[name];
            ConfigPropertyRegistry &registry = SyncSourceConfig::getRegistry();
            for (sit = it->second.begin(); sit != it->second.end(); sit++) {
                copyProperty(*sit, registry, sourceFilter);
            }
        } else {
            SE_THROW_EXCEPTION(InvalidCall, StringPrintf("invalid config entry '%s'", name.c_str()));
        }
    }
}
void Session::setConfig(bool update, bool temporary,
                        const ReadOperations::Config_t &config)
{
    if (!m_active) {
        SE_THROW_EXCEPTION(InvalidCall, "session is not active, call not allowed at this time");
    }
    if (m_runOperation != OP_NULL) {
        string msg = StringPrintf("%s started, cannot change configuration at this time", runOpToString(m_runOperation).c_str());
        SE_THROW_EXCEPTION(InvalidCall, msg);
    }

    m_server.getPresenceStatus().updateConfigPeers (m_configName, config);
    /** check whether we need remove the entire configuration */
    if(!update && !temporary && config.empty()) {
        boost::shared_ptr<SyncConfig> syncConfig(new SyncConfig(getConfigName()));
        if(syncConfig.get()) {
            syncConfig->remove();
            m_setConfig = true;
        }
        return;
    }

    /*
     * validate input config and convert to filters;
     * if validation fails, no harm was done at this point yet
     */
    FilterConfigNode::ConfigFilter syncFilter;
    SourceFilters_t sourceFilters;
    setSyncFilters(config, syncFilter, sourceFilters);

    if (temporary) {
        /* save temporary configs in session filters, either erasing old
           temporary settings or adding to them */
        if (update) {
            m_syncFilter.insert(syncFilter.begin(), syncFilter.end());
            BOOST_FOREACH(SourceFilters_t::value_type &source, sourceFilters) {
                SourceFilters_t::iterator it = m_sourceFilters.find(source.first);
                if (it != m_sourceFilters.end()) {
                    // add to existing source filter
                    it->second.insert(source.second.begin(), source.second.end());
                } else {
                    // add source filter
                    m_sourceFilters.insert(source);
                }
            }
        } else {
            m_syncFilter = syncFilter;
            m_sourceFilters = sourceFilters;            
        }
        m_tempConfig = true;
    } else {
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
        syncConfig->prepareConfigForWrite();
        syncConfig->copy(*from, NULL);

        syncConfig->preFlush(*syncConfig);
        syncConfig->flush();
        m_setConfig = true;
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
    if (m_runOperation == OP_SYNC) {
        string msg = StringPrintf("%s started, cannot start again", runOpToString(m_runOperation).c_str());
        SE_THROW_EXCEPTION(InvalidCall, msg);
    } else if (m_runOperation != OP_NULL) {
        string msg = StringPrintf("%s started, cannot start sync", runOpToString(m_runOperation).c_str());
        SE_THROW_EXCEPTION(InvalidCall, msg);
    }

    m_sync.reset(new DBusSync(getConfigName(), *this));
    m_sync->setServerAlerted(m_serverAlerted);
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

    if (m_remoteInitiated) {
        m_sync->setRemoteInitiated (true);
    }

    // Apply temporary config filters. The parameters of this function
    // override the source filters, if set.
    m_sync->setConfigFilter(true, "", m_syncFilter);
    FilterConfigNode::ConfigFilter filter;
    filter = m_sourceFilter;
    if (!mode.empty()) {
        filter["sync"] = mode;
    }
    m_sync->setConfigFilter(false, "", filter);
    BOOST_FOREACH(const std::string &source,
                  m_sync->getSyncSources()) {
        filter = m_sourceFilters[source];
        SourceModes_t::const_iterator it = source_modes.find(source);
        if (it != source_modes.end()) {
            filter["sync"] = it->second;
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
    m_runOperation = OP_SYNC;

    // now that we have a DBusSync object, return from the main loop
    // and once that is done, transfer control to that object
    g_main_loop_quit(loop);
}

void Session::abort()
{
    if (m_runOperation != OP_SYNC && m_runOperation != OP_CMDLINE) {
        SE_THROW_EXCEPTION(InvalidCall, "sync not started, cannot abort at this time");
    }
    m_syncStatus = SYNC_ABORT;
    fireStatus(true);

    // state change, return to caller so that it can react
    g_main_loop_quit(m_server.getLoop());
}

void Session::suspend()
{
    if (m_runOperation != OP_SYNC && m_runOperation != OP_CMDLINE) {
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
    if (m_stepIsWaiting) {
        status += ";waiting";
    }

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

boost::shared_ptr<Session> Session::createSession(DBusServer &server,
                                                  const std::string &peerDeviceID,
                                                  const std::string &config_name,
                                                  const std::string &session,
                                                  const std::vector<std::string> &flags)
{
    boost::shared_ptr<Session> me(new Session(server, peerDeviceID, config_name, session, flags));
    me->m_me = me;
    return me;
}

Session::Session(DBusServer &server,
                 const std::string &peerDeviceID,
                 const std::string &config_name,
                 const std::string &session,
                 const std::vector<std::string> &flags) :
    DBusObjectHelper(server.getConnection(),
                     std::string("/org/syncevolution/Session/") + session,
                     "org.syncevolution.Session",
                     boost::bind(&DBusServer::autoTermCallback, &server)),
    ReadOperations(config_name, server),
    m_server(server),
    m_flags(flags),
    m_sessionID(session),
    m_peerDeviceID(peerDeviceID),
    m_serverMode(false),
    m_useConnection(false),
    m_tempConfig(false),
    m_setConfig(false),
    m_active(false),
    m_done(false),
    m_remoteInitiated(false),
    m_syncStatus(SYNC_QUEUEING),
    m_stepIsWaiting(false),
    m_priority(PRI_DEFAULT),
    m_progress(0),
    m_progData(m_progress),
    m_error(0),
    m_statusTimer(100),
    m_progressTimer(50),
    m_restoreBefore(true),
    m_restoreSrcTotal(0),
    m_restoreSrcEnd(0),
    m_runOperation(OP_NULL),
    m_listener(NULL),
    emitStatus(*this, "StatusChanged"),
    emitProgress(*this, "ProgressChanged")
{
    add(this, &Session::attach, "Attach");
    add(this, &Session::detach, "Detach");
    add(this, &Session::getFlags, "GetFlags");
    add(this, &Session::getNormalConfigName, "GetConfigName");
    add(static_cast<ReadOperations *>(this), &ReadOperations::getConfigs, "GetConfigs");
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
    add(this, &Session::restore, "Restore");
    add(this, &Session::checkPresence, "checkPresence");
    add(this, &Session::execute, "Execute");
    add(emitStatus);
    add(emitProgress);

    SE_LOG_DEBUG(NULL, NULL, "session %s created", getPath());
}

void Session::done()
{
    if (m_done) {
        return;
    }
    SE_LOG_DEBUG(NULL, NULL, "session %s done", getPath());

    /* update auto sync manager when a config is changed */
    if (m_setConfig) {
        m_server.getAutoSyncManager().update(m_configName);
    }
    m_server.dequeue(this);

    // now tell other clients about config change?
    if (m_setConfig) {
        m_server.configChanged();
    }

    // typically set by m_server.dequeue(), but let's really make sure...
    m_active = false;

    m_done = true;
}

Session::~Session()
{
    SE_LOG_DEBUG(NULL, NULL, "session %s deconstructing", getPath());
    done();
}

void Session::startShutdown()
{
    m_runOperation = OP_SHUTDOWN;
}

void Session::shutdownFileModified()
{
    m_shutdownLastMod = Timespec::monotonic();
    SE_LOG_DEBUG(NULL, NULL, "file modified at %lu.%09lus, %s",
                 (unsigned long)m_shutdownLastMod.tv_sec,
                 (unsigned long)m_shutdownLastMod.tv_nsec,
                 m_active ? "active" : "not active");

    if (m_active) {
        // (re)set shutdown timer: once it fires, we are ready to shut down;
        // brute-force approach, will reset timer many times
        m_shutdownTimer.activate(DBusServer::SHUTDOWN_QUIESENCE_SECONDS,
                                 boost::bind(&Session::shutdownServer, this));
    }
}

bool Session::shutdownServer()
{
    Timespec now = Timespec::monotonic();
    bool autosync = m_server.getAutoSyncManager().hasTask() ||
        m_server.getAutoSyncManager().hasAutoConfigs();
    SE_LOG_DEBUG(NULL, NULL, "shut down server at %lu.%09lu because of file modifications, auto sync %s",
                 now.tv_sec, now.tv_nsec,
                 autosync ? "on" : "off");
    if (autosync) {
        // suitable exec() call which restarts the server using the same environment it was in
        // when it was started
        restart->restart();
    } else {
        // leave server now
        shutdownRequested = true;
        g_main_loop_quit(loop);
        SE_LOG_INFO(NULL, NULL, "server shutting down because files loaded into memory were modified on disk");
    }

    return false;
}

void Session::setActive(bool active)
{
    bool oldActive = m_active;
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

        if (!oldActive &&
            m_runOperation == OP_SHUTDOWN) {
            // shutdown session activated: check if or when we can shut down
            if (m_shutdownLastMod) {
                Timespec now = Timespec::monotonic();
                SE_LOG_DEBUG(NULL, NULL, "latest file modified at %lu.%09lus, now is %lu.%09lus",
                             (unsigned long)m_shutdownLastMod.tv_sec,
                             (unsigned long)m_shutdownLastMod.tv_nsec,
                             (unsigned long)now.tv_sec,
                             (unsigned long)now.tv_nsec);
                if (m_shutdownLastMod + DBusServer::SHUTDOWN_QUIESENCE_SECONDS <= now) {
                    // ready to shutdown immediately
                    shutdownServer();
                } else {
                    // need to wait
                    int secs = DBusServer::SHUTDOWN_QUIESENCE_SECONDS -
                        (now - m_shutdownLastMod).tv_sec;
                    SE_LOG_DEBUG(NULL, NULL, "shut down in %ds", secs);
                    m_shutdownTimer.activate(secs, boost::bind(&Session::shutdownServer, this));
                }
            }
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
    switch(m_runOperation) {
    case OP_SYNC: {
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
        break;
    }
    case OP_RESTORE: {
        switch(type) {
        case sysync::PEV_ALERTED:
            // count the total number of sources to be restored
            m_restoreSrcTotal++;
            break;
        case sysync::PEV_SYNCSTART: {
            if (source.getFinalSyncMode() != SYNC_NONE) {
                SourceStatus &status = m_sourceStatus[source.getName()];
                // set statuses as 'restore-from-backup'
                status.set(PrettyPrintSyncMode(source.getFinalSyncMode()), "running", 0);
                fireStatus(true);
            }
            break;
        }
        case sysync::PEV_SYNCEND: {
            if (source.getFinalSyncMode() != SYNC_NONE) {
                m_restoreSrcEnd++;
                SourceStatus &status = m_sourceStatus[source.getName()];
                status.set(PrettyPrintSyncMode(source.getFinalSyncMode()), "done", 0);
                m_progress = 100 * m_restoreSrcEnd / m_restoreSrcTotal;
                fireStatus(true);
                fireProgress(true);
            }
            break;
        }
        default:
            break;
        }
        break;
    }
    default:
        break;
    }
}

void Session::run()
{
    if (m_runOperation != OP_NULL) {
        try {
            m_syncStatus = SYNC_RUNNING;
            fireStatus(true);
            switch(m_runOperation) {
            case OP_SYNC: {
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
                // report 'sync done' event to listener
                if(m_listener) {
                    m_listener->syncDone(status);
                }
                break;
            }
            case OP_RESTORE:
                m_sync->restore(m_restoreDir, 
                                m_restoreBefore ? SyncContext::DATABASE_BEFORE_SYNC : SyncContext::DATABASE_AFTER_SYNC);
                break;
            case OP_CMDLINE:
                try {
                    m_cmdline->run();
                } catch (...) {
                    SyncMLStatus status = Exception::handle();
                    if (!m_error) {
                        m_error = status;
                    }
                }
                m_setConfig = m_cmdline->configWasModified();
                break;
            case OP_SHUTDOWN:
                // block until time for shutdown or restart if no
                // shutdown requested already
                if (!shutdownRequested) {
                    g_main_loop_run(loop);
                }
                break;
            default:
                break;
            };
        } catch (...) {
            // we must enter SYNC_DONE under all circumstances,
            // even when failing during connection shutdown
            m_syncStatus = SYNC_DONE;
            m_stepIsWaiting = false;
            fireStatus(true);
            throw;
        }
        m_syncStatus = SYNC_DONE;
        m_stepIsWaiting = false;
        fireStatus(true);
    } 
}

bool Session::setFilters(SyncConfig &config)
{
    /** apply temporary configs to config */
    config.setConfigFilter(true, "", m_syncFilter);
    // set all sources in the filter to config
    BOOST_FOREACH(const SourceFilters_t::value_type &value, m_sourceFilters) {
        config.setConfigFilter(false, value.first, value.second);
    }
    return m_tempConfig;
}

void Session::setStepInfo(bool isWaiting)
{
    // if stepInfo doesn't change, then ignore it to avoid duplicate status info
    if(m_stepIsWaiting != isWaiting) {
        m_stepIsWaiting = isWaiting;
        fireStatus(true);
    }
}

void Session::restore(const string &dir, bool before, const std::vector<std::string> &sources)
{
    if (!m_active) {
        SE_THROW_EXCEPTION(InvalidCall, "session is not active, call not allowed at this time");
    }
    if (m_runOperation == OP_RESTORE) {
        string msg = StringPrintf("restore started, cannot restore again");
        SE_THROW_EXCEPTION(InvalidCall, msg);
    } else if (m_runOperation != OP_NULL) {
        // actually this never happen currently, for during the real restore process, 
        // it never poll the sources in default main context 
        string msg = StringPrintf("%s started, cannot restore", runOpToString(m_runOperation).c_str());
        SE_THROW_EXCEPTION(InvalidCall, msg);
    }

    m_sync.reset(new DBusSync(getConfigName(), *this));

    if(!sources.empty()) {
        BOOST_FOREACH(const std::string &source, sources) {
            FilterConfigNode::ConfigFilter filter;
            filter["sync"] = "two-way";
            m_sync->setConfigFilter(false, source, filter);
        }
        // disable other sources
        FilterConfigNode::ConfigFilter disabled;
        disabled["sync"] = "disabled";
        m_sync->setConfigFilter(false, "", disabled);
    }
    m_restoreBefore = before;
    m_restoreDir = dir;
    m_runOperation = OP_RESTORE;

    // initiate status and progress and sourceProgress is not calculated currently
    BOOST_FOREACH(const std::string source,
                  m_sync->getSyncSources()) {
        m_sourceStatus[source];
    }
    fireProgress(true);
    fireStatus(true);

    g_main_loop_quit(loop);
}

string Session::runOpToString(RunOperation op)
{
    switch(op) {
    case OP_SYNC:
        return "sync";
    case OP_RESTORE:
        return "restore";
    case OP_CMDLINE:
        return "cmdline";
    default:
        return "";
    };
}

void Session::execute(const vector<string> &args, const map<string, string> &vars)
{
    if (!m_active) {
        SE_THROW_EXCEPTION(InvalidCall, "session is not active, call not allowed at this time");
    }
    if (m_runOperation == OP_CMDLINE) {
        SE_THROW_EXCEPTION(InvalidCall, "cmdline started, cannot start again");
    } else if (m_runOperation != OP_NULL) {
        string msg = StringPrintf("%s started, cannot start cmdline", runOpToString(m_runOperation).c_str());
        SE_THROW_EXCEPTION(InvalidCall, msg);
    }
    //create ostream with a specified streambuf
    m_cmdline.reset(new CmdlineWrapper(*this, args, vars));

    if(!m_cmdline->parse()) {
        m_cmdline.reset();
        SE_THROW_EXCEPTION(DBusSyncException, "arguments parsing error");
    }

    m_runOperation = OP_CMDLINE;
    g_main_loop_quit(loop);
}

inline void insertPair(std::map<string, string> &params,
                       const string &key, 
                       const string &value)
{
    if(!value.empty()) {
        params.insert(pair<string, string>(key, value));
    }
}

string Session::askPassword(const string &passwordName, 
                             const string &descr, 
                             const ConfigPasswordKey &key) 
{
    std::map<string, string> params;
    insertPair(params, "description", descr);
    insertPair(params, "user", key.user);
    insertPair(params, "SyncML server", key.server);
    insertPair(params, "domain", key.domain);
    insertPair(params, "object", key.object);
    insertPair(params, "protocol", key.protocol);
    insertPair(params, "authtype", key.authtype);
    insertPair(params, "port", key.port ? StringPrintf("%u",key.port) : "");
    boost::shared_ptr<InfoReq> req = m_server.createInfoReq("password", params, this);
    std::map<string, string> response;
    if(req->wait(response) == InfoReq::ST_OK) {
        std::map<string, string>::iterator it = response.find("password");
        if (it == response.end()) {
            SE_THROW_EXCEPTION_STATUS(StatusException, "user didn't provide password, abort", SyncMLStatus(sysync::LOCERR_USERABORT));
        } else {
            return it->second;
        }
    } 

    SE_THROW_EXCEPTION_STATUS(StatusException, "can't get the password from clients. The password request is '" + req->getStatusStr() + "'", STATUS_PASSWORD_TIMEOUT);
    return "";
}

/*Implementation of Session.CheckPresence */
void Session::checkPresence (string &status)
{
    vector<string> transport;
    m_server.m_presence.checkPresence (m_configName, status, transport);
}

void Session::syncSuccessStart()
{
    // if listener, report 'sync started' to it
    if(m_listener) {
        m_listener->syncSuccessStart();
    }
}

SessionListener* Session::addListener(SessionListener *listener)
{
    SessionListener *old = m_listener;
    m_listener = listener;
    return old;
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
            m_session->setStubConnectionError(reason);
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
            bool serverAlerted = false;
            // check message type, determine whether we act
            // as client or server, choose config
            if (message_type == "HTTP Config") {
                // type used for testing, payload is config name
                config.assign(reinterpret_cast<const char *>(message.second),
                              message.first);
            } else if (message_type == TransportAgent::m_contentTypeServerAlertedNotificationDS) {
                serverAlerted = true;
            	sysync::SanPackage san;
            	if (san.PassSan(const_cast<uint8_t *>(message.second), message.first, 2) || san.GetHeader()) {
                    // We are very tolerant regarding the content of the message.
                    // If it doesn't parse, try to do something useful anyway.
                    // only for SAN 1.2, for SAN 1.0/1.1 we can not be sure
                    // whether it is a SAN package or a normal sync pacakge
                    if (message_type == TransportAgent::m_contentTypeServerAlertedNotificationDS) {
                        config = "default";
                        SE_LOG_DEBUG(NULL, NULL, "SAN parsing failed, falling back to 'default' config");
                    }  
            	} else { //Server alerted notification case
                    // Extract server ID and match it against a server
                    // configuration.  Multiple different peers might use the
                    // same serverID ("PC Suite"), so check properties of the
                    // of our configs first before going back to the name itself.
                    std::string serverID = san.fServerID;
                    SyncConfig::ConfigList servers = SyncConfig::getConfigs();
                    BOOST_FOREACH(const SyncConfig::ConfigList::value_type &server,
                            servers) {
                        SyncConfig conf(server.first);
                        vector<string> urls = conf.getSyncURL();
                        BOOST_FOREACH (const string &url, urls) {
                            if (url == serverID) {
                                config = server.first;
                                break;
                            }
                        }
                        if (!config.empty()) {
                            break;
                        }
                    }

                    // for Bluetooth transports match against mac address.
                    StringMap::const_iterator id = m_peer.find("id"),
                        trans = m_peer.find("transport");
                    if (trans != m_peer.end() && id != m_peer.end()) {
                        if (trans->second == "org.openobex.obexd") {
                            m_peerBtAddr = id->second.substr(0, id->second.find("+"));
                            BOOST_FOREACH(const SyncConfig::ConfigList::value_type &server,
                                    servers) {
                                SyncConfig conf(server.first);
                                vector<string> urls = conf.getSyncURL();
                                BOOST_FOREACH (string &url, urls){
                                    url = url.substr (0, url.find("+"));
                                    SE_LOG_DEBUG (NULL, NULL, "matching against %s",url.c_str());
                                    if (url.find ("obex-bt://") ==0 && url.substr(strlen("obex-bt://"), url.npos) == m_peerBtAddr) {
                                        config = server.first;
                                        break;
                                    } 
                                }
                                if (!config.empty()){
                                    break;
                                }
                            }
                        }
                    }

                    if (config.empty()) {
                        BOOST_FOREACH(const SyncConfig::ConfigList::value_type &server,
                                      servers) {
                            if (server.first == serverID) {
                                config = serverID;
                                break;
                            }
                        }
                    }

                    // create a default configuration name if none matched
                    if (config.empty()) {
                        config = serverID+"_"+getCurrentTime();
                        SE_LOG_DEBUG(NULL,
                                     NULL,
                                     "SAN Server ID '%s' unknown, falling back to automatically created '%s' config",
                                     serverID.c_str(), config.c_str());
                    }


                    SE_LOG_DEBUG(NULL, NULL, "SAN sync with config %s", config.c_str());

                    m_SANContent.reset (new SANContent ());
                    // extract number of sources
                    int numSources = san.fNSync;
                    int syncType;
                    uint32_t contentType;
                    std::string serverURI;
                    if (!numSources) {
                        SE_LOG_DEBUG(NULL, NULL, "SAN message with no sources, using selected modes");
                        // Synchronize all known sources with the default mode.
                        if (san.GetNthSync(0, syncType, contentType, serverURI)) {
                            SE_LOG_DEBUG(NULL, NULL, "SAN invalid header, using default modes");
                        } else if (syncType < SYNC_FIRST || syncType > SYNC_LAST) {
                            SE_LOG_DEBUG(NULL, NULL, "SAN invalid sync type %d, using default modes", syncType);
                        } else {
                            m_syncMode = PrettyPrintSyncMode(SyncMode(syncType), true);
                            SE_LOG_DEBUG(NULL, NULL, "SAN sync mode for all configured sources: %s", m_syncMode.c_str());
                        }
                    } else {
                        for (int sync = 1; sync <= numSources; sync++) {
                            if (san.GetNthSync(sync, syncType, contentType, serverURI)) {
                                SE_LOG_DEBUG(NULL, NULL, "SAN invalid sync entry #%d", sync);
                            } else if (syncType < SYNC_FIRST || syncType > SYNC_LAST) {
                                SE_LOG_DEBUG(NULL, NULL, "SAN invalid sync type %d for entry #%d, ignoring entry", syncType, sync);
                            } else {
                                std::string syncMode = PrettyPrintSyncMode(SyncMode(syncType), true);
                                m_SANContent->m_syncType.push_back (syncMode);
                                m_SANContent->m_serverURI.push_back (serverURI);
                                m_SANContent->m_contentType.push_back (contentType);
                            }
                        }
                    }
                }
                // TODO: use the session ID set by the server if non-null
            } else if (// relaxed checking for XML: ignore stuff like "; CHARSET=UTF-8"
                       message_type.substr(0, message_type.find(';')) == TransportAgent::m_contentTypeSyncML ||
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
                BOOST_FOREACH(const SyncConfig::ConfigList::value_type &entry,
                              SyncConfig::getConfigs()) {
                    SyncConfig peer(entry.first);
                    if (info.m_deviceID == peer.getRemoteDevID()) {
                        config = entry.first;
                        SE_LOG_INFO(NULL, NULL, "matched %s against config %s (%s)",
                                    info.toString().c_str(),
                                    entry.first.c_str(),
                                    entry.second.c_str());
                        // Stop searching. Other peer configs might have the same remoteDevID.
                        // We go with the first one found, which because of the sort order
                        // of getConfigs() ensures that "foo" is found before "foo.old".
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
                throw runtime_error(StringPrintf("message type '%s' not supported for starting a sync", message_type.c_str()));
            }

            // run session as client or server
            m_state = PROCESSING;
            m_session = Session::createSession(m_server,
                                               peerDeviceID,
                                               config,
                                               m_sessionID);
            if (serverMode) {
                m_session->initServer(SharedBuffer(reinterpret_cast<const char *>(message.second),
                                                   message.first),
                                      message_type);
            }
            m_session->setServerAlerted(serverAlerted);
            m_session->setPriority(Session::PRI_CONNECTION);
            m_session->setStubConnection(myself);
            // this will be reset only when the connection shuts down okay
            // or overwritten with the error given to us in
            // Connection::close()
            m_session->setStubConnectionError("closed prematurely");
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
            m_session->setStubConnectionError(err);
        }
        failed(err);
    } else {
        m_state = DONE;
        if (m_session) {
            m_session->setStubConnectionError("");
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
                     "org.syncevolution.Connection",
                     boost::bind(&DBusServer::autoTermCallback, &server)),
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
    m_server.autoTermRef();
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
    m_server.autoTermUnref();
}

void Connection::ready()
{
    //if configuration not yet created
    std::string configName = m_session->getConfigName();
    SyncConfig config (configName);
    if (!config.exists() && m_SANContent) {
        SE_LOG_DEBUG (NULL, NULL, "Configuration %s not exists for a runnable session in a SAN context, create it automatically", configName.c_str());
        ReadOperations::Config_t from;
        const std::string templateName = "SyncEvolution";
        // TODO: support SAN from other well known servers
        ReadOperations ops(templateName, m_server);
        ops.getConfig(true , from);
        if (!m_peerBtAddr.empty()){
            from[""]["SyncURL"] = string ("obex-bt://") + m_peerBtAddr;
        }
        m_session->setConfig (false, false, from);
    }
    const SyncContext context (configName);
    std::list<std::string> sources = context.getSyncSources();

    if (m_SANContent && !m_SANContent->m_syncType.empty()) {
        // check what the server wants us to synchronize
        // and only synchronize that
        m_syncMode = "disabled";
        for (size_t sync=0; sync<m_SANContent->m_syncType.size(); sync++) {
            std::string syncMode = m_SANContent->m_syncType[sync];
            std::string serverURI = m_SANContent->m_serverURI[sync];
            //uint32_t contentType = m_SANContent->m_contentType[sync];
            bool found = false;
            BOOST_FOREACH(const std::string &source, sources) {
                boost::shared_ptr<const PersistentSyncSourceConfig> sourceConfig(context.getSyncSourceConfig(source));
                // prefix match because the local
                // configuration might contain
                // additional parameters (like date
                // range selection for events)
                if (boost::starts_with(sourceConfig->getURINonEmpty(), serverURI)) {
                    SE_LOG_DEBUG(NULL, NULL,
                                 "SAN entry #%d = source %s with mode %s",
                                 (int)sync, source.c_str(), syncMode.c_str());
                    m_sourceModes[source] = syncMode;
                    found = true;
                    break;
                }
            }
            if (!found) {
                SE_LOG_DEBUG(NULL, NULL,
                             "SAN entry #%d with mode %s ignored because Server URI %s is unknown",
                             (int)sync, syncMode.c_str(), serverURI.c_str());
            }
        }
        if (m_sourceModes.empty()) {
            SE_LOG_DEBUG(NULL, NULL,
                    "SAN message with no known entries, falling back to default");
            m_syncMode = "";
        }
    }

    if (m_SANContent) {
        m_session->setRemoteInitiated(true);
    }
    // proceed with sync now that our session is ready
    m_session->sync(m_syncMode, m_sourceModes);
}

/****************** DBusTransportAgent implementation **************/

DBusTransportAgent::DBusTransportAgent(GMainLoop *loop,
                                       Session &session,
                                       boost::weak_ptr<Connection> connection) :
    m_loop(loop),
    m_session(session),
    m_connection(connection),
    m_timeoutSeconds(0),
    m_eventTriggered(false),
    m_waiting(false)
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

    if (m_timeoutSeconds) {
        m_eventSource = g_timeout_add_seconds(m_timeoutSeconds, timeoutCallback, static_cast<gpointer>(this));
    }
    m_eventTriggered = false;

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

gboolean DBusTransportAgent::timeoutCallback(gpointer transport)
{
    DBusTransportAgent *me = static_cast<DBusTransportAgent *>(transport);
    me->m_eventTriggered = true;
    if (me->m_waiting) {
        g_main_loop_quit(me->m_loop);
    }
    return false;
}

void DBusTransportAgent::doWait(boost::shared_ptr<Connection> &connection)
{
    // let Connection wake us up when it has a reply or
    // when it closes down
    connection->m_loop = m_loop;

    // release our reference so that the Connection instance can
    // be destructed when requested by the D-Bus peer
    connection.reset();

    // now wait
    m_waiting = true;
    g_main_loop_run(m_loop);
    m_waiting = false;
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
        if (m_eventTriggered) {
            return TIME_OUT;
        }
        doWait(connection);

        // if the connection is still available, then keep waiting
        connection = m_connection.lock();
        if (connection) {
            return ACTIVE;
        } else if (m_session.getStubConnectionError().empty()) {
            return INACTIVE;
        } else {
            SE_THROW_EXCEPTION(TransportException, m_session.getStubConnectionError());
            return FAILED;
        }
        break;
    case Connection::WAITING:
        if (noReply) {
            // message is sent as far as we know, so return
            return INACTIVE;
        }

        if (m_eventTriggered) {
            return TIME_OUT;
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

/********************* PresenceStatus implementation ****************/
void PresenceStatus::init(){
    //initialize the configured peer list
    if (!m_initiated) {
        SyncConfig::ConfigList list = SyncConfig::getConfigs();
        BOOST_FOREACH(const SyncConfig::ConfigList::value_type &server, list) {
            SyncConfig config (server.first);
            vector<string> urls = config.getSyncURL();
            m_peers[server.first].clear();
            BOOST_FOREACH (const string &url, urls) {
                // take current status into account,
                // PresenceStatus::checkPresence() calls init() and
                // expects up-to-date information
                PeerStatus status;
                if ((boost::starts_with(url, "obex-bt") && m_btPresence) ||
                    (boost::starts_with (url, "http") && m_httpPresence) ||
                    boost::starts_with (url, "local")) {
                    status = MIGHTWORK;
                } else {
                    status = NOTRANSPORT;
                }
                m_peers[server.first].push_back(make_pair(url, status));
            }
        }
        m_initiated = true;
    }
}

/* Implement DBusServer::checkPresence*/
void PresenceStatus::checkPresence (const string &peer, string& status, std::vector<std::string> &transport) {

    if (!m_initiated) {
        //might triggered by updateConfigPeers
        init();
    }

    string peerName = SyncConfig::normalizeConfigString (peer);
    vector< pair<string, PeerStatus> > mytransports = m_peers[peerName];
    if (mytransports.empty()) {
        //wrong config name?
        status = status2string(NOTRANSPORT);
        transport.clear();
        return;
    }
    PeerStatus mystatus = MIGHTWORK;
    transport.clear();
    //only if all transports are unavailable can we declare the peer
    //status as unavailable
    BOOST_FOREACH (PeerStatusPair &mytransport, mytransports) {
        if (mytransport.second == MIGHTWORK) {
            transport.push_back (mytransport.first);
        }
    }
    if (transport.empty()) {
        mystatus = NOTRANSPORT;
    }
    status = status2string(mystatus);
}

void PresenceStatus::updateConfigPeers (const std::string &peer, const ReadOperations::Config_t &config) {
    ReadOperations::Config_t::const_iterator iter = config.find ("");
    if (iter != config.end()) {
        //As a simple approach, just reinitialize the whole STATUSMAP
        //it will cause later updatePresenceStatus resend all signals
        //and a reload in checkPresence
        m_initiated = false;
    }
}

void PresenceStatus::updatePresenceStatus (bool newStatus, PresenceStatus::TransportType type) {
    if (type == PresenceStatus::HTTP_TRANSPORT) {
        updatePresenceStatus (newStatus, m_btPresence);
    } else if (type == PresenceStatus::BT_TRANSPORT) {
        updatePresenceStatus (m_httpPresence, newStatus);
    }else {
    }
}

void PresenceStatus::updatePresenceStatus (bool httpPresence, bool btPresence) {
    bool httpChanged = (m_httpPresence != httpPresence);
    bool btChanged = (m_btPresence != btPresence);
    if(httpChanged) {
        m_httpTimer.reset();
    }
    if(btChanged) {
        m_btTimer.reset();
    }

    if (m_initiated && !httpChanged && !btChanged) {
        //nothing changed
        return;
    }

    //initialize the configured peer list using old presence status
    bool initiated = m_initiated;
    if (!m_initiated) {
        init();
    }

    // switch to new status
    m_httpPresence = httpPresence;
    m_btPresence = btPresence;

    //iterate all configured peers and fire singals
    BOOST_FOREACH (StatusPair &peer, m_peers) {
        //iterate all possible transports
        //TODO One peer might got more than one signals, avoid this
        std::vector<pair<string, PeerStatus> > &transports = peer.second;
        BOOST_FOREACH (PeerStatusPair &entry, transports) {
            string url = entry.first;
            if (boost::starts_with (url, "http") && (httpChanged || !initiated)) {
                entry.second = m_httpPresence ? MIGHTWORK: NOTRANSPORT;
                m_server.emitPresence (peer.first, status2string (entry.second), entry.first);
                SE_LOG_DEBUG(NULL, NULL,
                        "http presence signal %s,%s,%s",
                        peer.first.c_str(),
                        status2string (entry.second).c_str(), entry.first.c_str());
            } else if (boost::starts_with (url, "obex-bt") && (btChanged || !initiated)) {
                entry.second = m_btPresence ? MIGHTWORK: NOTRANSPORT;
                m_server.emitPresence (peer.first, status2string (entry.second), entry.first);
                SE_LOG_DEBUG(NULL, NULL,
                        "bluetooth presence signal %s,%s,%s",
                        peer.first.c_str(),
                        status2string (entry.second).c_str(), entry.first.c_str());
            } else if (boost::starts_with (url, "local") && !initiated) {
                m_server.emitPresence (peer.first, status2string (MIGHTWORK), entry.first);
                SE_LOG_DEBUG(NULL, NULL,
                        "local presence signal %s,%s,%s",
                        peer.first.c_str(),
                        status2string (MIGHTWORK).c_str(), entry.first.c_str());
            }
        }
    }
}

/********************** Connman Client implementation **************/
ConnmanClient::ConnmanClient(DBusServer &server):
    m_server(server),
    m_propertyChanged(*this, "PropertyChanged")
{
    const char *connmanTest = getenv ("DBUS_TEST_CONNMAN");
    m_connmanConn = b_dbus_setup_bus (connmanTest ? DBUS_BUS_SESSION: DBUS_BUS_SYSTEM, NULL, true, NULL);
    if (m_connmanConn){
        typedef std::map <std::string, boost::variant <std::vector <std::string> > > PropDict;
        DBusClientCall1<PropDict>  getProp(*this,"GetProperties");
        getProp (boost::bind(&ConnmanClient::getPropCb, this, _1, _2));
        m_propertyChanged.activate(boost::bind(&ConnmanClient::propertyChanged, this, _1, _2));
    }else{
        SE_LOG_ERROR (NULL, NULL, "DBus connection setup for connman failed");
    }
}

void ConnmanClient::getPropCb (const std::map <std::string,
                               boost::variant <std::vector <std::string> > >& props, const string &error){
    if (!error.empty()) {
        if (error == "org.freedesktop.DBus.Error.ServiceUnknown") {
            // ensure there is still first set of singal set in case of no
            // connman available
            m_server.getPresenceStatus().updatePresenceStatus (true, true);
            SE_LOG_DEBUG (NULL, NULL, "No connman service available %s", error.c_str());
            return;
        }
        SE_LOG_DEBUG (NULL, NULL, "error in connmanCallback %s", error.c_str());
        return;
    }

    typedef std::pair <std::string, boost::variant <std::vector <std::string> > > element;
    bool httpPresence = false, btPresence = false;
    BOOST_FOREACH (element entry, props) {
        //match connected for HTTP based peers (wifi/wimax/ethernet)
        if (entry.first == "ConnectedTechnologies") {
            std::vector <std::string> connected = boost::get <std::vector <std::string> > (entry.second);
            BOOST_FOREACH (std::string tech, connected) {
                if (boost::iequals (tech, "wifi") || boost::iequals (tech, "ethernet") 
                || boost::iequals (tech, "wimax")) {
                    httpPresence = true;
                    break;
                }
            }
        } else if (entry.first == "AvailableTechnologies") {
            std::vector <std::string> enabled = boost::get <std::vector <std::string> > (entry.second);
            BOOST_FOREACH (std::string tech, enabled){
                if (boost::iequals (tech, "bluetooth")) {
                    btPresence = true;
                    break;
                }
            }
        } else {
            continue;
        }
    }
    //now delivering the signals
    m_server.getPresenceStatus().updatePresenceStatus (httpPresence, btPresence);
}

void ConnmanClient::propertyChanged(const string &name,
                                    const boost::variant<vector<string>, string> &prop)
{
    bool httpPresence=false, btPresence=false;
    bool httpChanged=false, btChanged=false;
    if (boost::iequals(name, "ConnectedTechnologies")) {
        httpChanged=true;
        vector<string> connected = boost::get<vector<string> >(prop);
        BOOST_FOREACH (std::string tech, connected) {
            if (boost::iequals (tech, "wifi") || boost::iequals (tech, "ethernet") 
                    || boost::iequals (tech, "wimax")) {
                httpPresence=true;
                break;
            }
        }
    } else if (boost::iequals (name, "AvailableTechnologies")){
        btChanged=true;
        vector<string> enabled = boost::get<vector<string> >(prop);
        BOOST_FOREACH (std::string tech, enabled){
            if (boost::iequals (tech, "bluetooth")) {
                btPresence = true;
                break;
            }
        }
    }
    if(httpChanged) {
        m_server.getPresenceStatus().updatePresenceStatus (httpPresence, PresenceStatus::HTTP_TRANSPORT);
    } else if (btChanged) {
        m_server.getPresenceStatus().updatePresenceStatus (btPresence, PresenceStatus::BT_TRANSPORT);
    } else {
    }
}

/***************** NetworkManagerClient implementation *************/
NetworkManagerClient::NetworkManagerClient(DBusServer &server) :
    m_server(server),
    m_stateChanged(*this, "StateChanged"),
    m_properties(*this)
{
    m_networkManagerConn = b_dbus_setup_bus(DBUS_BUS_SYSTEM, NULL, true, NULL);
    if(m_networkManagerConn) {
        m_properties.get();
        m_stateChanged.activate(boost::bind(
                                    &NetworkManagerClient::stateChanged,
                                    this, _1));
    } else {
        SE_LOG_ERROR(NULL, NULL,
                     "DBus connection setup for NetworkManager failed");
    }
}

void NetworkManagerClient::stateChanged(uint32_t uiState)
{
    switch (uiState) {
    case NM_STATE_ASLEEP:
    case NM_STATE_DISCONNECTED:
    case NM_STATE_DISCONNECTING:
    case NM_STATE_CONNECTING:
    case NM_STATE_ASLEEP_DEPRECATED:
    case NM_STATE_CONNECTING_DEPRECATED:
    case NM_STATE_DISCONNECTED_DEPRECATED:
        SE_LOG_DEBUG(NULL, NULL, "NetworkManager disconnected");
        m_server.getPresenceStatus().updatePresenceStatus(
            false, PresenceStatus::HTTP_TRANSPORT);
        break;

    default:
        SE_LOG_DEBUG(NULL, NULL, "NetworkManager connected");
        m_server.getPresenceStatus().updatePresenceStatus(
            true, PresenceStatus::HTTP_TRANSPORT);
    }
}

NetworkManagerClient::NetworkManagerProperties::NetworkManagerProperties(
    NetworkManagerClient& manager) :
    m_manager(manager)
{

}

void NetworkManagerClient::NetworkManagerProperties::get()
{
    DBusClientCall1<boost::variant<uint32_t, std::string> > get(*this, "Get");
    get(std::string(m_manager.getInterface()), std::string("State"),
        boost::bind(&NetworkManagerProperties::getCallback, this, _1, _2));    
}

void NetworkManagerClient::NetworkManagerProperties::getCallback(
    const boost::variant<uint32_t, std::string> &prop,
    const std::string &error)
{
    if(!error.empty()) {
        SE_LOG_DEBUG (
            NULL, NULL,
            "Error in calling Get of Interface "
            "org.freedesktop.DBus.Properties : %s", error.c_str());
    } else {
        m_manager.stateChanged(boost::get<uint32_t>(prop));
    }
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
            autoTermUnref(it->second->getAttachCount());
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

vector<string> DBusServer::getCapabilities()
{
    // Note that this is tested by test-dbus.py in
    // TestDBusServer.testCapabilities, update the test when adding
    // capabilities.
    vector<string> capabilities;

    capabilities.push_back("ConfigChanged");
    capabilities.push_back("GetConfigName");
    capabilities.push_back("Notifications");
    capabilities.push_back("Version");
    capabilities.push_back("SessionFlags");
    capabilities.push_back("SessionAttach");
    capabilities.push_back("DatabaseProperties");
    return capabilities;
}

StringMap DBusServer::getVersions()
{
    StringMap versions;

    versions["version"] = VERSION;
    versions["system"] = EDSAbiWrapperInfo();
    versions["backends"] = SyncSource::backendsInfo();
    return versions;
}

void DBusServer::attachClient(const Caller_t &caller,
                              const boost::shared_ptr<Watch> &watch)
{
    boost::shared_ptr<Client> client = addClient(getConnection(),
                                                 caller,
                                                 watch);
    autoTermRef();
    client->increaseAttachCount();
}

void DBusServer::detachClient(const Caller_t &caller)
{
    boost::shared_ptr<Client> client = findClient(caller);
    if (client) {
        autoTermUnref();
        client->decreaseAttachCount();
    }
}

void DBusServer::setNotifications(bool enabled,
                                  const Caller_t &caller,
                                  const string & /* notifications */)
{
    boost::shared_ptr<Client> client = findClient(caller);
    if (client && client->getAttachCount()) {
        client->setNotificationsEnabled(enabled);
    } else {
        SE_THROW("client not attached, not allowed to change notifications");
    }
}

bool DBusServer::notificationsEnabled()
{
    for(Clients_t::iterator it = m_clients.begin();
        it != m_clients.end();
        ++it) {
        if (!it->second->getNotificationsEnabled()) {
            return false;
        }
    }
    return true;
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

void DBusServer::startSessionWithFlags(const Caller_t &caller,
                                       const boost::shared_ptr<Watch> &watch,
                                       const std::string &server,
                                       const std::vector<std::string> &flags,
                                       DBusObject_t &object)
{
    boost::shared_ptr<Client> client = addClient(getConnection(),
                                                 caller,
                                                 watch);
    std::string new_session = getNextSession();   
    boost::shared_ptr<Session> session = Session::createSession(*this,
                                                                "is this a client or server session?",
                                                                server,
                                                                new_session,
                                                                flags);
    client->attach(session);
    session->activate();
    enqueue(session);
    object = session->getPath();
}

void DBusServer::checkPresence(const std::string &server,
                               std::string &status,
                               std::vector<std::string> &transports)
{
    return m_presence.checkPresence(server, status, transports);
}

void DBusServer::getSessions(std::vector<DBusObject_t> &sessions)
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

DBusServer::DBusServer(GMainLoop *loop, const DBusConnectionPtr &conn, int duration) :
    DBusObjectHelper(conn.get(), 
                     "/org/syncevolution/Server", 
                     "org.syncevolution.Server", 
                     boost::bind(&DBusServer::autoTermCallback, this)),
    m_loop(loop),
    m_lastSession(time(NULL)),
    m_activeSession(NULL),
    m_lastInfoReq(0),
    m_bluezManager(*this),
    sessionChanged(*this, "SessionChanged"),
    presence(*this, "Presence"),
    templatesChanged(*this, "TemplatesChanged"),
    configChanged(*this, "ConfigChanged"),
    infoRequest(*this, "InfoRequest"),
    logOutput(*this, "LogOutput"),
    m_presence(*this),
    m_connman(*this),
    m_networkManager(*this),
    m_autoSync(*this),
    m_autoTerm(m_autoSync.preventTerm() ? -1 : duration), //if there is any task in auto sync, prevent auto termination
    m_parentLogger(LoggerBase::instance())
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    srand(tv.tv_usec);
    add(this, &DBusServer::getCapabilities, "GetCapabilities");
    add(this, &DBusServer::getVersions, "GetVersions");
    add(this, &DBusServer::attachClient, "Attach");
    add(this, &DBusServer::detachClient, "Detach");
    add(this, &DBusServer::enableNotifications, "EnableNotifications");
    add(this, &DBusServer::disableNotifications, "DisableNotifications");
    add(this, &DBusServer::notificationAction, "NotificationAction");
    add(this, &DBusServer::connect, "Connect");
    add(this, &DBusServer::startSession, "StartSession");
    add(this, &DBusServer::startSessionWithFlags, "StartSessionWithFlags");
    add(this, &DBusServer::getConfigs, "GetConfigs");
    add(this, &DBusServer::getConfig, "GetConfig");
    add(this, &DBusServer::getReports, "GetReports");
    add(this, &DBusServer::checkSource, "CheckSource");
    add(this, &DBusServer::getDatabases, "GetDatabases");
    add(this, &DBusServer::checkPresence, "CheckPresence");
    add(this, &DBusServer::getSessions, "GetSessions");
    add(this, &DBusServer::infoResponse, "InfoResponse");
    add(sessionChanged);
    add(templatesChanged);
    add(configChanged);
    add(presence);
    add(infoRequest);
    add(logOutput);

    LoggerBase::pushLogger(this);
    setLevel(LoggerBase::DEBUG);

    if (!m_connman.isAvailable() &&
        !m_networkManager.isAvailable()) {
        // assume that we are online if no network manager was found at all
        getPresenceStatus().updatePresenceStatus(true, true);
    }
}

DBusServer::~DBusServer()
{
    // make sure all other objects are gone before destructing ourselves
    m_syncSession.reset();
    m_workQueue.clear();
    m_clients.clear();
    LoggerBase::popLogger();
}

void DBusServer::fileModified()
{
    if (!m_shutdownSession) {
        string newSession = getNextSession();
        vector<string> flags;
        flags.push_back("no-sync");
        m_shutdownSession = Session::createSession(*this,
                                                   "",  "",
                                                   newSession,
                                                   flags);
        m_shutdownSession->setPriority(Session::PRI_AUTOSYNC);
        m_shutdownSession->startShutdown();
        enqueue(m_shutdownSession);
    }

    m_shutdownSession->shutdownFileModified();
}

void DBusServer::run()
{
    // This has the intended side effect that it loads everything into
    // memory which might be dynamically loadable, like backend
    // plugins.
    StringMap map = getVersions();
    SE_LOG_DEBUG(NULL, NULL, "D-Bus server ready to run, versions:");
    BOOST_FOREACH(const StringPair &entry, map) {
        SE_LOG_DEBUG(NULL, NULL, "%s: %s", entry.first.c_str(), entry.second.c_str());
    }

    // Now that everything is loaded, check memory map for files which we have to monitor.
    set<string> files;
    ifstream in("/proc/self/maps");
    while (!in.eof()) {
        string line;
        getline(in, line);
        size_t off = line.find('/');
        if (off != line.npos &&
            line.find(" r-xp ") != line.npos) {
            files.insert(line.substr(off));
        }
    }
    in.close();
    BOOST_FOREACH(const string &file, files) {
        try {
            SE_LOG_DEBUG(NULL, NULL, "watching: %s", file.c_str());
            boost::shared_ptr<GLibNotify> notify(new GLibNotify(file.c_str(), boost::bind(&DBusServer::fileModified, this)));
            m_files.push_back(notify);
        } catch (...) {
            // ignore errors for indidividual files
            Exception::handle();
        }
    }

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

        if (!shutdownRequested && m_autoSync.hasTask()) {
            // if there is at least one pending task and no session is created for auto sync,
            // pick one task and create a session
            m_autoSync.startTask();
        }
        // Make sure check whether m_activeSession is owned by autosync 
        // Otherwise activeSession is owned by AutoSyncManager but it never
        // be ready to run. Because methods of Session, like 'sync', are able to be
        // called when it is active.  
        if (!shutdownRequested && m_autoSync.hasActiveSession())
        {
            // if the autosync is the active session, then invoke 'sync'
            // to make it ready to run
            m_autoSync.prepare();
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
    client.reset(new Client(*this, ID));
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
            boost::shared_ptr<Connection> c = session->getStubConnection().lock();
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
            //if the active session is changed, give a chance to quit the main loop
            //and make it ready to run if it is owned by AutoSyncManager.
            //Otherwise, server might be blocked.
            g_main_loop_quit(m_loop);
            return;
        }
    }
}

bool DBusServer::sessionExpired(const boost::shared_ptr<Session> &session)
{
    SE_LOG_DEBUG(NULL, NULL, "session %s expired",
                 session->getSessionID().c_str());
    // don't call me again
    return false;
}

void DBusServer::delaySessionDestruction(const boost::shared_ptr<Session> &session)
{
    SE_LOG_DEBUG(NULL, NULL, "delaying destruction of session %s by one minute",
                 session->getSessionID().c_str());
    addTimeout(boost::bind(&DBusServer::sessionExpired,
                           session),
               60 /* 1 minute */);
}

bool DBusServer::callTimeout(const boost::shared_ptr<Timeout> &timeout, const boost::function<bool ()> &callback)
{
    if (!callback()) {
        m_timeouts.remove(timeout);
        return false;
    } else {
        return true;
    }
}

void DBusServer::addTimeout(const boost::function<bool ()> &callback,
                            int seconds)
{
    boost::shared_ptr<Timeout> timeout(new Timeout);
    m_timeouts.push_back(timeout);
    timeout->activate(seconds,
                      boost::bind(&DBusServer::callTimeout,
                                  this,
                                  // avoid copying the shared pointer here,
                                  // otherwise the Timeout will never be deleted
                                  boost::ref(m_timeouts.back()),
                                  callback));
}

void DBusServer::infoResponse(const Caller_t &caller,
                              const std::string &id,
                              const std::string &state,
                              const std::map<string, string> &response)
{
    InfoReqMap::iterator it = m_infoReqMap.find(id);
    // if not found, ignore
    if(it != m_infoReqMap.end()) {
        boost::shared_ptr<InfoReq> infoReq = it->second.lock();
        infoReq->setResponse(caller, state, response);
    }
}

boost::shared_ptr<InfoReq> DBusServer::createInfoReq(const string &type,
                                                     const std::map<string, string> &parameters,
                                                     const Session *session)
{
    boost::shared_ptr<InfoReq> infoReq(new InfoReq(*this, type, parameters, session)); 
    boost::weak_ptr<InfoReq> item(infoReq) ;
    m_infoReqMap.insert(pair<string, boost::weak_ptr<InfoReq> >(infoReq->getId(), item));
    return infoReq;
}

std::string DBusServer::getNextInfoReq()
{
    return StringPrintf("%u", ++m_lastInfoReq);
}

void DBusServer::emitInfoReq(const InfoReq &req)
{
    infoRequest(req.getId(), 
                req.getSessionPath(), 
                req.getInfoStateStr(), 
                req.getHandler(), 
                req.getType(), 
                req.getParam());
}

void DBusServer::removeInfoReq(const InfoReq &req)
{
    // remove InfoRequest from hash map
    InfoReqMap::iterator it = m_infoReqMap.find(req.getId());
    if(it != m_infoReqMap.end()) {
        m_infoReqMap.erase(it);
    }
}

void DBusServer::getDeviceList(SyncConfig::DeviceList &devices)
{
    //wait bluez or other device managers
    while(!m_bluezManager.isDone()) {
        g_main_loop_run(m_loop);
    }

    devices.clear();
    devices = m_syncDevices;
}

void DBusServer::addPeerTempl(const string &templName, 
                              const boost::shared_ptr<SyncConfig::TemplateDescription> peerTempl)
{
    std::string lower = templName;
    boost::to_lower(lower);
    m_matchedTempls.insert(MatchedTemplates::value_type(lower, peerTempl));
}

boost::shared_ptr<SyncConfig::TemplateDescription> DBusServer::getPeerTempl(const string &peer)
{
    std::string lower = peer;
    boost::to_lower(lower);
    MatchedTemplates::iterator it = m_matchedTempls.find(lower);
    if(it != m_matchedTempls.end()) {
        return it->second;
    } else {
        return boost::shared_ptr<SyncConfig::TemplateDescription>();
    }
}

bool DBusServer::getDevice(const string &deviceId, SyncConfig::DeviceDescription &device)
{
    SyncConfig::DeviceList::iterator syncDevIt;
    for(syncDevIt = m_syncDevices.begin(); syncDevIt != m_syncDevices.end(); ++syncDevIt) {
        if(boost::equals(syncDevIt->m_deviceId, deviceId)) {
            device = *syncDevIt;
            return true;
        }
    }
    return false;
}

void DBusServer::addDevice(const SyncConfig::DeviceDescription &device)
{
    SyncConfig::DeviceList::iterator it;
    for(it = m_syncDevices.begin(); it != m_syncDevices.end(); ++it) {
        if(boost::iequals(it->m_deviceId, device.m_deviceId)) {
            break;
        }
    }
    if(it == m_syncDevices.end()) {
        m_syncDevices.push_back(device);
        templatesChanged();
    }
}

void DBusServer::removeDevice(const string &deviceId)
{
    SyncConfig::DeviceList::iterator syncDevIt;
    for(syncDevIt = m_syncDevices.begin(); syncDevIt != m_syncDevices.end(); ++syncDevIt) {
        if(boost::equals(syncDevIt->m_deviceId, deviceId)) {
            m_syncDevices.erase(syncDevIt);
            templatesChanged();
            break;
        }
    }
}

void DBusServer::updateDevice(const string &deviceId,
                              const SyncConfig::DeviceDescription &device)
{
    SyncConfig::DeviceList::iterator it;
    for(it = m_syncDevices.begin(); it != m_syncDevices.end(); ++it) {
        if(boost::iequals(it->m_deviceId, deviceId)) {
            (*it) = device; 
            templatesChanged();
            break;
        }
    }
}

void DBusServer::messagev(Level level,
                          const char *prefix,
                          const char *file,
                          int line,
                          const char *function,
                          const char *format,
                          va_list args)
{
    // iterating over args in messagev() is destructive, must make a copy first
    va_list argsCopy;
    va_copy(argsCopy, args);
    m_parentLogger.messagev(level, prefix, file, line, function, format, args);
    string log = StringPrintfV(format, argsCopy);
    va_end(argsCopy);

    // prefix is used to set session path
    // for general server output, the object path field is dbus server 
    // the object path can't be empty for object paths prevent using empty string.
    string strLevel = Logger::levelToStr(level);
    if(m_activeSession) {
        logOutput(m_activeSession->getPath(), strLevel, log);
    } else {
        logOutput(getPath(), strLevel, log);
    }
}

/********************** InfoReq implementation ******************/
InfoReq::InfoReq(DBusServer &server,
                 const string &type,
                 const InfoMap &parameters,
                 const Session *session,
                 uint32_t timeout) :
    m_server(server), m_session(session), m_infoState(IN_REQ),
    m_status(ST_RUN), m_type(type), m_param(parameters), 
    m_timeout(timeout), m_timer(m_timeout * 1000)
{
    m_id = m_server.getNextInfoReq();
    m_server.emitInfoReq(*this);
    m_param.clear();
}

InfoReq::~InfoReq()
{
    m_handler = "";
    done();
    m_server.removeInfoReq(*this);
}

InfoReq::Status InfoReq::check()
{
    if(m_status == ST_RUN) {
        // give an opportunity to poll the sources on the main context
        g_main_context_iteration(g_main_loop_get_context(m_server.getLoop()), false);
        checkTimeout();
    }
    return m_status;
}

bool InfoReq::getResponse(InfoMap &response)
{
    if (m_status == ST_OK) {
        response = m_response;
        return true;
    }
    return false;
}

InfoReq::Status InfoReq::wait(InfoMap &response, uint32_t interval)
{
    // give a chance to check whether it has been timeout
    check();
    if(m_status == ST_RUN) {
        guint checkSource = g_timeout_add_seconds(interval, 
                                                  (GSourceFunc) checkCallback,
                                                  static_cast<gpointer>(this));
        while(m_status == ST_RUN) {
            g_main_context_iteration(g_main_loop_get_context(m_server.getLoop()), true);
        }

        // if the source is not removed
        if(m_status != ST_TIMEOUT && m_status != ST_CANCEL) {
            g_source_remove(checkSource);
        }
    }
    if (m_status == ST_OK) {
        response = m_response;
    }
    return m_status;
}

void InfoReq::cancel()
{
    if(m_status == ST_RUN) {
        m_handler = "";
        done();
        m_status = ST_CANCEL;
    }
}

string InfoReq::statusToString(Status status)
{
    switch(status) {
    case ST_RUN:
        return "running";
    case ST_OK:
        return "ok";
    case ST_CANCEL:
        return "cancelled";
    case ST_TIMEOUT:
        return "timeout";
    default:
        return "";
    };
}

string InfoReq::infoStateToString(InfoState state)
{
    switch(state) {
    case IN_REQ:
        return "request";
    case IN_WAIT:
        return "waiting";
    case IN_DONE:
        return "done";
    default:
        return "";
    }
}

gboolean InfoReq::checkCallback(gpointer data)
{
    // TODO: check abort and suspend(MB#8730)

    // if InfoRequest("request") is sent and waiting for InfoResponse("working"),
    // add a timeout mechanism
    InfoReq *req = static_cast<InfoReq*>(data);
    if (req->checkTimeout()) {
        return FALSE;
    }
    return TRUE;
}

bool InfoReq::checkTimeout()
{
    // if waiting for client response, check time out
    if(m_status == ST_RUN) {
        if (m_timer.timeout()) {
            m_status = ST_TIMEOUT;
            return true;
        }
    }
    return false;
}

void InfoReq::setResponse(const Caller_t &caller, const string &state, const InfoMap &response)
{
    if(m_status != ST_RUN) {
        return;
    } else if(m_infoState == IN_REQ && state == "working") {
        m_handler = caller;
        m_infoState = IN_WAIT;
        m_server.emitInfoReq(*this);
        //reset the timer, used to check timeout
        m_timer.reset();
    } else if(m_infoState == IN_WAIT && state == "response") {
        m_response = response;
        m_handler = caller;
        done();
        m_status = ST_OK;
    }
}

void InfoReq::done()
{
    if (m_infoState != IN_DONE) {
        m_infoState = IN_DONE;
        m_server.emitInfoReq(*this);
    }
}

/********************** BluezManager implementation ******************/
BluezManager::BluezManager(DBusServer &server) :
    m_server(server),
    m_adapterChanged(*this, "DefaultAdapterChanged")
{
    m_bluezConn = b_dbus_setup_bus(DBUS_BUS_SYSTEM, NULL, true, NULL);
    if(m_bluezConn) {
        m_done = false;
        DBusClientCall1<DBusObject_t> getAdapter(*this, "DefaultAdapter");
        getAdapter(boost::bind(&BluezManager::defaultAdapterCb, this, _1, _2 ));
        m_adapterChanged.activate(boost::bind(&BluezManager::defaultAdapterChanged, this, _1));
    } else {
        m_done = true;
    }
}

void BluezManager::defaultAdapterChanged(const DBusObject_t &adapter)
{
    m_done = false;
    //remove devices that belong to this original adapter
    if(m_adapter) {
        BOOST_FOREACH(boost::shared_ptr<BluezDevice> &device, m_adapter->getDevices()) {
            m_server.removeDevice(device->getMac());
        }
    }
    string error;
    defaultAdapterCb(adapter, error);
}

void BluezManager::defaultAdapterCb(const DBusObject_t &adapter, const string &error)
{
    if(!error.empty()) {
        SE_LOG_DEBUG (NULL, NULL, "Error in calling DefaultAdapter of Interface org.bluez.Manager: %s", error.c_str());
        m_done = true;
        return;
    }
    m_adapter.reset(new BluezAdapter(*this, adapter)); 
}

BluezManager::BluezAdapter::BluezAdapter(BluezManager &manager, const string &path)
    : m_manager(manager), m_path(path), m_devNo(0), m_devReplies(0),
      m_deviceRemoved(*this,  "DeviceRemoved"), m_deviceAdded(*this, "DeviceCreated")
{
    DBusClientCall1<std::vector<DBusObject_t> > listDevices(*this, "ListDevices");
    listDevices(boost::bind(&BluezAdapter::listDevicesCb, this, _1, _2));
    m_deviceRemoved.activate(boost::bind(&BluezAdapter::deviceRemoved, this, _1));
    m_deviceAdded.activate(boost::bind(&BluezAdapter::deviceCreated, this, _1));
}

void BluezManager::BluezAdapter::listDevicesCb(const std::vector<DBusObject_t> &devices, const string &error)
{
    if(!error.empty()) {
        SE_LOG_DEBUG (NULL, NULL, "Error in calling ListDevices of Interface org.bluez.Adapter: %s", error.c_str());
        checkDone(true);
        return;
    }
    m_devNo = devices.size();
    BOOST_FOREACH(const DBusObject_t &device, devices) {
        boost::shared_ptr<BluezDevice> bluezDevice(new BluezDevice(*this, device));
        m_devices.push_back(bluezDevice);
    }
    checkDone();
}

void BluezManager::BluezAdapter::deviceRemoved(const DBusObject_t &object)
{
    string address;
    std::vector<boost::shared_ptr<BluezDevice> >::iterator devIt;
    for(devIt = m_devices.begin(); devIt != m_devices.end(); ++devIt) {
        if(boost::equals((*devIt)->getPath(), object)) {
            address = (*devIt)->m_mac;
            if((*devIt)->m_reply) {
                m_devReplies--;
            }
            m_devNo--;
            m_devices.erase(devIt);
            break;
        }
    }
    m_manager.m_server.removeDevice(address);
}

void BluezManager::BluezAdapter::deviceCreated(const DBusObject_t &object)
{
    m_devNo++;
    boost::shared_ptr<BluezDevice> bluezDevice(new BluezDevice(*this, object));
    m_devices.push_back(bluezDevice);
}

BluezManager::BluezDevice::BluezDevice (BluezAdapter &adapter, const string &path)
    : m_adapter(adapter), m_path(path), m_reply(false), m_propertyChanged(*this, "PropertyChanged")
{
    DBusClientCall1<PropDict> getProperties(*this, "GetProperties");
    getProperties(boost::bind(&BluezDevice::getPropertiesCb, this, _1, _2));

    m_propertyChanged.activate(boost::bind(&BluezDevice::propertyChanged, this, _1, _2));
}

void BluezManager::BluezDevice::checkSyncService(const std::vector<std::string> &uuids)
{
    static const char * SYNCML_CLIENT_UUID = "00000002-0000-1000-8000-0002ee000002";
    bool hasSyncService = false;
    DBusServer &server = m_adapter.m_manager.m_server;
    BOOST_FOREACH(const string &uuid, uuids) {
        //if the device has sync service, add it to the device list
        if(boost::iequals(uuid, SYNCML_CLIENT_UUID)) {
            hasSyncService = true;
            if(!m_mac.empty()) {
                server.addDevice(SyncConfig::DeviceDescription(m_mac, m_name, SyncConfig::MATCH_FOR_SERVER_MODE));
            }
            break;
        }
    }
    // if sync service is not available now, possible to remove device
    if(!hasSyncService && !m_mac.empty()) {
        server.removeDevice(m_mac);
    }
}

void BluezManager::BluezDevice::getPropertiesCb(const PropDict &props, const string &error)
{
    m_adapter.m_devReplies++;
    m_reply = true;
    if(!error.empty()) {
        SE_LOG_DEBUG (NULL, NULL, "Error in calling GetProperties of Interface org.bluez.Device: %s", error.c_str());
    } else {
        PropDict::const_iterator it = props.find("Name");
        if(it != props.end()) {
            m_name = boost::get<string>(it->second);
        }
        it = props.find("Address");
        if(it != props.end()) {
            m_mac = boost::get<string>(it->second);
        }

        PropDict::const_iterator uuids = props.find("UUIDs");
        if(uuids != props.end()) {
            const std::vector<std::string> uuidVec = boost::get<std::vector<std::string> >(uuids->second);
            checkSyncService(uuidVec);
        }
    }
    m_adapter.checkDone();
}

void BluezManager::BluezDevice::propertyChanged(const string &name,
                                                const boost::variant<vector<string>, string> &prop)
{
    DBusServer &server = m_adapter.m_manager.m_server;
    if(boost::iequals(name, "Name")) {
        m_name = boost::get<std::string>(prop);
        SyncConfig::DeviceDescription device;
        if(server.getDevice(m_mac, device)) {
            device.m_fingerprint = m_name;
            server.updateDevice(m_mac, device);
        }
    } else if(boost::iequals(name, "UUIDs")) {
        const std::vector<std::string> uuidVec = boost::get<std::vector<std::string> >(prop);
        checkSyncService(uuidVec);
    } else if(boost::iequals(name, "Address")) {
        string mac = boost::get<std::string>(prop);
        SyncConfig::DeviceDescription device;
        if(server.getDevice(m_mac, device)) {
            device.m_deviceId = mac;
            server.updateDevice(m_mac, device);
        }
        m_mac = mac;
    }
}

/************************ AutoSyncManager ******************/
void AutoSyncManager::init()
{
    m_peerMap.clear();
    SyncConfig::ConfigList list = SyncConfig::getConfigs();
    BOOST_FOREACH(const SyncConfig::ConfigList::value_type &server, list) {
        initConfig(server.first);
    }

    m_notificationManager = NotificationManagerFactory::createManager();
    m_notificationManager->init();
}

void AutoSyncManager::initConfig(const string &configName)
{
    SyncConfig config (configName);
    if(!config.exists()) {
        return;
    }
    vector<string> urls = config.getSyncURL();
    string autoSync = config.getAutoSync();

    //enable http and bt?
    bool http = false, bt = false;
    bool any = false;
    if(autoSync.empty() || boost::iequals(autoSync, "0")
            || boost::iequals(autoSync, "f")) {
        http = false;
        bt = false;
        any = false;
    } else if(boost::iequals(autoSync, "1") || boost::iequals(autoSync, "t")) {
        http = true;
        bt = true;
        any = true;
    } else {
        vector<string> options;
        boost::split(options, autoSync, boost::is_any_of(",")); 
        BOOST_FOREACH(string op, options) {
            if(boost::iequals(op, "http")) {
                http = true;
            } else if(boost::iequals(op, "obex-bt")) {
                bt = true;
            }
        }
    }

    unsigned int interval = config.getAutoSyncInterval();
    unsigned int duration = config.getAutoSyncDelay();

    SE_LOG_DEBUG(NULL, NULL, "auto sync: %s: auto sync '%s', %s, %s, %d seconds repeat interval, %d seconds online duration",
                 configName.c_str(),
                 autoSync.c_str(),
                 bt ? "Bluetooth" : "no Bluetooth",
                 http ? "HTTP" : "no HTTP",
                 interval, duration);

    BOOST_FOREACH(string url, urls) {
        AutoSyncTask::Transport transport = AutoSyncTask::NEEDS_OTHER; // fallback for unknown sync URL
        if (boost::istarts_with(url, "http")) {
            transport = AutoSyncTask::NEEDS_HTTP;
        } else if (boost::istarts_with(url, "local")) {
            // TODO: instead of assuming that local sync needs HTTP, really look into the target config
            // and determine what the peerType is
            transport = AutoSyncTask::NEEDS_HTTP;
        } else if (boost::istarts_with(url, "obex-bt")) {
            transport = AutoSyncTask::NEEDS_BT;
        }
        if((transport == AutoSyncTask::NEEDS_HTTP && http) ||
           (transport == AutoSyncTask::NEEDS_BT && bt) ||
           (transport == AutoSyncTask::NEEDS_OTHER && any)) {
            AutoSyncTask syncTask(configName, duration, transport, url);
            PeerMap::iterator it = m_peerMap.find(interval);
            if(it != m_peerMap.end()) {
                SE_LOG_DEBUG(NULL, NULL,
                             "auto sync: adding config %s url %s to existing interval %ld",
                             configName.c_str(),
                             url.c_str(),
                             (long)interval);
                it->second->push_back(syncTask);
            } else {
                boost::shared_ptr<AutoSyncTaskList> list(new AutoSyncTaskList(*this, interval));
                list->push_back(syncTask);
                list->createTimeoutSource();
                if (m_peerMap.empty()) {
                    // Adding first auto sync task. Ensure that we don't shut down.
                    SE_LOG_DEBUG(NULL, NULL, "auto sync: adding first config %s url %s, prevent auto-termination",
                                 configName.c_str(),
                                 url.c_str());
                    m_server.autoTermRef();
                } else {
                    SE_LOG_DEBUG(NULL, NULL, "auto sync: adding config %s url %s, %ld already added earlier",
                                 configName.c_str(),
                                 url.c_str(),
                                 (long)m_peerMap.size());
                }
                m_peerMap.insert(std::make_pair(interval, list));
            }
        }
    }
}

void AutoSyncManager::remove(const string &configName)
{
    //wipe out tasks in the m_peerMap
    PeerMap::iterator it = m_peerMap.begin();
    while(it != m_peerMap.end()) {
        boost::shared_ptr<AutoSyncTaskList> &list = it->second;
        AutoSyncTaskList::iterator taskIt = list->begin();
        while(taskIt != list->end()) {
            if(boost::iequals(taskIt->m_peer, configName)) {
                taskIt = list->erase(taskIt);
            } else {
                ++taskIt;
            }
        }
        //if list is empty, remove the list from map
        if(list->empty()) {
            PeerMap::iterator erased = it++;
            m_peerMap.erase(erased);
            if (m_peerMap.empty()) {
                // removed last entry, remove lock on auto termination
                SE_LOG_DEBUG(NULL, NULL, "auto sync: last auto sync config %s gone, allow auto-termination",
                             configName.c_str());
                m_server.autoTermUnref();
            } else {
                SE_LOG_DEBUG(NULL, NULL, "auto sync: sync config %s gone, still %ld configure for auto-sync",
                             configName.c_str(),
                             (long)m_peerMap.size());
            }
        } else {
            ++it;
        }
    }

    //wipe out scheduled tasks in the working queue based on configName
    list<AutoSyncTask>::iterator qit = m_workQueue.begin();
    while(qit != m_workQueue.end()) {
        if(boost::iequals(qit->m_peer, configName)) {
            qit = m_workQueue.erase(qit);
        } else {
            ++qit;
        }
    }
}

void AutoSyncManager::update(const string &configName)
{
    SE_LOG_DEBUG(NULL, NULL, "auto sync: refreshing %s", configName.c_str());

    // remove task from m_peerMap and tasks in the working queue for this config
    remove(configName);
    // re-load the config and re-init peer map
    initConfig(configName);

    //don't clear if the task is running
    if(m_session && !hasActiveSession()
            && boost::iequals(m_session->getConfigName(), configName)) {
        SE_LOG_DEBUG(NULL, NULL, "auto sync: removing queued session for %s during update",
                     configName.c_str());
        m_server.dequeue(m_session.get());
        m_session.reset();
        m_activeTask.reset();
        startTask();
    }
}

void AutoSyncManager::scheduleAll()
{
    BOOST_FOREACH(PeerMap::value_type &elem, m_peerMap) {
        elem.second->scheduleTaskList();
    }
}

bool AutoSyncManager::addTask(const AutoSyncTask &syncTask)
{
    if(taskLikelyToRun(syncTask)) {
        m_workQueue.push_back(syncTask);
        return true;
    }
    return false;
}

bool AutoSyncManager::findTask(const AutoSyncTask &syncTask)
{
    if(m_activeTask && *m_activeTask == syncTask) {
        return true;
    }
    BOOST_FOREACH(const AutoSyncTask &task, m_workQueue) {
        if(task == syncTask) {
            return true;
        }
    }
    return false;
}

bool AutoSyncManager::taskLikelyToRun(const AutoSyncTask &syncTask)
{
    PresenceStatus &status = m_server.getPresenceStatus(); 

    if (syncTask.m_transport == AutoSyncTask::NEEDS_HTTP && status.getHttpPresence()) {
        // don't add duplicate tasks
        if(!findTask(syncTask)) {
            Timer& timer = status.getHttpTimer();
            // if the time peer have been around is longer than 'autoSyncDelay',
            // then return true
            if (timer.timeout(syncTask.m_delay * 1000 /* seconds to milliseconds */)) {
                return true;
            }
        } 
    } else if ((syncTask.m_transport == AutoSyncTask::NEEDS_BT && status.getBtPresence()) ||
               syncTask.m_transport == AutoSyncTask::NEEDS_OTHER) {
        // don't add duplicate tasks
        if(!findTask(syncTask)) {
            return true;
        }
    }
    return false;
}

void AutoSyncManager::startTask()
{
    // get the front task and run a sync
    // if there has been a session for the front task, do nothing
    if(hasTask() && !m_session) {
        m_activeTask.reset(new AutoSyncTask(m_workQueue.front()));
        m_workQueue.pop_front();
        string newSession = m_server.getNextSession();   
        m_session = Session::createSession(m_server,
                                           "",
                                           m_activeTask->m_peer,
                                           newSession);
        m_session->setPriority(Session::PRI_AUTOSYNC);
        m_session->addListener(this);
        m_session->activate();
        m_server.enqueue(m_session);
    }
}

bool AutoSyncManager::hasActiveSession()
{
    return m_session && m_session->getActive();
}

void AutoSyncManager::prepare()
{
    if(m_session && m_session->getActive()) {
        // now a config may contain many urls, so replace it with our own temporarily
        // otherwise it only picks the first one
        ReadOperations::Config_t config;
        StringMap stringMap;
        stringMap["syncURL"] = m_activeTask->m_url;
        config[""] = stringMap;
        m_session->setConfig(true, true, config);

        string mode;
        Session::SourceModes_t sourceModes;
        m_session->sync("", Session::SourceModes_t());
    }
}

void AutoSyncManager::syncSuccessStart()
{
    m_syncSuccessStart = true;
    SE_LOG_INFO(NULL, NULL,"Automatic sync for '%s' has been successfully started.\n", m_activeTask->m_peer.c_str());
    if (m_server.notificationsEnabled()) {
        string summary = StringPrintf(_("%s is syncing"), m_activeTask->m_peer.c_str());
        string body = StringPrintf(_("We have just started to sync your computer with the %s sync service."), m_activeTask->m_peer.c_str());
        //TODO: set config information for 'sync-ui'
        m_notificationManager->publish(summary, body);
    }
}

/**
 * True if the error is likely to go away by itself when continuing
 * with auto-syncing. This errs on the side of showing notifications
 * too often rather than not often enough.
 */
static bool ErrorIsTemporary(SyncMLStatus status)
{
    switch (status) {
    case STATUS_TRANSPORT_FAILURE:
        return true;
    default:
        // pretty much everying this not temporary
        return false;
    }
}

void AutoSyncManager::syncDone(SyncMLStatus status)
{
    SE_LOG_INFO(NULL, NULL,"Automatic sync for '%s' has been done.\n", m_activeTask->m_peer.c_str());
    if (m_server.notificationsEnabled()) {
        // send a notification to notification server
        string summary, body;
        if(m_syncSuccessStart && status == STATUS_OK) {
            // if sync is successfully started and done
            summary = StringPrintf(_("%s sync complete"), m_activeTask->m_peer.c_str());
            body = StringPrintf(_("We have just finished syncing your computer with the %s sync service."), m_activeTask->m_peer.c_str());
            //TODO: set config information for 'sync-ui'
            m_notificationManager->publish(summary, body);
        } else if (m_syncSuccessStart || !ErrorIsTemporary(status)) {
            // if sync is successfully started and has errors, or not started successful with a permanent error
            // that needs attention
            summary = StringPrintf(_("Sync problem."));
            body = StringPrintf(_("Sorry, there's a problem with your sync that you need to attend to."));
            //TODO: set config information for 'sync-ui'
            m_notificationManager->publish(summary, body);
        }
    }

    // keep session around to give clients a chance to query it
    m_server.delaySessionDestruction(m_session);
    m_session->done();

    m_session.reset();
    m_activeTask.reset();
    m_syncSuccessStart = false;
}

void AutoSyncManager::AutoSyncTaskList::createTimeoutSource()
{
    //if interval is 0, only run auto sync when changes are detected.
    if(m_interval) {
        m_source = g_timeout_add_seconds(m_interval, taskListTimeoutCb, static_cast<gpointer>(this));
    }
}

gboolean AutoSyncManager::AutoSyncTaskList::taskListTimeoutCb(gpointer data)
{
    AutoSyncTaskList *list = static_cast<AutoSyncTaskList*>(data);
    list->scheduleTaskList();
    return TRUE;
}

void AutoSyncManager::AutoSyncTaskList::scheduleTaskList()
{
    BOOST_FOREACH(AutoSyncTask &syncTask, *this) {
        m_manager.addTask(syncTask);
    }
    g_main_loop_quit(m_manager.m_server.getLoop());
}

/**************************** main *************************/

void niam(int sig)
{
    shutdownRequested = true;
    SyncContext::handleSignal(sig);
    g_main_loop_quit (loop);
}

static bool parseDuration(int &duration, const char* value)
{
    if(value == NULL) {
        return false;
    } else if (boost::iequals(value, "unlimited")) {
        duration = -1;
        return true;
    } else if ((duration = atoi(value)) > 0) {
        return true;
    } else {
        return false;
    }
}

SE_END_CXX

int main(int argc, char **argv, char **envp)
{
    // remember environment for restart
    restart.reset(new Restart(argv, envp));

    int duration = 600;
    int opt = 1;
    while(opt < argc) {
        if(argv[opt][0] != '-') {
            break;
        }
        if (boost::iequals(argv[opt], "--duration") ||
            boost::iequals(argv[opt], "-d")) {
            opt++;
            if(!parseDuration(duration, opt== argc ? NULL : argv[opt])) {
                std::cout << argv[opt-1] << ": unknown parameter value or not set" << std::endl;
                return false;
            }
        } else {
            std::cout << argv[opt] << ": unknown parameter" << std::endl;
            return false;
        }
        opt++;
    }
    try {
        SyncContext::initMain("syncevo-dbus-server");

        loop = g_main_loop_new (NULL, FALSE);

        setvbuf(stderr, NULL, _IONBF, 0);
        setvbuf(stdout, NULL, _IONBF, 0);

        signal(SIGTERM, niam);
        signal(SIGINT, niam);

        LogRedirect redirect(true);
        redirectPtr = &redirect;

        // make daemon less chatty - long term this should be a command line option
        LoggerBase::instance().setLevel(getenv("SYNCEVOLUTION_DEBUG") ?
                                        LoggerBase::DEBUG :
                                        LoggerBase::INFO);

        DBusErrorCXX err;
        DBusConnectionPtr conn = b_dbus_setup_bus(DBUS_BUS_SESSION,
                                                  "org.syncevolution",
                                                  true,
                                                  &err);
        if (!conn) {
            err.throwFailure("b_dbus_setup_bus()", " failed - server already running?");
        }

        SyncEvo::DBusServer server(loop, conn, duration);
        server.activate();

        SE_LOG_INFO(NULL, NULL, "%s: ready to run",  argv[0]);
        server.run();
        SE_LOG_INFO(NULL, NULL, "%s: terminating",  argv[0]);
	return 0;
    } catch ( const std::exception &ex ) {
        SE_LOG_ERROR(NULL, NULL, "%s", ex.what());
    } catch (...) {
        SE_LOG_ERROR(NULL, NULL, "unknown error");
    }

    return 1;
}
