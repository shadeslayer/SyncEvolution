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

#include "session-common.h"

#include <syncevo/SyncContext.h>
#include <syncevo/UserInterface.h>
#include <syncevo/SuspendFlags.h>

#include <boost/signals2.hpp>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

class SessionHelper;

namespace SessionCommon {
    struct SyncParams;
}

/**
 * Maps sync events to D-Bus signals in SessionHelper.
 * Does password requests by sending out a request for
 * them via SessionHelper and waiting until a reply (positive
 * or negative) is received.
 */
class DBusSync : public SyncContext, private UserInterface
{
    SessionHelper &m_helper;
    SessionCommon::SyncParams m_params;
    bool m_waiting;
    boost::function<void (const std::string &)> m_passwordSuccess;
    boost::function<void ()> m_passwordFailure;
    std::string m_passwordDescr;
    boost::signals2::connection m_parentWatch;
    boost::signals2::connection m_suspendFlagsWatch;

    void suspendFlagsChanged(SuspendFlags &flags);

public:
    DBusSync(const SessionCommon::SyncParams &params,
             SessionHelper &helper);
    ~DBusSync();

    /** to be called by SessionHelper when it gets a response via D-Bus */
    void passwordResponse(bool timedOut, bool aborted, const std::string &password);

protected:
    virtual boost::shared_ptr<TransportAgent> createTransportAgent();
    virtual void displaySyncProgress(sysync::TProgressEventEnum type,
                                     int32_t extra1, int32_t extra2, int32_t extra3);
    virtual void displaySourceProgress(sysync::TProgressEventEnum type,
                                       SyncSource &source,
                                       int32_t extra1, int32_t extra2, int32_t extra3);
    virtual void reportStepCmd(sysync::uInt16 stepCmd);
    virtual void syncSuccessStart();
    string askPassword(const string &passwordName,
                       const string &descr,
                       const ConfigPasswordKey &key);
    virtual void askPasswordAsync(const std::string &passwordName,
                                  const std::string &descr,
                                  const ConfigPasswordKey &key,
                                  const boost::function<void (const std::string &)> &success,
                                  const boost::function<void ()> &failureException);

    virtual bool savePassword(const std::string &passwordName,
                              const std::string &password,
                              const ConfigPasswordKey &key);
    virtual void readStdin(std::string &content);
};

SE_END_CXX

#endif // DBUS_SYNC_H
