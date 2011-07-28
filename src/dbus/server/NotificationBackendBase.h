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

#ifndef __NOTIFICATION_BACKEND_BASE_H
#define __NOTIFICATION_BACKEND_BASE_H

#include "syncevo/declarations.h"

#include <string>

SE_BEGIN_CXX

class NotificationBackendBase {
    public:
        virtual bool init() = 0;

        virtual void publish(const std::string& summary,
                             const std::string& body,
                             const std::string& viewParams = std::string()) = 0;
};

SE_END_CXX

#endif
