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

#ifndef DBUSTYPES_H
#define DBUSTYPES_H

#include <QtCore/QList>
#include <QtCore/QMap>
#include <QtCore/QString>
#include <QtCore/QMetaType>
#include <QtDBus/QtDBus>

struct SyncDatabase
{
    QString name;
    QString source;
    bool flag;
};
Q_DECLARE_METATYPE ( SyncDatabase )

// Marshall the SyncDatabase data into a D-BUS argument
QDBusArgument &operator<<(QDBusArgument &argument, const SyncDatabase &mystruct);
// Retrieve the SyncDatabase data from the D-BUS argument
const QDBusArgument &operator>>(const QDBusArgument &argument, SyncDatabase &mystruct);

struct SyncProgress
{
    QString phase;
    int prepareCount;
    int prepareTotal;
    int sendCount;
    int sendTotal;
    int recieveCount;
    int recieveTotal;
};
Q_DECLARE_METATYPE ( SyncProgress )

// Marshall the SyncProgress data into a D-BUS argument
QDBusArgument &operator<<(QDBusArgument &argument, const SyncProgress &mystruct);
// Retrieve the SyncProgress data from the D-BUS argument
const QDBusArgument &operator>>(const QDBusArgument &argument, SyncProgress &mystruct);

struct SyncStatus
{
    QString mode;
    QString status;
    unsigned int error;
};
Q_DECLARE_METATYPE ( SyncStatus )

// Marshall the SyncStatus data into a D-BUS argument
QDBusArgument &operator<<(QDBusArgument &argument, const SyncStatus &mystruct);
// Retrieve the SyncStatus data from the D-BUS argument
const QDBusArgument &operator>>(const QDBusArgument &argument, SyncStatus &mystruct);

typedef QMap<QString, QString> QStringMap;
typedef QMap<QString, QStringMap > QStringMultiMap;
typedef QList< QStringMap > QArrayOfStringMap;
typedef QList< SyncDatabase > QArrayOfDatabases;
typedef QMap<QString, SyncProgress >  QSyncProgressMap;
typedef QMap<QString, SyncStatus >  QSyncStatusMap;

Q_DECLARE_METATYPE ( QStringMap )
Q_DECLARE_METATYPE ( QStringMultiMap )
Q_DECLARE_METATYPE ( QArrayOfStringMap )
Q_DECLARE_METATYPE ( QArrayOfDatabases )
Q_DECLARE_METATYPE ( QSyncProgressMap )
Q_DECLARE_METATYPE ( QSyncStatusMap )

inline void syncevolution_qt_dbus_register_types() {
    qDBusRegisterMetaType< SyncDatabase >();
    qDBusRegisterMetaType< QStringMap >();
    qDBusRegisterMetaType< QStringMultiMap >();
    qDBusRegisterMetaType< QArrayOfStringMap >();
    qDBusRegisterMetaType< QArrayOfDatabases >();
    qDBusRegisterMetaType< SyncProgress >();
    qDBusRegisterMetaType< QSyncProgressMap >();
    qDBusRegisterMetaType< SyncStatus >();
    qDBusRegisterMetaType< QSyncStatusMap >();
}

#endif   //MYTYPES_H
