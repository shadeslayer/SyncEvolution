/*
 * Copyright (C) 2005 Patrick Ohly
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <memory>
using namespace std;

#include "EvolutionCalendarSource.h"

#include <common/base/Log.h>
#include <common/vocl/VObject.h>
#include <common/vocl/VConverter.h>

EvolutionCalendarSource::EvolutionCalendarSource( ECalSourceType type,
                                                  const string &name,
                                                  const string &changeId,
                                                  const string &id ) :
    EvolutionSyncSource(name, changeId, id),
    m_type(type)
{
}

EvolutionCalendarSource::EvolutionCalendarSource( const EvolutionCalendarSource &other ) :
    EvolutionSyncSource(other),
    m_type(other.m_type)
{
}

EvolutionSyncSource::sources EvolutionCalendarSource::getSyncBackends()
{
    ESourceList *sources = NULL;
    GError *gerror = NULL;

    if (!e_cal_get_sources(&sources, m_type, &gerror)) {
        throwError("unable to access calendars", gerror);
    }

    EvolutionSyncSource::sources result;

    for (GSList *g = e_source_list_peek_groups (sources); g; g = g->next) {
        ESourceGroup *group = E_SOURCE_GROUP (g->data);
        for (GSList *s = e_source_group_peek_sources (group); s; s = s->next) {
            ESource *source = E_SOURCE (s->data);
            result.push_back( EvolutionSyncSource::source( e_source_peek_name(source),
                                                           e_source_get_uri(source) ) );
        }
    }
    return result;
}

void EvolutionCalendarSource::open()
{
    ESourceList *sources;
    GError *gerror = NULL;

    if (!e_cal_get_sources(&sources, m_type, &gerror)) {
        throwError("unable to access calendars", gerror);
    }
    
    ESource *source = findSource(sources, m_id);
    if (!source) {
        throw string(getName()) + ": no such calendar: '" + m_id + "'";
    }

    m_calendar.set(e_cal_new(source, m_type), "calendar");

    if (!e_cal_open(m_calendar, TRUE, &gerror)) {
        throwError( "opening calendar", gerror );
    }
}

void EvolutionCalendarSource::beginSyncThrow(bool needAll,
                                             bool needPartial,
                                             bool deleteLocal)
{
    GError *gerror = NULL;

    if (deleteLocal) {
        GList *nextItem;
        
        if (!e_cal_get_object_list_as_comp(m_calendar,
                                           "(contains? \"any\" \"\")",
                                           &nextItem,
                                           &gerror)) {
            throwError( "reading all items", gerror );
        }
        while (nextItem) {
            const char *uid;

            e_cal_component_get_uid(E_CAL_COMPONENT(nextItem->data), &uid);
            if (!e_cal_remove_object(m_calendar, uid, &gerror) ) {
                throwError( string( "deleting calendar entry" ) + uid,
                            gerror );
            }
            nextItem = nextItem->next;
        }
    }

    if (needAll) {
        GList *nextItem;
        
        if (!e_cal_get_object_list_as_comp(m_calendar, "(contains? \"any\" \"\")", &nextItem, &gerror)) {
            throwError( "reading all items", gerror );
        }
        while (nextItem) {
            const char *uid;

            e_cal_component_get_uid(E_CAL_COMPONENT(nextItem->data), &uid);
            m_allItems.addItem(uid);
            nextItem = nextItem->next;
        }
    }

    if (needPartial) {
        GList *nextItem;

        if (!e_cal_get_changes(m_calendar, (char *)m_changeId.c_str(), &nextItem, &gerror)) {
            throwError( "reading changes", gerror );
        }
        while (nextItem) {
            ECalChange *ecc = (ECalChange *)nextItem->data;
            const char *uid;

            e_cal_component_get_uid(ecc->comp, &uid);
            switch (ecc->type) {
             case E_CAL_CHANGE_ADDED:
                m_newItems.addItem(uid);
                break;
             case E_CAL_CHANGE_MODIFIED:
                m_updatedItems.addItem(uid);
                break;
             case E_CAL_CHANGE_DELETED:
                m_deletedItems.addItem(uid);
                break;
            }
            nextItem = nextItem->next;
        }
    }
}

void EvolutionCalendarSource::endSyncThrow()
{
    if (m_isModified) {
        GError *gerror = NULL;
        GList *nextItem;
        // move change_id forward so that our own changes are not listed the next time
        if (!e_cal_get_changes(m_calendar, (char *)m_changeId.c_str(), &nextItem, &gerror )) {
            throwError( "reading changes", gerror );
        }
    }
    resetItems();
    m_isModified = false;
}

void EvolutionCalendarSource::close()
{
    endSyncThrow();
    m_calendar = NULL;
}

void EvolutionCalendarSource::exportData(ostream &out)
{
    GList *nextItem;
    GError *gerror = NULL;

    if (!e_cal_get_object_list_as_comp(m_calendar,
                                       "(contains? \"any\" \"\")",
                                       &nextItem,
                                       &gerror)) {
        throwError( "reading all items", gerror );
    }
    while (nextItem) {
        gptr<char> icalstr;
        ECalComponent *comp = E_CAL_COMPONENT(nextItem->data);

        e_cal_component_commit_sequence(comp);
        icalstr = e_cal_component_get_as_string(comp);

        out << (const char *)icalstr << "\r\n\r\n";
        nextItem = nextItem->next;
    }
}

SyncItem *EvolutionCalendarSource::createItem( const string &uid, SyncState state )
{
    // this function must never throw an exception
    // because it is called inside the Sync4j C++ API library
    // which cannot handle exceptions
    try {
        logItem( uid, "extracting from EV" );
        
        string icalstr;

        // the item itself is a VEVENT and needs to be embedded
        // inside a VCALENDAR
        icalstr += "BEGIN:VCALENDAR\r\n";
        icalstr += retrieveItemAsString(uid);
        icalstr += "END:VCALENDAR\r\n";

        auto_ptr<SyncItem> item(new SyncItem(uid.c_str()));
        item->setData(icalstr.c_str(), icalstr.size() + 1);
        // TODO: take MIME type from config
        item->setDataType("text/x-vcalendar");
        item->setModificationTime(0);
        item->setState(state);

        return item.release();
    } catch (...) {
        m_hasFailed = true;
    }

    return NULL;
}

void EvolutionCalendarSource::setItemStatusThrow(const char *key, int status)
{
    switch (status) {
     case STC_CONFLICT_RESOLVED_WITH_SERVER_DATA: {
        // make a copy before allowing the server to overwrite it
        LOG.error("calendar item %.80s: conflict, will be replaced by server - creating copy\n",
                  key);

        ECalComponent *comp = retrieveItem(key);
        ECalComponent *copy = e_cal_component_clone(comp);
        if (!copy) {
            LOG.error("copying %s: making copy failed\n", key);
        }
        break;
     }
     default:
        EvolutionSyncSource::setItemStatusThrow(key, status);
        break;
    }
}

void EvolutionCalendarSource::addItemThrow(SyncItem& item)
{
    icalcomponent *icomp = newFromItem(item);
    GError *gerror = NULL;
    char *uid = NULL;
    if (!e_cal_create_object(m_calendar, icomp, &uid, &gerror)) {
        if (gerror->domain == E_CALENDAR_ERROR &&
            gerror->code == E_CALENDAR_STATUS_OBJECT_ID_ALREADY_EXISTS) {
            // Deal with error due to adding already existing item, that can happen,
            // for example with a "dumb" server which cannot pair items by UID.
            //
            // TODO: alert the server? duplicate?
            //
            const char *olduid = getCompUID(icomp);
            if (!olduid) {
                throw "cannot extract UID to remove the item which is in the way";
            }
            item.setKey(olduid);

#if 0
            // overwrite item
            deleteItemThrow(item);
            if (!e_cal_create_object(m_calendar, icomp, &uid, &gerror)) {
                throwError( "storing new calendar item", gerror );
            }
#else
            // update item
            updateItemThrow(item);
            uid = NULL;
#endif
        } else {
            throwError( "storing new calendar item", gerror );
        }
    }
    if (uid) {
        item.setKey(uid);
    }
}

void EvolutionCalendarSource::updateItemThrow(SyncItem& item)
{
    icalcomponent *icomp = newFromItem(item);
    GError *gerror = NULL;
    if (!e_cal_modify_object(m_calendar, icomp, CALOBJ_MOD_ALL, &gerror)) {
        throwError("updating calendar item", gerror);
    }

    const char *uid = getCompUID(icomp);
    if (uid) {
        item.setKey(uid);
    }
}

void EvolutionCalendarSource::deleteItemThrow(SyncItem& item)
{
    GError *gerror = NULL;
    if (!e_cal_remove_object(m_calendar, item.getKey(), &gerror)) {
        throwError( string( "deleting calendar item" ) + item.getKey(),
                    gerror );
    }
}

void EvolutionCalendarSource::logItem(const string &uid, const string &info)
{
    if (LOG.getLevel() >= LOG_LEVEL_INFO) {
        // TODO
        LOG.info("%s: %s", info.c_str(), uid.c_str());
    }
}

void EvolutionCalendarSource::logItem( SyncItem &item, const string &info )
{
    if (LOG.getLevel() >= LOG_LEVEL_INFO) {
        // TODO
        LOG.info("%s: %s", info.c_str(), item.getKey());
    }
}

ECalComponent *EvolutionCalendarSource::retrieveItem(const string &uid)
{
    GError *gerror = NULL;
    GList *singleItem;
    string query;

    query = "(uid? \"";
    query += uid;
    query += "\" )";

    if (!e_cal_get_object_list_as_comp(m_calendar,
                                       query.c_str(),
                                       &singleItem,
                                       &gerror)) {
        throwError(string("executing query: ") + query, gerror);
    }

    if (!singleItem || singleItem->next) {
        throw "got more or less than the single expected item";
    }

    return E_CAL_COMPONENT(singleItem->data);
}

string EvolutionCalendarSource::retrieveItemAsString(const string &uid)
{
    ECalComponent *comp = retrieveItem(uid);
    gptr<char> icalstr;

    e_cal_component_commit_sequence(comp);
    icalstr = e_cal_component_get_as_string(comp);
    return string(icalstr);
}

icalcomponent *EvolutionCalendarSource::newFromItem(SyncItem &item)
{
    gptr<char> data;
    data.set(new char[item.getDataSize() + 1], "copy of item");
    memcpy(data, item.getData(), item.getDataSize());
    data[item.getDataSize()] = 0;
    icalcomponent *icomp = icalcomponent_new_from_string(data);

    if( !icomp ) {
        throwError( string( "parsing ical" ) + (char *)data,
                    NULL );
    }

    // the icomp must be the ICAL_VEVENT/VTODO_COMPONENT of the item,
    // e_cal_create/modify_object() fail otherwise
    icomp = icalcomponent_get_first_component(icomp,
                                              getCompType());
    if (!icomp) {
        throw "cannot extract event";
    }

    return icomp;
}

const char *EvolutionCalendarSource::getCompUID(icalcomponent *icomp)
{
    // figure out what the UID is
    icalproperty *iprop = icalcomponent_get_first_property(icomp,
                                                           ICAL_UID_PROPERTY);
    if (!iprop) {
        throw "cannot extract UID property";
    }
    const char *uid = icalproperty_get_uid(iprop);
    return uid;
}

