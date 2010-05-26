/*
 * Copyright (C) 2007-2009 Patrick Ohly <patrick.ohly@gmx.de>
 * Copyright (C) 2009 Intel Corporation
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

#include "config.h"

#ifdef ENABLE_QTCONTACTS

#include "QtContactsSource.h"

#include <QContact>
#include <QContactManager>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

using namespace QtMobility;

QtContactsSource::QtContactsSource(const SyncSourceParams &params) :
    TrackingSyncSource(params)
{
    m_data = NULL;
}

QtContactsSource::~QtContactsSource()
{
    // delete m_data;
}

void QtContactsSource::open()
{
}

bool QtContactsSource::isEmpty()
{
    return false;
}

void QtContactsSource::close()
{
}

QtContactsSource::Databases QtContactsSource::getDatabases()
{
    Databases result;
    QStringList availableManagers = QContactManager::availableManagers();

    result.push_back(Database("select database via QtContacts Manager URL",
                              "[file://]<path>"));
    return result;
}

void QtContactsSource::listAllItems(RevisionMap_t &revisions)
{
}

void QtContactsSource::readItem(const string &uid, std::string &item, bool raw)
{
}

TrackingSyncSource::InsertItemResult QtContactsSource::insertItem(const string &uid, const std::string &item, bool raw)
{
    return InsertItemResult("",
                            "",
                            false /* true if adding item was turned into update */);
}


void QtContactsSource::removeItem(const string &uid)
{
}

SE_END_CXX

#endif /* ENABLE_QTCONTACTS */

#ifdef ENABLE_MODULES
# include "QtContactsSourceRegister.cpp"
#endif
