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

#ifndef INCL_KDEPLATFORM
#define INCL_KDEPLATFORM

#include <string>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

struct ConfigPasswordKey;

void KDEInitMainSlot(const char *appname);

bool KWalletLoadPasswordSlot(const std::string &passwordName,
                             const std::string &descr,
                             const ConfigPasswordKey &key,
                             std::string &password);

bool KWalletSavePasswordSlot(const std::string &passwordName,
                             const std::string &password,
                             const ConfigPasswordKey &key);
SE_END_CXX

#endif // INCL_KDEPLATFORM
