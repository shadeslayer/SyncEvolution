/*
 * Copyright (C) 2010 Intel Corporation
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

#include "dbustypes.h"


// Marshall the SyncDatabase data into a D-BUS argument
QDBusArgument &operator<<(QDBusArgument &argument, const SyncDatabase &d)
{
    argument.beginStructure();
    argument << d.name << d.source << d.flag;
    argument.endStructure();
    return argument;
}

// Retrieve the SyncDatabase data from the D-BUS argument
const QDBusArgument &operator>>(const QDBusArgument &argument, SyncDatabase &d)
{
    argument.beginStructure();
    argument >> d.name >> d.source >> d.flag;
    argument.endStructure();
    return argument;
}

// Marshall the SyncProgress data into a D-BUS argument
QDBusArgument &operator<<(QDBusArgument &argument, const SyncProgress &p)
{
    argument.beginStructure();
    argument << p.prepareCount << p.prepareTotal << p.sendCount << p.sendTotal << p.recieveCount \
             << p.recieveTotal;
    argument.endStructure();
    return argument;
}

// Retrieve the SyncProgress data from the D-BUS argument
const QDBusArgument &operator>>(const QDBusArgument &argument, SyncProgress &p)
{
    argument.beginStructure();
    argument >> p.prepareCount >> p.prepareTotal >> p.sendCount >> p.sendTotal >> p.recieveCount \
             >> p.recieveTotal;
    argument.endStructure();
    return argument;
}

// Marshall the SyncStatus data into a D-BUS argument
QDBusArgument &operator<<(QDBusArgument &argument, const SyncStatus &s)
{
    argument.beginStructure();
    argument << s.mode << s.status << s.error;
    argument.endStructure();
    return argument;
}

// Retrieve the SyncStatus data from the D-BUS argument
const QDBusArgument &operator>>(const QDBusArgument &argument, SyncStatus &s)
{
    argument.beginStructure();
    argument >> s.mode >> s.status >> s.error;
    argument.endStructure();
    return argument;
}
