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

#include "auto-sync-manager.h"
#include "session.h"
#include "server.h"
#include "dbus-callbacks.h"

#include <glib.h>
#include <glib/gi18n.h>

#include <boost/tokenizer.hpp>

SE_BEGIN_CXX

AutoSyncManager::AutoSyncManager(Server &server) :
    m_server(server),
    m_autoTermLocked(false)
{
}

static void updatePresence(Timespec *t, bool present)
{
    *t = present ?
        Timespec::monotonic() :
        Timespec();
}

boost::shared_ptr<AutoSyncManager> AutoSyncManager::createAutoSyncManager(Server &server)
{
    boost::shared_ptr<AutoSyncManager> result(new AutoSyncManager(server));
    result->m_me = result;
    result->init();

    // update cached information about a config each time it changes
    server.m_configChangedSignal.connect(Server::ConfigChangedSignal_t::slot_type(&AutoSyncManager::initConfig, result.get(), _1).track(result));

    // monitor running sessions
    server.m_newSyncSessionSignal.connect(Server::NewSyncSessionSignal_t::slot_type(&AutoSyncManager::sessionStarted, result.get(), _1).track(result));

    // Keep track of the time when a transport became online. As with
    // time of last sync, we are pessimistic here and assume that the
    // transport just now became available.
    PresenceStatus &p = server.getPresenceStatus();
    Timespec now = Timespec::monotonic();
    if (p.getBtPresence()) {
        result->m_btStartTime = now;
    }
    p.m_btPresenceSignal.connect(PresenceStatus::PresenceSignal_t::slot_type(updatePresence,
                                                                             &result->m_btStartTime,
                                                                             _1).track(result));
    if (p.getHttpPresence()) {
        result->m_httpStartTime = now;
    }
    p.m_httpPresenceSignal.connect(PresenceStatus::PresenceSignal_t::slot_type(updatePresence,
                                                                               &result->m_httpStartTime,
                                                                               _1).track(result));

    return result;
}

void AutoSyncManager::init()
{
    m_notificationManager = NotificationManagerFactory::createManager();
    m_notificationManager->init();

    m_peerMap.clear();
    SyncConfig::ConfigList list = SyncConfig::getConfigs();
    BOOST_FOREACH(const SyncConfig::ConfigList::value_type &server, list) {
        initConfig(server.first);
    }
}

void AutoSyncManager::initConfig(const std::string &configName)
{
    SE_LOG_DEBUG(NULL, NULL, "auto sync: updating info about config %s", configName.c_str());

    if (configName.empty()) {
        // Anything might have changed. Check all configs we know
        // about (might have been removed) and all existing configs
        // (might have been modified).
        std::set<std::string> configs;
        BOOST_FOREACH (const PeerMap::value_type &entry, m_peerMap) {
            const std::string &configName = entry.first;
            configs.insert(configName);
        }
        BOOST_FOREACH (const StringPair &entry, SyncConfig::getConfigs()) {
            const std::string &configName = entry.first;
            configs.insert(configName);
        }
        BOOST_FOREACH (const std::string &configName, configs) {
            if (!configName.empty()) {
                initConfig(configName);
            }
        }

        // TODO: only call schedule() once in this case, instead
        // once per recursive initConfig().
    }

    // TODO: once we depend on shared settings, remember to check
    // all other configs which share the same set of settings.
    // Not currently the case.

    // Create anew or update, directly in map. Never remove
    // old entries, because we want to keep the m_lastSyncTime
    // in cases where configs get removed and recreated.
    boost::shared_ptr<AutoSyncTask> &task = m_peerMap[configName];
    if (!task) {
        task.reset(new AutoSyncTask(configName));
        // We should check past sessions here. Instead we assume
        // the "worst" case, which is that the session ran zero
        // seconds ago. This has the additional benefit that we
        // don't run automatic sync sessions directly after
        // starting up (the system or syncevo-dbus-server).
        task->m_lastSyncTime = Timespec::monotonic();
    }

    SyncConfig config (configName);
    if (config.exists()) {
        std::vector<std::string> urls = config.getSyncURL();
        std::string autoSync = config.getAutoSync();

        //enable http and bt?
        bool http = false, bt = false;
        bool any = false;
        if (autoSync.empty() ||
            boost::iequals(autoSync, "0") ||
            boost::iequals(autoSync, "f")) {
            http = false;
            bt = false;
            any = false;
        } else if (boost::iequals(autoSync, "1") ||
                   boost::iequals(autoSync, "t")) {
            http = true;
            bt = true;
            any = true;
        } else {
            BOOST_FOREACH(std::string op,
                          boost::tokenizer< boost::char_separator<char> >(autoSync,
                                                                          boost::char_separator<char>(","))) {
                if(boost::iequals(op, "http")) {
                    http = true;
                } else if(boost::iequals(op, "obex-bt")) {
                    bt = true;
                }
            }
        }

        task->m_peerName = config.getPeerName();
        if (task->m_peerName.empty()) {
            task->m_peerName = configName;
        }
        task->m_interval = config.getAutoSyncInterval();
        task->m_delay = config.getAutoSyncDelay();
        task->m_remoteDeviceId = config.getRemoteDevID();

        // Assume that whatever change was made might have resolved
        // the past problem -> allow auto syncing again.
        task->m_permanentFailure = false;

        SE_LOG_DEBUG(NULL, NULL,
                     "auto sync: %s: auto sync '%s', %s, %s, %d seconds repeat interval, %d seconds online delay",
                     configName.c_str(),
                     autoSync.c_str(),
                     bt ? "Bluetooth" : "no Bluetooth",
                     http ? "HTTP" : "no HTTP",
                     task->m_interval, task->m_delay);

        task->m_urls.clear();
        BOOST_FOREACH(std::string url, urls) {
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
                task->m_urls.push_back(std::make_pair(transport, url));
                SE_LOG_DEBUG(NULL, NULL,
                             "auto sync: adding config %s url %s",
                             configName.c_str(),
                             url.c_str());
            }
        }
    } else {
        // Just clear urls, which disables auto syncing.
        task->m_urls.clear();
    }

    bool lock = preventTerm();
    if (m_autoTermLocked && !lock) {
        SE_LOG_DEBUG(NULL, NULL, "auto sync: allow auto shutdown");
        m_server.autoTermUnref();
        m_autoTermLocked = false;
    } else if (!m_autoTermLocked && lock) {
        SE_LOG_DEBUG(NULL, NULL, "auto sync: prevent auto shutdown");
        m_server.autoTermRef();
        m_autoTermLocked = true;
    }

    // reschedule
    schedule("initConfig() for " + configName);
}

void AutoSyncManager::schedule(const std::string &reason)
{
    SE_LOG_DEBUG(NULL, NULL, "auto sync: reschedule, %s", reason.c_str());

    // idle callback will be (re)set if needed
    m_idleConnection.disconnect();

    if (!preventTerm()) {
        SE_LOG_DEBUG(NULL, NULL, "auto sync: nothing to do");
        return;
    }

    if (!m_server.isIdle()) {
        // Only schedule automatic syncs when nothing else is
        // going on or pending.
        SE_LOG_DEBUG(NULL, NULL, "auto sync: server not idle");
        connectIdle();
        return;
    }

    // Now look for a suitable task that is ready to run.
    Timespec now = Timespec::monotonic();
    BOOST_FOREACH (const PeerMap::value_type &entry, m_peerMap) {
        const std::string &configName = entry.first;
        const boost::shared_ptr<AutoSyncTask> &task = entry.second;

        if (task->m_interval <= 0 || // not enabled
            task->m_permanentFailure) { // don't try again
            continue;
        }

        if (task->m_lastSyncTime + task->m_interval > now) {
            // Ran too recently, check again in the future. Always
            // reset timer, because both m_lastSyncTime and m_interval
            // may have changed.
            int seconds = (task->m_lastSyncTime + task->m_interval - now).seconds() + 1;
            SE_LOG_DEBUG(NULL, NULL, "auto sync: %s: interval expires in %ds",
                         configName.c_str(),
                         seconds);
            task->m_intervalTimeout.runOnce(seconds,
                                            boost::bind(&AutoSyncManager::schedule,
                                                        this,
                                                        configName + " interval timer"));
            continue;
        }

        std::string readyURL;
        BOOST_FOREACH (const AutoSyncTask::URLInfo_t::value_type &urlinfo, task->m_urls) {
            // check m_delay against presence of transport
            Timespec *starttime = NULL;
            PresenceStatus::PresenceSignal_t *signal = NULL;
            Timeout *timeout = NULL;
            switch (urlinfo.first) {
            case AutoSyncTask::NEEDS_HTTP:
                starttime = &m_httpStartTime;
                signal = &m_server.getPresenceStatus().m_httpPresenceSignal;
                timeout = &task->m_httpTimeout;
                break;
            case AutoSyncTask::NEEDS_BT:
                starttime = &m_btStartTime;
                signal = &m_server.getPresenceStatus().m_btPresenceSignal;
                timeout = &task->m_btTimeout;
                break;
            case AutoSyncTask::NEEDS_OTHER:
                break;
            }
            if (!starttime || // some other transport, assumed to be online, use it
                (*starttime && // present
                 (task->m_delay <= 0 || *starttime + task->m_delay > now))) { // present long enough
                readyURL = urlinfo.second;
                break;
            }
            if (!*starttime) {
                // check again when it becomes present
                signal->connect(PresenceStatus::PresenceSignal_t::slot_type(&AutoSyncManager::schedule,
                                                                            this,
                                                                            "presence change").track(m_me));
            } else {
                // check again after waiting the requested amount of time
                int seconds = (*starttime + task->m_delay - now).seconds() + 1;
                SE_LOG_DEBUG(NULL, NULL, "auto sync: %s: presence delay expires in %ds",
                             configName.c_str(),
                             seconds);
                timeout->runOnce(seconds,
                                 boost::bind(&AutoSyncManager::schedule,
                                             this,
                                             configName + " transport timer"));
            }
        }

        if (!readyURL.empty()) {
            // Found a task, run it. The session is not attached to any client,
            // but we keep a pointer to it, so it won't go away.
            // m_task = task;

            // Just in case... also done in syncDone() when we detect
            // that session is completed.
            m_server.delaySessionDestruction(m_session);

            task->m_syncSuccessStart = false;
            m_session = Session::createSession(m_server,
                                               task->m_remoteDeviceId,
                                               configName,
                                               m_server.getNextSession());

            // Temporarily set sync URL to the one which we picked above
            // once the session is active (setConfig() not allowed earlier).
            ReadOperations::Config_t config;
            config[""]["syncURL"] = readyURL;
            m_session->m_sessionActiveSignal.connect(boost::bind(&Session::setConfig,
                                                                 m_session.get(),
                                                                 true, true,
                                                                 config));

            // Run sync as soon as it is active.
            m_session->m_sessionActiveSignal.connect(boost::bind(&Session::sync,
                                                                 m_session.get(),
                                                                 "",
                                                                 SessionCommon::SourceModes_t()));

            // Now run it.
            m_session->activate();
            m_server.enqueue(m_session);

            // Reschedule when server is idle again.
            connectIdle();
            return;
        }

    }

    SE_LOG_DEBUG(NULL, NULL, "auto sync: nothing to do");
}

void AutoSyncManager::connectIdle()
{
    m_idleConnection =
        m_server.m_idleSignal.connect(Server::IdleSignal_t::slot_type(&AutoSyncManager::schedule,
                                                                      this,
                                                                      "server is idle").track(m_me));
}

void AutoSyncManager::sessionStarted(const boost::shared_ptr<Session> &session)
{
    // Do we have a task for this config?
    std::string configName = session->getConfigName();
    PeerMap::iterator it = m_peerMap.find(configName);
    if (it == m_peerMap.end()) {
        SE_LOG_DEBUG(NULL, NULL, "auto sync: ignore running sync %s without config",
                     configName.c_str());
        return;
    }

    boost::shared_ptr<AutoSyncManager> me = m_me.lock();
    if (!me) {
        SE_LOG_DEBUG(NULL, NULL, "auto sync: already destructing, ignore new sync %s",
                     configName.c_str());
        return;
    }

    const boost::shared_ptr<AutoSyncTask> &task = it->second;
    task->m_lastSyncTime = Timespec::monotonic();

    // track permanent failure
    session->m_doneSignal.connect(Session::DoneSignal_t::slot_type(&AutoSyncManager::anySyncDone, this, task.get(), _1).track(task).track(me));

    if (m_session == session) {
        // Only for our own auto sync session: notify user once session starts successful.
        //
        // In the (unlikely) case that the AutoSyncTask gets deleted, the
        // slot won't get involved, thus skipping user notifications.
        // Also protects against manager destructing before session.
        session->m_syncSuccessStartSignal.connect(Session::SyncSuccessStartSignal_t::slot_type(&AutoSyncManager::autoSyncSuccessStart, this, task.get()).track(task).track(me));

        // Notify user once session ends, with or without failure.
        // Same instance tracking as for sync success start.
        session->m_doneSignal.connect(Session::DoneSignal_t::slot_type(&AutoSyncManager::autoSyncDone, this, task.get(), _1).track(task).track(me));
    }
}

bool AutoSyncManager::preventTerm()
{
    BOOST_FOREACH (const PeerMap::value_type &entry, m_peerMap) {
        const boost::shared_ptr<AutoSyncTask> &task = entry.second;
        if (task->m_interval > 0 &&
            !task->m_permanentFailure &&
            !task->m_urls.empty()) {
            // that task might run
            return true;
        }
    }
    return false;
}

void AutoSyncManager::autoSyncSuccessStart(AutoSyncTask *task)
{
    task->m_syncSuccessStart = true;
    SE_LOG_INFO(NULL, NULL,"Automatic sync for '%s' has been successfully started.\n",
                task->m_peerName.c_str());
    if (m_server.notificationsEnabled()) {
        std::string summary = StringPrintf(_("%s is syncing"), task->m_peerName.c_str());
        std::string body = StringPrintf(_("We have just started to sync your computer with the %s sync service."),
                                        task->m_peerName.c_str());
        // TODO: set config information for 'sync-ui'
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

void AutoSyncManager::autoSyncDone(AutoSyncTask *task, SyncMLStatus status)
{
    SE_LOG_INFO(NULL, NULL,"Automatic sync for '%s' has been done.\n", task->m_peerName.c_str());
    if (m_server.notificationsEnabled()) {
        // send a notification to notification server
        std::string summary, body;
        if (task->m_syncSuccessStart && status == STATUS_OK) {
            // if sync is successfully started and done
            summary = StringPrintf(_("%s sync complete"), task->m_peerName.c_str());
            body = StringPrintf(_("We have just finished syncing your computer with the %s sync service."),
                                task->m_peerName.c_str());
            //TODO: set config information for 'sync-ui'
            m_notificationManager->publish(summary, body);
        } else if (task->m_syncSuccessStart || !ErrorIsTemporary(status)) {
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
    m_session.reset();
}

void AutoSyncManager::anySyncDone(AutoSyncTask *task, SyncMLStatus status)
{
    // set "permanently failed" flag according to most recent result
    task->m_permanentFailure = !ErrorIsTemporary(status);
    SE_LOG_DEBUG(NULL, NULL, "auto sync: sync session %s done, result %d %s",
                 task->m_configName.c_str(),
                 status,
                 task->m_permanentFailure ?
                 "is a permanent failure" :
                 status == STATUS_OK ?
                 "is success" :
                 "is temporary failure");
}

SE_END_CXX
