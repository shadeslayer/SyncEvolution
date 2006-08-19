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

#include "config.h"

#ifdef ENABLE_ECAL

#include "EvolutionCalendarSource.h"
#include "EvolutionSmartPtr.h"

#include <common/base/Log.h>
#include <common/vocl/VObject.h>
#include <common/vocl/VConverter.h>

static const string
EVOLUTION_CALENDAR_PRODID("PRODID:-//ACME//NONSGML SyncEvolution//EN"),
EVOLUTION_CALENDAR_VERSION("VERSION:2.0");

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

char *EvolutionCalendarSource::authenticate(const char *prompt,
                                            const char *key)
{
    string user, passwd;
    getAuthentication(user, passwd);
    
    LOG.debug("%s: authentication requested, prompt \"%s\", key \"%s\" => %s",
              getName(), prompt, key,
              passwd.size() ? "returning configured password" : "no password configured");
    return passwd.size() ? strdup(passwd.c_str()) : NULL;
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
        throw runtime_error(string(getName()) + ": no such calendar: '" + m_id + "'");
    }

    m_calendar.set(e_cal_new(source, m_type), "calendar");

    e_cal_set_auth_func(m_calendar, eCalAuthFunc, this);
    
    if (!e_cal_open(m_calendar, TRUE, &gerror)) {
        throwError( "opening calendar", gerror );
    }
}

void EvolutionCalendarSource::getChanges()
{
    GError *gerror = NULL;
    bool foundChanges;

    m_newItems.clear();
    m_updatedItems.clear();
    m_deletedItems.clear();

    // This is repeated in a loop because it was observed that in
    // Evolution 2.0.6 the deleted items were not all reported in one
    // chunk. Instead each invocation of e_cal_get_changes() reported
    // additional changes.
    do {
        GList *nextItem;

        foundChanges = false;
        if (!e_cal_get_changes(m_calendar, (char *)m_changeId.c_str(), &nextItem, &gerror)) {
            throwError( "reading changes", gerror );
        }
        while (nextItem) {
            ECalChange *ecc = (ECalChange *)nextItem->data;

            if (ecc->comp) {
                const char *uid = NULL;

                // does not have a return code which could be checked
                e_cal_component_get_uid(ecc->comp, &uid);

                if (uid) {
                    switch (ecc->type) {
                    case E_CAL_CHANGE_ADDED:
                        if (m_newItems.addItem(uid)) {
                            foundChanges = true;
                        }
                        break;
                    case E_CAL_CHANGE_MODIFIED:
                        if (m_updatedItems.addItem(uid)) {
                            foundChanges = true;
                        }
                        break;
                    case E_CAL_CHANGE_DELETED:
                        if (m_deletedItems.addItem(uid)) {
                            foundChanges = true;
                        }
                        break;
                    }
                }
            }

            nextItem = nextItem->next;
        }
    } while(foundChanges);
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
        getChanges();
    }
}

void EvolutionCalendarSource::endSyncThrow()
{
    if (m_isModified) {
        getChanges();
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
        const char *uid;
        e_cal_component_get_uid(E_CAL_COMPONENT(nextItem->data), &uid);
        out << retrieveItemAsString(uid);
        out << "\r\n";
        nextItem = nextItem->next;
    }
}

SyncItem *EvolutionCalendarSource::createItem( const string &uid, SyncState state )
{
    logItem( uid, "extracting from EV" );
        
    string icalstr = retrieveItemAsString(uid);

    auto_ptr<SyncItem> item(new SyncItem(uid.c_str()));
    item->setData(icalstr.c_str(), icalstr.size() + 1);
    item->setDataType("text/calendar");
    item->setModificationTime(0);
    item->setState(state);

    return item.release();
}

void EvolutionCalendarSource::setItemStatusThrow(const char *key, int status)
{
    switch (status) {
     case STC_CONFLICT_RESOLVED_WITH_SERVER_DATA: {
         LOG.error("%s: calendar item %.80s: conflict, will be replaced by server\n",
                   getName(), key);

        // uids make the item unique, so it cannot be copied;
        // the following code does not work:
        //
        // ECalComponent *comp = retrieveItem(key);
        // ECalComponent *copy = e_cal_component_clone(comp);
        // if (!copy) {
        //    LOG.error("copying %s: making copy failed\n", key);
        // }
        break;
     }
     default:
        EvolutionSyncSource::setItemStatusThrow(key, status);
        break;
    }
}

int EvolutionCalendarSource::addItemThrow(SyncItem& item)
{
    return insertItem(item, false);
}

int EvolutionCalendarSource::insertItem(SyncItem& item, bool update)
{
    bool fallback = false;
    eptr<char> data;
    data.set((char *)malloc(item.getDataSize() + 1), "copy of item");
    memcpy(data, item.getData(), item.getDataSize());
    data[item.getDataSize()] = 0;
    eptr<icalcomponent> icomp(icalcomponent_new_from_string(data));

    if( !icomp ) {
        throwError( string( "parsing ical" ) + (char *)data,
                    NULL );
    }

    GError *gerror = NULL;
    char *uid = NULL;
    int status = STC_OK;

    // insert before adding/updating the event so that the new VTIMEZONE is
    // immediately available should anyone want it
    for (icalcomponent *tcomp = icalcomponent_get_first_component(icomp, ICAL_VTIMEZONE_COMPONENT);
         tcomp;
         tcomp = icalcomponent_get_next_component(icomp, ICAL_VTIMEZONE_COMPONENT)) {
        eptr<icaltimezone> zone(icaltimezone_new(), "icaltimezone");
        icaltimezone_set_component(zone, tcomp);

        GError *gerror = NULL;
        gboolean success = e_cal_add_timezone(m_calendar, zone, &gerror);
        if (!success) {
            throwError(string("error adding VTIMEZONE ") + icaltimezone_get_tzid(zone),
                       gerror);
        }
    }

    // the component to update/add must be the
    // ICAL_VEVENT/VTODO_COMPONENT of the item,
    // e_cal_create/modify_object() fail otherwise
    icalcomponent *subcomp = icalcomponent_get_first_component(icomp,
                                                               getCompType());
    if (!subcomp) {
        throw runtime_error("cannot extract event");
    }
    
    if (!update) {
        if(!e_cal_create_object(m_calendar, subcomp, &uid, &gerror)) {
            if (gerror->domain == E_CALENDAR_ERROR &&
                gerror->code == E_CALENDAR_STATUS_OBJECT_ID_ALREADY_EXISTS) {
                // Deal with error due to adding already existing item, that can happen,
                // for example with a "dumb" server which cannot pair items by UID.
                update = true;
                fallback = true;
                g_clear_error(&gerror);
            } else {
                throwError( "storing new calendar item", gerror );
            }
        } else {
            if (uid) {
                item.setKey(uid);
            }
        }
    }

    if (update) {
        if (!e_cal_modify_object(m_calendar, subcomp, CALOBJ_MOD_ALL, &gerror)) {
            throwError(string("updating calendar item") + item.getKey(), gerror);
        }
        string uid = getCompUID(subcomp);
        if (uid.size()) {
            item.setKey(uid.c_str());
        }
        if (fallback ) {
            status = STC_CONFLICT_RESOLVED_WITH_MERGE;
        }
    }

    return status;
}

int EvolutionCalendarSource::updateItemThrow(SyncItem& item)
{
    return insertItem(item, true);
}

int EvolutionCalendarSource::deleteItemThrow(SyncItem& item)
{
    int status = STC_OK;
    GError *gerror = NULL;

    if (!e_cal_remove_object(m_calendar, item.getKey(), &gerror)) {
        throwError( string( "deleting calendar item" ) + item.getKey(),
                    gerror );
    }
    return status;
}

void EvolutionCalendarSource::logItem(const string &uid, const string &info)
{
    if (LOG.getLevel() >= LOG_LEVEL_INFO) {
        LOG.info("%s: %s: %s", getName(), uid.c_str(), info.c_str());
    }
}

void EvolutionCalendarSource::logItem( SyncItem &item, const string &info )
{
    if (LOG.getLevel() >= LOG_LEVEL_INFO) {
        LOG.info("%s: %s: %s", getName(), item.getKey(), info.c_str());
    }
}

icalcomponent *EvolutionCalendarSource::retrieveItem(const string &uid)
{
    GError *gerror = NULL;
    icalcomponent *comp;

    if (!e_cal_get_object(m_calendar,
                          uid.c_str(),
                          NULL,
                          &comp,
                          &gerror)) {
        throwError(string("retrieving item: ") + uid, gerror);
    }

    return comp;
}

string EvolutionCalendarSource::retrieveItemAsString(const string &uid)
{
    eptr<icalcomponent> comp(retrieveItem(uid));
    eptr<char> icalstr;

    icalstr = e_cal_get_component_as_string(m_calendar, comp);
    return string(icalstr);
}

string EvolutionCalendarSource::getCompUID(icalcomponent *icomp)
{
    // figure out what the UID is
    icalproperty *iprop = icalcomponent_get_first_property(icomp,
                                                           ICAL_UID_PROPERTY);
    if (!iprop) {
        throw runtime_error("cannot extract UID property");
    }
    const char *uid = icalproperty_get_uid(iprop);
    return string(uid ? uid : "");
}

#endif /* ENABLE_ECAL */
