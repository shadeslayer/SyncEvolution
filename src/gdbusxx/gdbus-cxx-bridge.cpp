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

#include "gdbus-cxx-bridge.h"

namespace boost
{
    void intrusive_ptr_add_ref(GDBusConnection  *con)  { g_object_ref(con); }
    void intrusive_ptr_release(GDBusConnection  *con)  { g_object_unref(con); }
    void intrusive_ptr_add_ref(GDBusMessage     *msg)  { g_object_ref(msg); }
    void intrusive_ptr_release(GDBusMessage     *msg)  { g_object_unref(msg); }
    void intrusive_ptr_add_ref(GDBusPendingCall *call) { g_object_ref(call); }
    void intrusive_ptr_release(GDBusPendingCall *call) { g_object_unref(call); }
}
