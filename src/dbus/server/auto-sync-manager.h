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
#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/signals2.hpp>

#include <syncevo/SyncML.h>
#include <syncevo/SyncContext.h>
#include <syncevo/SmartPtr.h>
#include <syncevo/util.h>

#include "notification-manager-factory.h"
#include "timeout.h"

SE_BEGIN_CXX

class Server;
class Session;

/**
 * Manager to manage automatic sync.
 *
 * Once a configuration is enabled with automatic sync, possibly http
 * or obex-bt or both, the manager tracks whether they are ready to
 * run. For that it watches which transports are available (and how
 * long), which syncs run, etc.
 *
 * Automatic syncs only run when the server is idle. Then a new
 * Session is created and thus runs immediately. Because multiple
 * parallel sessions are not currently supported by SyncEvolution,
 * scheduling the next session waits until the server is idle again.
 *
 * Currently only time-based automatic syncs are supported.
 * Syncs triggered by local or remote changes will be added
 * later.
 */
class AutoSyncManager
{
    Server &m_server;
    boost::weak_ptr<AutoSyncManager> m_me;

    /** true if we currently hold a ref for AutoTerm */
    bool m_autoTermLocked;

    /** currently running auto sync session */
    boost::shared_ptr<Session> m_session;

    /** connects m_server.m_idleSignal with schedule() */
    boost::signals2::connection m_idleConnection;

    /** time when Bluetooth and HTTP transports became available, zero if not available */
    Timespec m_btStartTime, m_httpStartTime;

    /** initialize m_idleConnection */
    void connectIdle();

 public:
    /**
     * A single task for automatic sync.
     *
     * Caches information about the corresponding configuration.
     * Some of that information is directly from the config, other
     * is collected from sessions (time of last sync).
     *
     * Each task maintains information for all sync URLs.
     */
    class AutoSyncTask
    {
    public:
        /**
         * unique, normalized config name, set when task is created the first time;
         * by definition it cannot be changed later
         */
        const std::string m_configName;

        /**
         * user-configurable peer name, with config name as fallback
         */
        std::string m_peerName;

        /** copy of config's remoteDeviceId sync property */
        std::string m_remoteDeviceId;

        /** copy of config's notifyLevel property */
        SyncConfig::NotifyLevel m_notifyLevel;

        /** last auto sync attempt succeeded (needed for notification logic) */
        bool m_syncSuccessStart;

        /** last auto sync attempt showed permanent failure (don't retry) */
        bool m_permanentFailure;

        /** autoSyncDelay = the time that the peer must at least have been around (seconds) */
        unsigned int m_delay;

        /**
         * autoSyncInterval = the minimum time in seconds between syncs.
         *
         * Documentation is vague about whether this is measured as the time from
         * start to start or between end to start. Traditionally, the implementation
         * was between starts (= fixed rate). This assumed that syncs are short compared
         * to the interval. In the extreme case (seen in testing), a sync takes longer
         * than the interval and thus the next sync is started immediately - probably
         * not what is expected. Keeping the behavior for now.
         */
        unsigned int m_interval;

        /**
         * currently the start time of the last sync, measured with
         * the monotonicly increasing OS time
         */
        Timespec m_lastSyncTime;

        /** maps syncURL to a specific transport mechanism */
        enum Transport {
            NEEDS_HTTP,
            NEEDS_BT,
            NEEDS_OTHER
        };

        /** list of sync URLs for which autosyncing over their transport mechanism
            , in same order as in syncURL */
        typedef std::list< std::pair<Transport, std::string> > URLInfo_t;
        URLInfo_t m_urls;

        AutoSyncTask(const std::string &configName) :
            m_configName(configName),
            m_syncSuccessStart(false),
            m_permanentFailure(false),
            m_delay(0),
            m_interval(0)
        {
        }

        /* /\** compare whether two tasks are the same, based on unique config name *\/ */
        /* bool operator==(const AutoSyncTask &right) const */
        /* { */
        /*     return m_peer == right.m_peer; */
        /* } */

        Timeout m_intervalTimeout;
        Timeout m_btTimeout;
        Timeout m_httpTimeout;
    };

    /* /\** remove tasks from m_peerMap and m_workQueue created from the config *\/ */
    /* void remove(const std::string &configName); */

    /**
     * A map with information about *all* configs ever seen while auto
     * sync manager was active, including configs without auto sync
     * enabled (to track when and if they ran) and deleted configs
     * (because they might get recreated).
     */
    typedef std::map<std::string, boost::shared_ptr<AutoSyncTask> > PeerMap;
    PeerMap m_peerMap;

    /** used to send notifications */
    boost::shared_ptr<NotificationManagerBase> m_notificationManager;

    /**
     * It reads all peers which are enabled to do auto sync and store them in
     * the m_peerMap and then add timeout sources in the main loop to schedule
     * auto sync tasks.
     */
    void init();

    /**
     * check m_peerMap: runs syncs that are ready, sets/updates timers for the rest
     *
     * @param reason     a short explanation why the method gets called (for debugging)
     */
    void schedule(const std::string &reason);

    /**
     * Watch further progress (if auto sync session),
     * record start time (in all cases).
     */
    void sessionStarted(const boost::shared_ptr<Session> &session);

    /** Show "sync started" notification. */
    void autoSyncSuccessStart(AutoSyncTask *task);

    /** Show completion notification. */
    void autoSyncDone(AutoSyncTask *task, SyncMLStatus status);

    /** Record result. */
    void anySyncDone(AutoSyncTask *task, SyncMLStatus status);

    AutoSyncManager(Server &server);

 public:
    static boost::shared_ptr<AutoSyncManager> createAutoSyncManager(Server &server);

    /**
     * prevent dbus server automatic termination when it has
     * any auto sync task enabled in the configs.
     * If returning true, prevent automatic termination.
     */
    bool preventTerm();

    /** init a config and set up auto sync task for it */
    void initConfig(const std::string &configName);
};

SE_END_CXX

#endif // AUTO_SYNC_MANAGER_H
