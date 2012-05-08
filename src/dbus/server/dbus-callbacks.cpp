/*
 * Copyright (C) 2012 Intel Corporation
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

#include "dbus-callbacks.h"
#include "session-common.h"
#include <gdbus-cxx-bridge.h>

#include <syncevo/util.h>

SE_BEGIN_CXX

uint32_t dbusErrorCallback(const boost::shared_ptr<GDBusCXX::Result> &result)
{
    try {
        // If there is no pending exception, the process will abort
        // with "terminate called without an active exception";
        // dbusErrorCallback() should only be called when there is
        // a pending exception.
        // TODO: catch this misuse in a better way
        throw;
    } catch (...) {
        // let D-Bus parent log the error
        std::string explanation;
        uint32_t error = Exception::handle(explanation, HANDLE_EXCEPTION_NO_ERROR);
        try {
            result->failed(GDBusCXX::dbus_error(SessionCommon::SERVER_IFACE, explanation));
        } catch (...) {
            // Ignore failures while sending the reply. This can
            // happen when our caller dropped the connection before we
            // could reply.
            Exception::handle(HANDLE_EXCEPTION_NO_ERROR);
        }
        return error;
    }
    // keep compiler happy
    return 500;
}

ErrorCb_t createDBusErrorCb(const boost::shared_ptr<GDBusCXX::Result> &result)
{
    return boost::bind(dbusErrorCallback, result);
}

SE_END_CXX
