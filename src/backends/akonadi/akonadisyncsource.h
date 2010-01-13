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

#ifndef AKONADISYNCSOURCE_H
#define AKONADISYNCSOURCE_H

#include "config.h"

#ifdef ENABLE_AKONADI

#include <Akonadi/Collection>
#include <Akonadi/Item>

#include <QtCore/QDateTime>

#include <syncevo/TrackingSyncSource.h>

SE_BEGIN_CXX

class TimeTrackingObserver;

/**
 * General purpose Akonadi Sync Source. Choosing the type of data is
 * done when instantiating it, using the Akonadi MIME subtypes.
 * Payload is always using the native Akonadi format (no special "raw"
 * and "engine" formats).
 *
 * Change tracking is done via the item uid/revision attributes.
 * 
 *
 * Databases (collections in Akonadi terminology) are selected via
 * their int64 ID number.
 */
class AkonadiSyncSource : public TrackingSyncSource
{
public:
    /**
     * @param submime     the MIME type string used by Akonadi
     *                    to identify contacts, tasks, events, etc.
     * @param params      the SyncEvolution source parameters
     */
    AkonadiSyncSource(const char *submime,
                      const SyncSourceParams &params);
    virtual ~AkonadiSyncSource();

    /* methods that have to be implemented to complete TrackingSyncSource */
    virtual Databases getDatabases();
    virtual void open();
    virtual void listAllItems(SyncSourceRevisions::RevisionMap_t &revisions);
    virtual InsertItemResult insertItem(const std::string &luid, const std::string &item, bool raw);
    virtual void readItem(const std::string &luid, std::string &item, bool raw);
    virtual void removeItem(const string &luid);
    virtual void close();
    virtual bool isEmpty();
private:
    Akonadi::Collection m_collection;
    const std::string m_subMime;

    void start();
};

class AkonadiContactSource : public AkonadiSyncSource
{
 public:
    AkonadiContactSource(const SyncSourceParams &params) :
        AkonadiSyncSource("text/vcard", params)
    {}

    virtual const char *getMimeType() const { return "text/vcard"; }
    virtual const char *getMimeVersion() const { return "3.0"; }

    void getSynthesisInfo(SynthesisInfo &info,
                          XMLConfigFragments &fragments)
    {
        TrackingSyncSource::getSynthesisInfo(info, fragments);

        /** enable the KDE X- extensions in the Synthesis<->backend conversion */
        info.m_backendRule = "KDE";

        /*
         * Disable the default VCARD_BEFOREWRITE_SCRIPT_EVOLUTION.
         * If any KDE-specific transformations via such a script
         * are needed, it can be named here and then defined by appending
         * to the fragments.
         */
        info.m_beforeWriteScript = ""; // "$VCARD_BEFOREWRITE_SCRIPT_KDE;";
        // fragments.m_datatypes["VCARD_BEFOREWRITE_SCRIPT_KDE"] = "<macro name=\"VCARD_BEFOREWRITE_SCRIPT_KDE\"><![DATA[ ... ]]></macro>";
    }
};

class AkonadiCalendarSource : public AkonadiSyncSource
{
 public:
    AkonadiCalendarSource(const SyncSourceParams &params) :
        AkonadiSyncSource("application/x-vnd.akonadi.calendar.event", params)
    {}

    // TODO: the items are expected to be complete VCALENDAR with
    // all necessary VTIMEZONEs and one VEVENT (here) resp. VTODO
    // (AkonadiTodoSource). Not sure what we get from Akonadi.
    virtual const char *getMimeType() const { return "text/calendar"; }
    virtual const char *getMimeVersion() const { return "2.0"; }
};

class AkonadiTaskSource : public AkonadiSyncSource
{
 public:
    AkonadiTaskSource(const SyncSourceParams &params) :
        AkonadiSyncSource("text/x-vnd.akonadi.calendar.todo", params)
    {}

    virtual const char *getMimeType() const { return "text/calendar"; }
    virtual const char *getMimeVersion() const { return "2.0"; }
};

class AkonadiMemoSource : public AkonadiSyncSource
{
 public:
    AkonadiMemoSource(const SyncSourceParams &params) :
        AkonadiSyncSource("text/x-vnd.akonadi.calendar.journal", params)
    {}

    // TODO: the AkonadiMemoSource is expected to import/export
    // plain text with the summary in the first line; currently
    // the AkonadiSyncSource will use VJOURNAL
    virtual const char *getMimeType() const { return "text/plain"; }
    virtual const char *getMimeVersion() const { return "1.0"; }
};

SE_END_CXX

#endif // ENABLE_AKONADI
#endif // AKONADISYNCSOURCE_H
