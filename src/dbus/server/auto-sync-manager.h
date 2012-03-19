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

#ifndef AUTO_SYNC_MANAGER_H
#define AUTO_SYNC_MANAGER_H

#include <boost/algorithm/string/predicate.hpp>

#include <syncevo/SyncML.h>
#include <syncevo/SmartPtr.h>

#include "notification-manager-factory.h"

#include "session-listener.h"

SE_BEGIN_CXX

class Server;
class Session;


/**
 * Manager to manage automatic sync.
 *
 * Once a configuration is enabled with automatic sync, possibly http
 * or obex-bt or both, one or more tasks for different URLs are added
 * in the task map, grouped by their intervals.  A task have to be
 * checked whether there is an existing same task in the working
 * queue. Once actived, it is put in the working queue.
 *
 * At any time, there is at most one session for the first task. Once
 * it is active by Server, we prepare it and make it ready to
 * run. After completion, a new session is created again for the next
 * task. And so on.
 *
 * The Server is in charge of dispatching requests from dbus
 * clients and automatic sync tasks.  
 * 
 * See Server::run().
 *
 * Here there are 3 scenarios which have been considered to do
 * automatic sync right now:
 * 1) For a config enables autosync, an interval has passed.
 * 2) Once users log in or resume and an interval has passed. Not
 *    implemented yet.
 * 3) Evolution data server notify any changes. Not implemented yet.
 */

class AutoSyncManager : public SessionListener
{
    Server &m_server;

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
        std::string m_peer;
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

      AutoSyncTask(const std::string &peer, unsigned int delay, Transport transport, const std::string &url)
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
    class AutoSyncTaskList : public std::list<AutoSyncTask>
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
    void initConfig(const std::string &configName);

    /** remove tasks from m_peerMap and m_workQueue created from the config */
    void remove(const std::string &configName);

    /** a map to contain all auto sync tasks. All initialized tasks are stored here.
     * Tasks here are grouped by auto sync interval */
    typedef std::map<unsigned int, boost::shared_ptr<AutoSyncTaskList> > PeerMap;
    PeerMap m_peerMap;

    /**
     * a working queue that including tasks which are pending for doing sync.
     * Tasks here are picked from m_peerMap and scheduled to do auto sync */
    std::list<AutoSyncTask> m_workQueue;

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

    AutoSyncManager(Server &server);

 public:
    static boost::shared_ptr<AutoSyncManager> create(Server &server);

    /**
     * prevent dbus server automatic termination when it has
     * any auto sync task enabled in the configs.
     * If returning true, prevent automatic termination.
     */
    bool preventTerm() { return !m_peerMap.empty(); }

    /**
     * called when a config is changed. This causes re-loading the config
     */
    void update(const std::string &configName);

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

SE_END_CXX

#endif // AUTO_SYNC_MANAGER_H
