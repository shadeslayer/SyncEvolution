/*
 * Copyright (C) 2010 Intel Corporation
 */

#include "CalDAVSource.h"

#ifdef ENABLE_DAV

#include <boost/bind.hpp>

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
    revisions.clear();

    const std::string query =
        "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n"
        "<C:calendar-query xmlns:D=\"DAV:\"\n"
        "xmlns:C=\"urn:ietf:params:xml:ns:caldav\">\n"
        "<D:prop>\n"
        "<D:getetag/>\n"
        "<C:calendar-data>\n"
        "<C:comp name=\"VCALENDAR\">\n"
        "<C:prop name=\"VERSION\"/>\n"
        "<C:comp name=\"VEVENT\">\n"
        "<C:prop name=\"SUMMARY\"/>\n"
        "<C:prop name=\"UID\"/>\n"
        "<C:prop name=\"RECURRENCE-ID\"/>\n"
        "</C:comp>\n"
        "<C:comp name=\"VTIMEZONE\"/>\n"
        "</C:comp>\n"
        "</C:calendar-data>\n"
        "</D:prop>\n"
        "</C:calendar-query>\n";
    string result;
    string href, etag, data;
    Neon::XMLParser parser;
    parser.pushHandler(boost::bind(Neon::XMLParser::accept, "DAV:", "multistatus", _2, _3))
        .pushHandler(boost::bind(Neon::XMLParser::accept, "DAV:", "response", _2, _3))
        .pushHandler(boost::bind(Neon::XMLParser::accept, "DAV:", "href", _2, _3),
                     boost::bind(Neon::XMLParser::append, boost::ref(href), _2, _3))
        .pushHandler(boost::bind(Neon::XMLParser::accept, "DAV:", "propstat", _2, _3))
        .pushHandler(boost::bind(Neon::XMLParser::accept, "DAV:", "status", _2, _3) /* check status? */)
        .pushHandler(boost::bind(Neon::XMLParser::accept, "DAV:", "prop", _2, _3))
        .pushHandler(boost::bind(Neon::XMLParser::accept, "DAV:", "getetag", _2, _3),
                     boost::bind(Neon::XMLParser::append, boost::ref(etag), _2, _3))
        .pushHandler(boost::bind(Neon::XMLParser::accept, "urn:ietf:params:xml:ns:caldav", "calendar-data", _2, _3),
                     boost::bind(Neon::XMLParser::append, boost::ref(data), _2, _3),
                     boost::bind(&CalDAVSource::appendItem, this,
                                 boost::ref(revisions),
                                 boost::ref(href), boost::ref(etag), boost::ref(data)));
    Neon::Request report(*getSession(), "REPORT", getCalendar().m_path, query, parser);
    report.addHeader("Depth", "1");
    report.addHeader("Content-Type", "application/xml; charset=\"utf-8\"");
    report.run();
    m_cache.m_initialized = true;
}

int CalDAVSource::appendItem(SubRevisionMap_t &revisions,
                             std::string &href,
                             std::string &etag,
                             std::string &data)
{
    eptr<icalcomponent> calendar(icalcomponent_new_from_string(data.c_str()),
                                 "iCalendar 2.0");
    std::string davLUID = path2luid(Neon::URI::parse(href).m_path);
    pair<string, set<string> > &rev = revisions[davLUID];
    rev.first = ETag2Rev(etag);
    for (icalcomponent *comp = icalcomponent_get_first_component(calendar, ICAL_VEVENT_COMPONENT);
         comp;
         comp = icalcomponent_get_next_component(calendar, ICAL_VEVENT_COMPONENT)) {
        std::string subid = Event::getSubID(comp);
        rev.second.insert(subid);
    }

    if (!m_cache.m_initialized) {
        boost::shared_ptr<Event> event(new Event);
        event->m_DAVluid = davLUID;
        event->m_etag = rev.first;
        event->m_subids = rev.second;
        m_cache.insert(make_pair(davLUID, event));
    }

    // reset data for next item
    data.clear();
    href.clear();
    etag.clear();
    return 0;
}

SubSyncSource::SubItemResult CalDAVSource::insertSubItem(const std::string &davLUID, const std::string &callerSubID,
                                                         const std::string &item)
{
    SubItemResult subres;

    // parse new event
    boost::shared_ptr<Event> newEvent(new Event);
    newEvent->m_calendar.set(icalcomponent_new_from_string(item.c_str()),
                             "parsing iCalendar 2.0");
    struct icaltimetype lastmodtime = icaltime_null_time();
    int sequenceval = 0;
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
                lastmodtime = icalproperty_get_lastmodified(lastmod);
                icalproperty_set_dtstamp(dtstamp, lastmodtime);
            }
        }
        icalproperty *sequence = icalcomponent_get_first_property(comp, ICAL_SEQUENCE_PROPERTY);
        if (sequence) {
            sequenceval = icalproperty_get_sequence(sequence);
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

        // update cache: find old VEVENT and remove it before adding new one,
        // update last modified time of all other components
        icalcomponent *removeme = NULL;
        for (icalcomponent *comp = icalcomponent_get_first_component(event.m_calendar, ICAL_VEVENT_COMPONENT);
             comp;
             comp = icalcomponent_get_next_component(event.m_calendar, ICAL_VEVENT_COMPONENT)) {
            if (Event::getSubID(comp) == subid) {
                removeme = comp;
            } else {
                // increase modification time stamps and sequence to that of the new item,
                // Google rejects the whole update otherwise
                if (!icaltime_is_null_time(lastmodtime)) {
                    icalproperty *dtstamp = icalcomponent_get_first_property(comp, ICAL_DTSTAMP_PROPERTY);
                    if (dtstamp) {
                        icalproperty_set_dtstamp(dtstamp, lastmodtime);
                    }
                    icalproperty *lastmod = icalcomponent_get_first_property(comp, ICAL_LASTMODIFIED_PROPERTY);
                    if (lastmod) {
                        icalproperty_set_lastmodified(lastmod, lastmodtime);
                    }
                }
                if (sequenceval) {
                    icalproperty *sequence = icalcomponent_get_first_property(comp, ICAL_SEQUENCE_PROPERTY);
                    if (sequence) {
                        icalproperty_set_sequence(sequence, sequenceval);
                    }
                }
            }
        }
        if (!removeme) {
            SE_THROW("event not found");
        }
        icalcomponent_remove_component(event.m_calendar, removeme);
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
        event.m_subids.erase(subid);
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
