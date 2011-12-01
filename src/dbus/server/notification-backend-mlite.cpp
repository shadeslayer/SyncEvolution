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

#include <config.h>

#ifdef HAS_MLITE

#include "notification-backend-mlite.h"

#include <mlite/MNotification>
#include <QString>

SE_BEGIN_CXX

/// TODO: these should really be in a common place.
#define SYNCEVOLUTION_SERVICE_NAME "org.syncevolution"
#define SYNCEVOLUTION_OBJECT_PATH  "/org/syncevolution/Server"
#define SYNCEVOLUTION_INTERFACE    "org.syncevolution.Server"

NotificationBackendMLite::NotificationBackendMLite()
{
}

NotificationBackendMLite::~NotificationBackendMLite()
{
}

bool NotificationBackendMLite::init()
{
    return true;
}

void NotificationBackendMLite::publish(
    const std::string& summary, const std::string& body,
    const std::string& viewParams)
{
    MNotification n ("Sync");

    n.setSummary(QString::fromStdString(summary));
    n.setBody(QString::fromStdString(body));
    n.setImage("image://themedimage/icons/settings/sync");

    MRemoteAction action(SYNCEVOLUTION_SERVICE_NAME,
                         SYNCEVOLUTION_OBJECT_PATH,
                         SYNCEVOLUTION_INTERFACE,
                         "NotificationAction");
    n.setAction(action);

    n.publish();
}

SE_END_CXX

#endif
