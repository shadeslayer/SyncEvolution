/*
 * Copyright (C) 2007-2009 Patrick Ohly <patrick.ohly@gmx.de>
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

#ifndef INCL_QTCONTACTSSYNCSOURCE
#define INCL_QTCONTACTSSYNCSOURCE

#include <syncevo/TrackingSyncSource.h>

#ifdef ENABLE_QTCONTACTS

#include <memory>
#include <boost/noncopyable.hpp>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

class QtContactsData;

/**
 * Access contacts stored in QtContacts.
 *
 * This class is designed so that no Qt header files are required
 * to include this header file.
 */
class QtContactsSource : public TrackingSyncSource, public SyncSourceLogging, private boost::noncopyable
{
  public:
    QtContactsSource(const SyncSourceParams &params);
    ~QtContactsSource();

 protected:
    /* implementation of SyncSource interface */
    virtual void open();
    virtual bool isEmpty();
    virtual void close();
    virtual Databases getDatabases();
    virtual std::string getMimeType() const { return "text/vcard"; }
    virtual std::string getMimeVersion() const { return "3.0"; }

    /* implementation of TrackingSyncSource interface */
    virtual void listAllItems(RevisionMap_t &revisions);
    virtual InsertItemResult insertItem(const string &luid, const std::string &item, bool raw);
    void readItem(const std::string &luid, std::string &item, bool raw);
    virtual void removeItem(const string &uid);

    /* implementation of SyncSourceLogging */
    virtual std::string getDescription(const string &luid);

 private:
    QtContactsData *m_data;
};

SE_END_CXX

#endif // ENABLE_QTCONTACTS
#endif // INCL_QTCONTACTSSYNCSOURCE
