/*
    Copyright (c) 2009 Sascha Peilicke <sasch.pe@gmx.de>

    This application is free software; you can redistribute it and/or modify it
    under the terms of the GNU Library General Public License as published by
    the Free Software Foundation; either version 2 of the License, or (at your
    option) any later version.

    This application is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Library General Public
    License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this application; see the file COPYING.LIB.  If not, write to the
    Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
    02110-1301, USA.
*/

#ifndef EVENTSSYNCSOURCE_H
#define EVENTSSYNCSOURCE_H

#include "akonadisyncsource.h"

/**
 *
 */
class EventsSyncSourceConfig : public AkonadiSyncSourceConfig
{
    Q_OBJECT

public:
    EventsSyncSourceConfig()
        : AkonadiSyncSourceConfig(Settings::self()->eventsLastSyncTime().toTime_t(),
                                  Settings::self()->eventsRemoteDatabaseName().toLatin1())
    {
        setName(Settings::self()->eventsCollectionName().toLatin1());
        setType("text/x-vcalendar");
        setSupportedTypes("text/x-vcalendar:");
    }
};

/**
 *
 */
class EventsSyncSource : public AkonadiSyncSource
{
    Q_OBJECT

public:
    EventsSyncSource(TimeTrackingObserver *observer,
                     EventsSyncSourceConfig *config,
                     SyncManagerConfig *managerConfig);
};

#endif
