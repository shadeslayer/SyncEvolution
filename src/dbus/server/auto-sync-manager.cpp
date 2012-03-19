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

#include <glib.h>
#include <glib/gi18n.h>

#include <boost/tokenizer.hpp>

SE_BEGIN_CXX

AutoSyncManager::AutoSyncManager(Server &server) :
    m_server(server), m_syncSuccessStart(false)
{
}

boost::shared_ptr<AutoSyncManager> AutoSyncManager::create(Server &server)
{
    boost::shared_ptr<AutoSyncManager> result(new AutoSyncManager(server));
    result->init();

    return result;
}

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

void AutoSyncManager::initConfig(const std::string &configName)
{
    SyncConfig config (configName);
    if(!config.exists()) {
        return;
    }
    std::vector<std::string> urls = config.getSyncURL();
    std::string autoSync = config.getAutoSync();

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
        BOOST_FOREACH(std::string op,
		      boost::tokenizer< boost::char_separator<char> >(autoSync, boost::char_separator<char>(","))) {
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

void AutoSyncManager::remove(const std::string &configName)
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

void AutoSyncManager::update(const std::string &configName)
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
        std::string newSession = m_server.getNextSession();
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

        std::string mode;
        Session::SourceModes_t sourceModes;
        m_session->sync("", Session::SourceModes_t());
    }
}

void AutoSyncManager::syncSuccessStart()
{
    m_syncSuccessStart = true;
    SE_LOG_INFO(NULL, NULL,"Automatic sync for '%s' has been successfully started.\n", m_activeTask->m_peer.c_str());
    if (m_server.notificationsEnabled()) {
        std::string summary = StringPrintf(_("%s is syncing"), m_activeTask->m_peer.c_str());
        std::string body = StringPrintf(_("We have just started to sync your computer with the %s sync service."), m_activeTask->m_peer.c_str());
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
        std::string summary, body;
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

SE_END_CXX
