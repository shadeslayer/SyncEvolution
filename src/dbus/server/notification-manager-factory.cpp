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

#include "notification-manager-factory.h"

#include "notification-backend-noop.h"
#include "notification-backend-mlite.h"
#include "notification-backend-libnotify.h"
#include "notification-manager.h"

#include <unistd.h>

SE_BEGIN_CXX

#define SYNC_UI_PATH "/usr/bin/sync-ui"

boost::shared_ptr<NotificationManagerBase> NotificationManagerFactory::createManager()
{
    boost::shared_ptr<NotificationManagerBase> mgr;

    /* Detect what kind of manager we need: if /usr/bin/sync-ui
     * doesn't exists, we shall use the MLite backend; otherwise, if
     * libnotify is enabled, we shall use the libnotify backend;
     * if everything fails, then we'll use the no-op backend.
     */
    if(access(SYNC_UI_PATH, F_OK) != 0) {
        // i.e. it does not exist
#if defined(HAS_MLITE)
        mgr.reset(new NotificationManager<NotificationBackendMLite>());
#elif defined(HAS_NOTIFY)
        mgr.reset(new NotificationManager<NotificationBackendLibnotify>());
#endif
    } else { // it exists
#if defined(HAS_NOTIFY)
        mgr.reset(new NotificationManager<NotificationBackendLibnotify>());
#endif
    }

    // Fallback if no manager was created
    if(mgr.get() == 0)
        mgr.reset(new NotificationManager<NotificationBackendNoop>());

    return mgr;
}

SE_END_CXX

