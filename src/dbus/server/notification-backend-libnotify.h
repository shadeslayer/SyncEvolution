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

#ifndef __NOTIFICATION_BACKEND_LIBNOTIFY_H
#define __NOTIFICATION_BACKEND_LIBNOTIFY_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#if HAS_NOTIFY

#include "syncevo/declarations.h"
#include "notification-backend-base.h"

#include <libnotify/notify.h>

SE_BEGIN_CXX

class NotificationBackendLibnotify : public NotificationBackendBase {
    public:
        NotificationBackendLibnotify();
        virtual ~NotificationBackendLibnotify();

        /**
         * Callback for the notification action.
         */
        static void notifyAction(NotifyNotification *notify,
                                 gchar *action, gpointer userData);

        bool init();

        void publish(const std::string& summary, const std::string& body,
                     const std::string& viewParams = std::string());

    private:
        /**
         * Flag to indicate whether libnotify has been successfully
         * initialized.
         */
        bool m_initialized;

        /**
         * Flag to indicate whether libnotify accepts actions.
         */
        bool m_acceptsActions;

        /**
         * The current notification.
         */
        NotifyNotification *m_notification;
};

SE_END_CXX

#endif // HAS_NOTIFY

#endif // __NOTIFICATION_BACKEND_LIBNOTIFY_H

