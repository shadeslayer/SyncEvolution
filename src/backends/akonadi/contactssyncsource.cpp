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

#include "contactssyncsource.h"
#include "settings.h"

ContactsSyncSource::ContactsSyncSource(TimeTrackingObserver *observer,
                                       ContactsSyncSourceConfig *config,
                                       SyncManagerConfig *managerConfig)
    : AkonadiSyncSource(observer, config, managerConfig)
{
    m_collectionId = Settings::self()->contactsCollectionId();
    m_lastSyncTime = Settings::self()->contactsLastSyncTime();
}

#include "moc_contactssyncsource.cpp"
