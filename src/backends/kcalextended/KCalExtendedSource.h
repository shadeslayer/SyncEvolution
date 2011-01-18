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

#ifndef INCL_KCALEXTENDEDSYNCSOURCE
#define INCL_KCALEXTENDEDSYNCSOURCE

#include <syncevo/SyncSource.h>

#ifdef ENABLE_KCALEXTENDED

#include <boost/noncopyable.hpp>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

class KCalExtendedData;

/**
 * Access contacts stored in KCalExtended.
 *
 * Change tracking is based on time stamps instead of id/revision
 * pairs as in other sources. Items are imported/export as iCalendar 2.0
 * strings. This allows us to implement TestingSyncSource (and thus
 * use client-test). We have to override the begin/end methods
 * to get time stamps recorded as anchors.
 *
 * This class is designed so that no KCalExtended header files are required
 * to include this header file.
 */
class KCalExtendedSource : public TestingSyncSource, private SyncSourceAdmin, private SyncSourceBlob, private SyncSourceRevisions, public SyncSourceLogging, private boost::noncopyable
{
  public:
    KCalExtendedSource(const SyncSourceParams &params);
    ~KCalExtendedSource();

 protected:
    /* implementation of SyncSource interface */
    virtual void open();
    virtual bool isEmpty();
    virtual void close();
    virtual Databases getDatabases();
    virtual void enableServerMode();
    virtual bool serverModeEnabled() const;
    virtual std::string getPeerMimeType() const { return "text/calendar"; }

    /* implementation of SyncSourceSession interface */
    virtual void beginSync(const std::string &lastToken, const std::string &resumeToken);
    virtual std::string endSync(bool success);

    /* implementation of SyncSourceDelete interface */
    virtual void deleteItem(const string &luid);

    /* implementation of SyncSourceSerialize interface */
    virtual std::string getMimeType() const { return "text/calendar"; }
    virtual std::string getMimeVersion() const { return "2.0"; }
    virtual InsertItemResult insertItem(const std::string &luid, const std::string &item);
    virtual void readItem(const std::string &luid, std::string &item);

    /*
     * implementation of SyncSourceRevisions
     *
     * Used for backup/restore (with dummy revision string).
     */
    virtual void listAllItems(RevisionMap_t &revisions);

    /* implementation of SyncSourceLogging */
    virtual std::string getDescription(const string &luid);

 private:
    KCalExtendedData *m_data;
};

SE_END_CXX

#endif // ENABLE_KCALEXTENDED
#endif // INCL_KCALEXTENDEDSYNCSOURCE
