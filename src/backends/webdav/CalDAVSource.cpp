/*
 * Copyright (C) 2010 Intel Corporation
 */

#include "CalDAVSource.h"

#ifdef ENABLE_DAV

#include <syncevo/declarations.h>
SE_BEGIN_CXX

// TODO: use EDS backend icalstrdup.c
#define ical_strdup(_x) (_x)

CalDAVSource::CalDAVSource(const SyncSourceParams &params,
                           const boost::shared_ptr<Neon::Settings> &settings) :
    WebDAVSource(params, settings)
{
}

void CalDAVSource::listAllSubItems(SubRevisionMap_t &revisions)
{
    if (!m_cache.m_initialized) {
        // TODO: replace with more efficient CalDAV specific code
        RevisionMap_t items;
        listAllItems(items);
        BOOST_FOREACH(const RevisionMap_t::value_type &item, items) {
            const std::string &davLUID = item.first;
            const std::string &etag = item.second;
            std::string data;
            readItem(davLUID, data, true);
            boost::shared_ptr<Event> entry(new Event);
            entry->m_calendar.set(icalcomponent_new_from_string(data.c_str()),
                                 "parsing iCalendar 2.0 failed");
            entry->m_DAVluid = davLUID;
            entry->m_etag = etag;
            for (icalcomponent *comp = icalcomponent_get_first_component(entry->m_calendar, ICAL_VEVENT_COMPONENT);
                 comp;
                 comp = icalcomponent_get_next_component(entry->m_calendar, ICAL_VEVENT_COMPONENT)) {
                std::string subid = Event::getSubID(comp);
                entry->m_subids.insert(subid);
            }
            m_cache[davLUID] = entry;
        }
        m_cache.m_initialized = true;
    }

    BOOST_FOREACH(const EventCache::value_type &entry, m_cache) {
        const Event &event = *entry.second;
        pair<string, set<string> > &rev = revisions[event.m_DAVluid];
        rev.first = event.m_etag;
        rev.second = event.m_subids;
    }
}

SubSyncSource::SubItemResult CalDAVSource::insertSubItem(const std::string &davLUID, const std::string &callerSubID,
                                                         const std::string &item)
{
    SubItemResult subres;

    // parse new event
    boost::shared_ptr<Event> newEvent(new Event);
    newEvent->m_calendar.set(icalcomponent_new_from_string(item.c_str()),
                             "parsing iCalendar 2.0");
    for (icalcomponent *comp = icalcomponent_get_first_component(newEvent->m_calendar, ICAL_VEVENT_COMPONENT);
         comp;
         comp = icalcomponent_get_next_component(newEvent->m_calendar, ICAL_VEVENT_COMPONENT)) {
        std::string subid = Event::getSubID(comp);
        newEvent->m_subids.insert(subid);

        // set DTSTAMP to LAST-MODIFIED in replacement
        //
        // Needed because Google insists on replacing the original
        // DTSTAMP and checks it (409, "Can only store an event with
        // a newer DTSTAMP").
        //
        // According to RFC 2445, the property is set once when the
        // event is created for the first time. RFC 5545 extends this
        // and states that without a METHOD property (the case with
        // CalDAV), DTSTAMP is identical to LAST-MODIFIED, so Google
        // is right.
        icalproperty *dtstamp = icalcomponent_get_first_property(comp, ICAL_DTSTAMP_PROPERTY);
        if (dtstamp) {
            icalproperty *lastmod = icalcomponent_get_first_property(comp, ICAL_LASTMODIFIED_PROPERTY);
            if (lastmod) {
                icalproperty_set_dtstamp(dtstamp, icalproperty_get_lastmodified(lastmod));
            }
        }
    }
    if (newEvent->m_subids.size() != 1) {
        SE_THROW("new CalDAV item did not contain exactly one VEVENT");
    }
    std::string subid = *newEvent->m_subids.begin();



    if (davLUID.empty()) {
        // New VEVENT; may or may not be part of an existing merged item
        // ("meeting series").
        //
        // We deal with this by sending the new item to the server.
        //
        // We expect it to recognize a shared UID and to merge it on
        // the server. We then do the same locally to keep our cache
        // up-to-date.
        InsertItemResult res = insertItem("", item, true);
        subres.m_uid = res.m_luid;
        subres.m_subid = subid;
        subres.m_revision = res.m_revision;

        EventCache::iterator it = m_cache.find(res.m_luid);
        if (it != m_cache.end()) {
            // merge into existing Event
            Event &event = loadItem(*it->second);
            event.m_etag = res.m_revision;
            if (event.m_subids.find(subid) != event.m_subids.end()) {
                // was already in that item but caller didn't seem to know
                subres.m_merged = true;
            } else {
                // add to merged item
                event.m_subids.insert(subid);                
            }
            icalcomponent_merge_component(event.m_calendar,
                                          newEvent->m_calendar.release()); // function destroys merged calendar
        } else {
            // add to cache
            newEvent->m_DAVluid = res.m_luid;
            newEvent->m_etag = res.m_revision;
            m_cache[newEvent->m_DAVluid] = newEvent;
        }
    } else {
        if (subid != callerSubID) {
            SE_THROW("new CalDAV item does not have right RECURRENCE-ID");
        }
        Event &event = loadItem(davLUID);
        // no changes expected yet, copy previous attributes
        subres.m_uid = davLUID;
        subres.m_subid = subid;
        subres.m_revision = event.m_etag;

        // update sub component in cache: find old VEVENT and remove it
        // before adding new one
        bool found = false;
        for (icalcomponent *comp = icalcomponent_get_first_component(event.m_calendar, ICAL_VEVENT_COMPONENT);
             comp;
             comp = icalcomponent_get_next_component(event.m_calendar, ICAL_VEVENT_COMPONENT)) {
            if (Event::getSubID(comp) == subid) {
                icalcomponent_remove_component(event.m_calendar, comp);
                found = true;
                break;
            }
        }
        if (!found) {
            SE_THROW("event not found");
        }
        icalcomponent_merge_component(event.m_calendar,
                                      newEvent->m_calendar.release()); // function destroys merged calendar
        eptr<char> icalstr(ical_strdup(icalcomponent_as_ical_string(event.m_calendar)));
        // TODO: avoid updating item on server immediately?
        InsertItemResult res = insertItem(event.m_DAVluid, icalstr.get(), true);
        if (res.m_merged ||
            res.m_luid != event.m_DAVluid) {
            // should not merge with anything, if so, our cache was invalid
            SE_THROW("CalDAV item not updated as expected");
        }
        event.m_etag = res.m_revision;
        subres.m_revision = event.m_etag;
    }

    return subres;
}

void CalDAVSource::readSubItem(const std::string &davLUID, const std::string &subid, std::string &item)
{
    Event &event = loadItem(davLUID);
    if (event.m_subids.size() == 1) {
        // simple case: convert existing VCALENDAR
        if (*event.m_subids.begin() == subid) {
            eptr<char> icalstr(ical_strdup(icalcomponent_as_ical_string(event.m_calendar)));
            item = icalstr.get();
        } else {
            SE_THROW("event not found");
        }
    } else {
        // complex case: create VCALENDAR with just the VTIMEZONE definition(s)
        // and the one event, then convert that
        eptr<icalcomponent> calendar(icalcomponent_new(ICAL_VCALENDAR_COMPONENT), "VCALENDAR");
        for (icalcomponent *tz = icalcomponent_get_first_component(event.m_calendar, ICAL_VTIMEZONE_COMPONENT);
             tz;
             tz = icalcomponent_get_next_component(event.m_calendar, ICAL_VTIMEZONE_COMPONENT)) {
            eptr<icalcomponent> clone(icalcomponent_new_clone(tz), "VTIMEZONE");
            icalcomponent_add_component(calendar, clone.release());
        }
        bool found = false;
        for (icalcomponent *comp = icalcomponent_get_first_component(event.m_calendar, ICAL_VEVENT_COMPONENT);
             comp;
             comp = icalcomponent_get_next_component(event.m_calendar, ICAL_VEVENT_COMPONENT)) {
            if (Event::getSubID(comp) == subid) {
                eptr<icalcomponent> clone(icalcomponent_new_clone(comp), "VEVENT");
                icalcomponent_add_component(calendar, clone.release());
                found = true;
                break;
            }
        }
        if (!found) {
            SE_THROW("event not found");
        }
        eptr<char> icalstr(ical_strdup(icalcomponent_as_ical_string(calendar)));
        item = icalstr.get();
    }
}

std::string CalDAVSource::removeSubItem(const string &davLUID, const std::string &subid)
{
    Event &event = loadItem(davLUID);
    if (event.m_subids.size() == 1) {
        // remove entire merged item, nothing will be left after removal
        if (*event.m_subids.begin() != subid) {
            SE_THROW("event not found");
        } else {
            removeItem(event.m_DAVluid);
        }
        m_cache.erase(davLUID);
        return "";
    } else {
        bool found = false;
        for (icalcomponent *comp = icalcomponent_get_first_component(event.m_calendar, ICAL_VEVENT_COMPONENT);
             comp;
             comp = icalcomponent_get_next_component(event.m_calendar, ICAL_VEVENT_COMPONENT)) {
            if (Event::getSubID(comp) == subid) {
                icalcomponent_remove_component(event.m_calendar, comp);
                icalcomponent_free(comp);
                found = true;
            }
        }
        if (!found) {
            SE_THROW("event not found");
        }
        // TODO: avoid updating the item immediately
        eptr<char> icalstr(ical_strdup(icalcomponent_as_ical_string(event.m_calendar)));
        InsertItemResult res = insertItem(davLUID, icalstr.get(), true);
        if (res.m_merged ||
            res.m_luid != davLUID) {
            SE_THROW("unexpected result of removing sub event");
        }
        event.m_etag = res.m_revision;
        return event.m_etag;
    }
}

void CalDAVSource::flushItem(const string &davLUID)
{
    // TODO: currently we always flush immediately, so no need to send data here
    EventCache::iterator it = m_cache.find(davLUID);
    if (it != m_cache.end()) {
        it->second->m_calendar.set(NULL);
    }
}

std::string CalDAVSource::getSubDescription(const string &uid, const string &subid)
{
    // TODO: CalDAV query
    return "";
}

CalDAVSource::Event &CalDAVSource::loadItem(const std::string &davLUID)
{
    EventCache::iterator it = m_cache.find(davLUID);
    if (it == m_cache.end()) {
        throwError("event not found");
    }
    return loadItem(*it->second);
}

CalDAVSource::Event &CalDAVSource::loadItem(Event &event)
{
    if (!event.m_calendar) {
        std::string item;
        readItem(event.m_DAVluid, item, true);
        event.m_calendar.set(icalcomponent_new_from_string(item.c_str()),
                             "parsing iCalendar 2.0");
    }
    return event;
}

std::string CalDAVSource::Event::icalTime2Str(const icaltimetype &tt)
{
    static const struct icaltimetype null = { 0 };
    if (!memcmp(&tt, &null, sizeof(null))) {
        return "";
    } else {
        eptr<char> timestr(ical_strdup(icaltime_as_ical_string(tt)));
        if (!timestr) {
            SE_THROW("cannot convert to time string");
        }
        return timestr.get();
    }
}

std::string CalDAVSource::Event::getSubID(icalcomponent *comp)
{
    struct icaltimetype rid = icalcomponent_get_recurrenceid(comp);
    return icalTime2Str(rid);
}

SE_END_CXX

#endif // ENABLE_DAV
