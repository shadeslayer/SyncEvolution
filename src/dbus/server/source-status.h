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

#ifndef SOURCE_STATUS_H
#define SOURCE_STATUS_H

#include "gdbus-cxx-bridge.h"

#include <syncevo/declarations.h>
SE_BEGIN_CXX
struct SourceStatus
{
    SourceStatus() :
        m_mode("none"),
        m_status("idle"),
        m_error(0)
    {}
    void set(const std::string &mode, const std::string &status, uint32_t error)
    {
        m_mode = mode;
        m_status = status;
        m_error = error;
    }

    std::string m_mode;
    std::string m_status;
    uint32_t m_error;
};

SE_END_CXX

namespace GDBusCXX {
using namespace SyncEvo;
template<> struct dbus_traits<SourceStatus> :
    public dbus_struct_traits<SourceStatus,
                              dbus_member<SourceStatus, std::string, &SourceStatus::m_mode,
                              dbus_member<SourceStatus, std::string, &SourceStatus::m_status,
                              dbus_member_single<SourceStatus, uint32_t, &SourceStatus::m_error> > > >
{};
}

#endif // SOURCE_STATUS_H
