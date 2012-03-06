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

#ifndef DBUS_SYNC_H
#define DBUS_SYNC_H

#include "dbus-user-interface.h"
#include <syncevo/SyncContext.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

class Session;

/**
 * A running sync engine which keeps answering on D-Bus whenever
 * possible and updates the Session while the sync runs.
 */
class DBusSync : public SyncContext, private DBusUserInterface
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

    virtual int sleep(int intervals);

    /**
     * Implement askPassword to retrieve password in gnome-keyring.
     * If not found, then ask it from dbus clients.
     */
    string askPassword(const string &passwordName,
                       const string &descr,
                       const ConfigPasswordKey &key);
};

SE_END_CXX

#endif // DBUS_SYNC_H
