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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#if HAS_NOTIFY

#include "notification-backend-libnotify.h"
#include "syncevo/util.h"
#include "syncevo/GLibSupport.h"

#include <stdlib.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/foreach.hpp>

#ifdef NOTIFY_COMPATIBILITY
# include <dlfcn.h>
#endif

SE_BEGIN_CXX

#ifdef NOTIFY_COMPATIBILITY
/**
 * set to real old C notify_notification_new() (with widget pointer) or new one (without);
 * because of the x86/AMD64 calling conventions, calling the newer function with
 * one extra parameter is okay
 */
gboolean (*notify_init)(const char *app_name);
GList *(*notify_get_server_caps)(void);
NotifyNotification *(*notify_notification_new)(const char *summary, const char *body, const char *icon, void *widget);
void (*notify_notification_add_action)(NotifyNotification *notification,
                                       const char *action,
                                       const char *label,
                                       NotifyActionCallback callback,
                                       gpointer user_data,
                                       GFreeFunc free_func);
void (*notify_notification_clear_actions)(NotifyNotification *notification);
gboolean (*notify_notification_close)(NotifyNotification *notification,
                                      GError **error);
gboolean (*notify_notification_show)(NotifyNotification *notification,
                                     GError **error);

static bool NotFound(const char *func)
{
    SE_LOG_DEBUG(NULL, NULL, "%s: not found", func);
    return false;
}
#endif

NotificationBackendLibnotify::NotificationBackendLibnotify()
    : m_initialized(false),
      m_acceptsActions(false),
      m_notification(NULL)
{
}

NotificationBackendLibnotify::~NotificationBackendLibnotify()
{
}

void NotificationBackendLibnotify::notifyAction(
    NotifyNotification *notify,
    gchar *action, gpointer userData)
{
    if(boost::iequals(action, "view")) {
        pid_t pid;
        if((pid = fork()) == 0) {
            // search sync-ui from $PATH
            if(execlp("sync-ui", "sync-ui", (const char*)0) < 0) {
                exit(0);
            }
        }
    }
    // if dismissed, ignore.
}

bool NotificationBackendLibnotify::init()
{
    bindtextdomain (GETTEXT_PACKAGE, SYNCEVOLUTION_LOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);

#ifdef NOTIFY_COMPATIBILITY
    void *dlhandle = NULL;
    int i;
    for (i = 1; i <= 4; i++) {
        dlhandle = dlopen(StringPrintf("libnotify.so.%d", i).c_str(), RTLD_LAZY|RTLD_GLOBAL);
        if (!dlhandle) {
            SE_LOG_DEBUG(NULL, NULL, "failed to load libnotify.so.%d: %s", i, dlerror());
        } else {
            break;
        }
    }
    if (!dlhandle) {
        return false;
    }

#define LOOKUP(_x) ((_x = reinterpret_cast<typeof(_x)>(dlsym(dlhandle, #_x))) || \
                    NotFound(#_x))

    if (!LOOKUP(notify_init) ||
        !LOOKUP(notify_get_server_caps) ||
        !LOOKUP(notify_notification_new) ||
        !LOOKUP(notify_notification_add_action) ||
        !LOOKUP(notify_notification_clear_actions) ||
        !LOOKUP(notify_notification_close) ||
        !LOOKUP(notify_notification_show)) {
        return false;
    }
    SE_LOG_DEBUG(NULL, NULL, "using libnotify.so.%d", i);
#endif

    m_initialized = notify_init("SyncEvolution");
    if(m_initialized) {
        GStringListFreeCXX list(notify_get_server_caps());
        BOOST_FOREACH (const char *cap, list) {
            if(boost::iequals(cap, "actions")) {
                m_acceptsActions = true;
            }
        }
        return true;
    }

    return false;
}

void NotificationBackendLibnotify::publish(
    const std::string& summary, const std::string& body,
    const std::string& viewParams)
{
    if(!m_initialized)
        return;

    if(m_notification) {
        notify_notification_clear_actions(m_notification);
        notify_notification_close(m_notification, NULL);
    }
#ifndef NOTIFY_CHECK_VERSION
# define NOTIFY_CHECK_VERSION(_x,_y,_z) 0
#endif
#if !NOTIFY_CHECK_VERSION(0,7,0) || defined(NOTIFY_COMPATIBILITY)
    m_notification = notify_notification_new(summary.c_str(), body.c_str(), NULL, NULL);
#else
    m_notification = notify_notification_new(summary.c_str(), body.c_str(), NULL);
#endif
    //if actions are not supported, don't add actions
    //An example is Ubuntu Notify OSD. It uses an alert box
    //instead of a bubble when a notification is appended with actions.
    //the alert box won't be closed until user inputs.
    //so disable it in case of no support of actions
    if(m_acceptsActions) {
        notify_notification_add_action(m_notification, "view",
                                       _("View"), notifyAction,
                                       (gpointer)viewParams.c_str(),
                                       NULL);
        // Use "default" as ID because that is what mutter-moblin
        // recognizes: it then skips the action instead of adding it
        // in addition to its own "Dismiss" button (always added).
        notify_notification_add_action(m_notification, "default",
                                       _("Dismiss"), notifyAction,
                                       (gpointer)viewParams.c_str(),
                                       NULL);
    }
    notify_notification_show(m_notification, NULL);
}

SE_END_CXX

#endif // HAS_NOTIFY

