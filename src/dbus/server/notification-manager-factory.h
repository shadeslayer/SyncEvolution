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

#ifndef __NOTIFICATION_MANAGER_FACTORY_H
#define __NOTIFICATION_MANAGER_FACTORY_H

#include "syncevo/declarations.h"
#include "notification-manager.h"

#include <boost/shared_ptr.hpp>

SE_BEGIN_CXX

class NotificationBackendBase;

class NotificationManagerFactory {
    public:
        /** Creates the appropriate NotificationManager for the current
         * platform.
         * Note: NotificationManagerFactory does not take ownership of
         * the returned pointer: the user must delete it when done.
         */
        static boost::shared_ptr<NotificationManagerBase> createManager();
};

SE_END_CXX

#endif

