/*
 * Copyright (C) 2010 Intel Corporation
 */

#include "config.h"

#ifdef ENABLE_DAV

// include first, it sets HANDLE_LIBICAL_MEMORY for us
#include <syncevo/icalstrdup.h>

#include "CalDAVSource.h"

#include <boost/bind.hpp>
#include <boost/algorithm/string/replace.hpp>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

/**
 * @return "<master>" if subid is empty, otherwise subid
 */
static std::string SubIDName(const std::string &subid)
{
    return subid.empty() ? "<master>" : subid;
}

/** remove X-SYNCEVOLUTION-EXDATE-DETACHED from VEVENT */
static void removeSyncEvolutionExdateDetached(icalcomponent *parent)
{
    icalproperty *prop = icalcomponent_get_first_property(parent, ICAL_ANY_PROPERTY);
    while (prop) {
        icalproperty *next = icalcomponent_get_next_property(parent, ICAL_ANY_PROPERTY);
        const char *xname = icalproperty_get_x_name(prop);
        if (xname &&
            !strcmp(xname, "X-SYNCEVOLUTION-EXDATE-DETACHED")) {
            icalcomponent_remove_property(parent, prop);
            icalproperty_free(prop);
        }
        prop = next;
    }
}

CalDAVSource::CalDAVSource(const SyncSourceParams &params,
                           const boost::shared_ptr<Neon::Settings> &settings) :
    WebDAVSource(params, settings)
{
    SyncSourceLogging::init(InitList<std::string>("SUMMARY") + "LOCATION",
                            ", ",
                            m_operations);
    // override default backup/restore from base class with our own
    // version
    m_operations.m_backupData = boost::bind(&CalDAVSource::backupData,
                                            this, _1, _2, _3);
    m_operations.m_restoreData = boost::bind(&CalDAVSource::restoreData,
                                             this, _1, _2, _3);
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

        // In practice, peers always return the full data dump
        // even if asked to return only a subset. Therefore we use this
        // REPORT to populate our m_cache instead of sending lots of GET
        // requests later on: faster sync, albeit with higher
        // memory consumption.
        //
        // Because incremental syncs typically don't use listAllSubItems(),
        // this looks like a good trade-off.
#ifdef SHORT_ALL_SUB_ITEMS_DATA
        "<C:calendar-data>\n"
        "<C:comp name=\"VCALENDAR\">\n"
        "<C:prop name=\"VERSION\"/>\n"
        "<C:comp name=\"VEVENT\">\n"
        "<C:prop name=\"SUMMARY\"/>\n"
        "<C:prop name=\"UID\"/>\n"
        "<C:prop name=\"RECURRENCE-ID\"/>\n"
        "<C:prop name=\"SEQUENCE\"/>\n"
        "</C:comp>\n"
        "<C:comp name=\"VTIMEZONE\"/>\n"
        "</C:comp>\n"
        "</C:calendar-data>\n"
#else
        "<C:calendar-data/>\n"
#endif

        "</D:prop>\n"
        // filter expected by Yahoo! Calendar
        "<C:filter>\n"
        "<C:comp-filter name=\"VCALENDAR\">\n"
        "<C:comp-filter name=\"VEVENT\">\n"
        "</C:comp-filter>\n"
        "</C:comp-filter>\n"
        "</C:filter>\n"
        "</C:calendar-query>\n";
    Timespec deadline = createDeadline();
    getSession()->startOperation("REPORT 'meta data'", deadline);
    while (true) {
        string data;
        Neon::XMLParser parser;
        parser.initReportParser(boost::bind(&CalDAVSource::appendItem, this,
                                            boost::ref(revisions),
                                            _1, _2, boost::ref(data)));
        m_cache.clear();
        m_cache.m_initialized = false;
        parser.pushHandler(boost::bind(Neon::XMLParser::accept, "urn:ietf:params:xml:ns:caldav", "calendar-data", _2, _3),
                           boost::bind(Neon::XMLParser::append, boost::ref(data), _2, _3));
        Neon::Request report(*getSession(), "REPORT", getCalendar().m_path, query, parser);
        report.addHeader("Depth", "1");
        report.addHeader("Content-Type", "application/xml; charset=\"utf-8\"");
        if (report.run()) {
            break;
        }
    }

    m_cache.m_initialized = true;
}

void CalDAVSource::addResource(StringMap &items,
                               const std::string &href,
                               const std::string &etag)
{
    std::string davLUID = path2luid(Neon::URI::parse(href).m_path);
    items[davLUID] = ETag2Rev(etag);
}

void CalDAVSource::updateAllSubItems(SubRevisionMap_t &revisions)
{
    // list items to identify new, updated and removed ones
    const std::string query =
        "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n"
        "<C:calendar-query xmlns:D=\"DAV:\"\n"
        "xmlns:C=\"urn:ietf:params:xml:ns:caldav\">\n"
        "<D:prop>\n"
        "<D:getetag/>\n"
        "</D:prop>\n"
        // filter expected by Yahoo! Calendar
        "<C:filter>\n"
        "<C:comp-filter name=\"VCALENDAR\">\n"
        "<C:comp-filter name=\"VEVENT\">\n"
        "</C:comp-filter>\n"
        "</C:comp-filter>\n"
        "</C:filter>\n"
        "</C:calendar-query>\n";
    Timespec deadline = createDeadline();
    StringMap items;
    getSession()->startOperation("updateAllSubItems REPORT 'list items'", deadline);
    while (true) {
        string data;
        Neon::XMLParser parser;
        items.clear();
        parser.initReportParser(boost::bind(&CalDAVSource::addResource,
                                            this, boost::ref(items), _1, _2));
        Neon::Request report(*getSession(), "REPORT", getCalendar().m_path, query, parser);
        report.addHeader("Depth", "1");
        report.addHeader("Content-Type", "application/xml; charset=\"utf-8\"");
        if (report.run()) {
            break;
        }
    }

    // remove obsolete entries
    SubRevisionMap_t::iterator it = revisions.begin();
    while (it != revisions.end()) {
        SubRevisionMap_t::iterator next = it;
        ++next;
        if (items.find(it->first) == items.end()) {
            revisions.erase(it);
        }
        it = next;
    }

    // build list of new or updated entries,
    // copy others to cache
    m_cache.clear();
    m_cache.m_initialized = false;
    std::list<std::string> mustRead;
    BOOST_FOREACH(const StringPair &item, items) {
        SubRevisionMap_t::iterator it = revisions.find(item.first);
        if (it == revisions.end() ||
            it->second.m_revision != item.second) {
            // read current information below
            SE_LOG_DEBUG(NULL, NULL, "updateAllSubItems(): read new or modified item %s", item.first.c_str());
            mustRead.push_back(item.first);
            // The server told us that the item exists. We still need
            // to deal with the situation that the server might fail
            // to deliver the item data when we ask for it below.
            //
            // There are two reasons when this can happen: either an
            // item was removed in the meantime or the server is
            // confused.  The latter started to happen reliably with
            // the Google Calendar server sometime in January/February
            // 2012.
            //
            // In both cases, let's assume that the item is really gone
            // (and not just unreadable due to that other Google Calendar
            // bug, see loadItem()+REPORT workaround), and therefore let's
            // remove the entry from the revisions.
            if (it != revisions.end()) {
                revisions.erase(it);
            }
            m_cache.erase(item.first);
        } else {
            // copy still relevant information
            SE_LOG_DEBUG(NULL, NULL, "updateAllSubItems(): unmodified item %s", it->first.c_str());
            addSubItem(it->first, it->second);
        }
    }

    // request dump of these items, add to cache and revisions
    //
    // Failures to find or read certain items will be
    // ignored. appendItem() will only be called for actually
    // retrieved items. This is partly intentional: Google is known to
    // have problems with providing all of its data via GET or the
    // multiget REPORT below. It returns a 404 error for items that a
    // calendar-query includes (see loadItem()). Such items are
    // ignored and thus will be silently skipped. This is not
    // perfect, but better than failing the sync.
    //
    // Unfortunately there are other servers (Radicale, I'm looking at
    // you) which simply return neither data nor errors for the
    // requested hrefs. To handle that we try the multiget first,
    // record retrieved or failed responses, then follow up with
    // individual requests for anything that wasn't mentioned.
    if (!mustRead.empty()) {
        std::stringstream buffer;
        buffer << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
            "<C:calendar-multiget xmlns:D=\"DAV:\"\n"
            "   xmlns:C=\"urn:ietf:params:xml:ns:caldav\">\n"
            "<D:prop>\n"
            "   <D:getetag/>\n"
            "   <C:calendar-data/>\n"
            "</D:prop>\n";
        BOOST_FOREACH(const std::string &luid, mustRead) {
            buffer << "<D:href>" << luid2path(luid) << "</D:href>\n";
        }
        buffer << "</C:calendar-multiget>";
        std::string query = buffer.str();
        std::set<std::string> results; // LUIDs of all hrefs returned by report
        getSession()->startOperation("updateAllSubItems REPORT 'multiget new/updated items'", deadline);
        while (true) {
            string data;
            Neon::XMLParser parser;
            parser.initReportParser(boost::bind(&CalDAVSource::appendMultigetResult, this,
                                                boost::ref(revisions),
                                                boost::ref(results),
                                                _1, _2, boost::ref(data)));
            parser.pushHandler(boost::bind(Neon::XMLParser::accept, "urn:ietf:params:xml:ns:caldav", "calendar-data", _2, _3),
                               boost::bind(Neon::XMLParser::append, boost::ref(data), _2, _3));
            Neon::Request report(*getSession(), "REPORT", getCalendar().m_path,
                                 query, parser);
            report.addHeader("Depth", "1");
            report.addHeader("Content-Type", "application/xml; charset=\"utf-8\"");
            if (report.run()) {
                break;
            }
        }
        // Workaround for Radicale 0.6.4: it simply returns nothing (no error, no data).
        // Fall back to GET of items with no response.
        BOOST_FOREACH(const std::string &luid, mustRead) {
            if (results.find(luid) == results.end()) {
                getSession()->startOperation(StringPrintf("GET item %s not returned by 'multiget new/updated items'", luid.c_str()),
                                             deadline);
                std::string path = luid2path(luid);
                std::string data;
                std::string etag;
                while (true) {
                    data.clear();
                    Neon::Request req(*getSession(), "GET", path,
                                      "", data);
                    req.addHeader("Accept", contentType());
                    if (req.run()) {
                        etag = getETag(req);
                        break;
                    }
                }
                appendItem(revisions, path, etag, data);
            }
        }
    }
}

int CalDAVSource::appendMultigetResult(SubRevisionMap_t &revisions,
                                       std::set<std::string> &luids,
                                       const std::string &href,
                                       const std::string &etag,
                                       std::string &data)
{
    // record which items were seen in the response...
    luids.insert(path2luid(href));
    // and store information about them
    return appendItem(revisions, href, etag, data);
}

int CalDAVSource::appendItem(SubRevisionMap_t &revisions,
                             const std::string &href,
                             const std::string &etag,
                             std::string &data)
{
    // Ignore responses with no data: this is not perfect (should better
    // try to figure out why there is no data), but better than
    // failing.
    //
    // One situation is the response for the collection itself,
    // which comes with a 404 status and no data with Google Calendar.
    if (data.empty()) {
        return 0;
    }

    Event::unescapeRecurrenceID(data);
    eptr<icalcomponent> calendar(icalcomponent_new_from_string((char *)data.c_str()), // cast is a hack for broken definition in old libical
                                 "iCalendar 2.0");
    Event::fixIncomingCalendar(calendar.get());
    std::string davLUID = path2luid(Neon::URI::parse(href).m_path);
    SubRevisionEntry &entry = revisions[davLUID];
    entry.m_revision = ETag2Rev(etag);
    long maxSequence = 0;
    std::string uid;
    entry.m_subids.clear();
    for (icalcomponent *comp = icalcomponent_get_first_component(calendar, ICAL_VEVENT_COMPONENT);
         comp;
         comp = icalcomponent_get_next_component(calendar, ICAL_VEVENT_COMPONENT)) {
        std::string subid = Event::getSubID(comp);
        uid = Event::getUID(comp);
        long sequence = Event::getSequence(comp);
        if (sequence > maxSequence) {
            maxSequence = sequence;
        }
        entry.m_subids.insert(subid);
    }
    entry.m_uid = uid;

    // Ignore items which contain no VEVENT. Happens with Google Calendar
    // after using it for a while. Deleting them via DELETE doesn't seem
    // to have an effect either, so all we really can do is ignore them.
    if (entry.m_subids.empty()) {
        SE_LOG_DEBUG(NULL, NULL, "ignoring broken item %s (is empty)", davLUID.c_str());
        revisions.erase(davLUID);
        m_cache.erase(davLUID);
        data.clear();
        return 0;
    }

    if (!m_cache.m_initialized) {
        boost::shared_ptr<Event> event(new Event);
        event->m_DAVluid = davLUID;
        event->m_UID = uid;
        event->m_etag = entry.m_revision;
        event->m_subids = entry.m_subids;
        event->m_sequence = maxSequence;
#ifndef SHORT_ALL_SUB_ITEMS_DATA
        // we got a full data dump, use it
        for (icalcomponent *comp = icalcomponent_get_first_component(calendar, ICAL_VEVENT_COMPONENT);
             comp;
             comp = icalcomponent_get_next_component(calendar, ICAL_VEVENT_COMPONENT)) {
        }
        event->m_calendar = calendar;
#endif
        m_cache.insert(make_pair(davLUID, event));
    }

    // reset data for next item
    data.clear();
    return 0;
}

void CalDAVSource::addSubItem(const std::string &luid,
                              const SubRevisionEntry &entry)
{
    boost::shared_ptr<Event> &event = m_cache[luid];
    event.reset(new Event);
    event->m_DAVluid = luid;
    event->m_etag = entry.m_revision;
    event->m_UID = entry.m_uid;
    // We don't know sequence and last-modified. This
    // information will have to be filled in by loadItem()
    // when some operation on this event needs it.
    event->m_subids = entry.m_subids;
}

void CalDAVSource::setAllSubItems(const SubRevisionMap_t &revisions)
{
    if (!m_cache.m_initialized) {
        // populate our cache (without data) from the information cached
        // for us
        BOOST_FOREACH(const SubRevisionMap_t::value_type &subentry,
                      revisions) {
            addSubItem(subentry.first,
                       subentry.second);
        }
        m_cache.m_initialized = true;
    }
}

SubSyncSource::SubItemResult CalDAVSource::insertSubItem(const std::string &luid, const std::string &callerSubID,
                                                         const std::string &item)
{
    SubItemResult subres;

    // parse new event
    boost::shared_ptr<Event> newEvent(new Event);
    newEvent->m_calendar.set(icalcomponent_new_from_string((char *)item.c_str()), // hack for old libical
                             "parsing iCalendar 2.0");
    struct icaltimetype lastmodtime = icaltime_null_time();
    icalcomponent *firstcomp = NULL;
    for (icalcomponent *comp = firstcomp = icalcomponent_get_first_component(newEvent->m_calendar, ICAL_VEVENT_COMPONENT);
         comp;
         comp = icalcomponent_get_next_component(newEvent->m_calendar, ICAL_VEVENT_COMPONENT)) {
        std::string subid = Event::getSubID(comp);
        EventCache::iterator it;
        // remove X-SYNCEVOLUTION-EXDATE-DETACHED, could be added by
        // the engine's read/modify/write cycle when resolving a
        // conflict
        removeSyncEvolutionExdateDetached(comp);
        if (!luid.empty() &&
            (it = m_cache.find(luid)) != m_cache.end()) {
            // Additional sanity check: ensure that the expected UID is set.
            // Necessary if the peer we synchronize with (aka the local
            // data storage) doesn't support foreign UIDs. Maemo 5 calendar
            // backend is one example.
            Event::setUID(comp, it->second->m_UID);
            newEvent->m_UID = it->second->m_UID;
        } else {
            newEvent->m_UID = Event::getUID(comp);
            if (newEvent->m_UID.empty()) {
                // create new UID
                newEvent->m_UID = UUID();
                Event::setUID(comp, newEvent->m_UID);
            }
        }
        newEvent->m_sequence = Event::getSequence(comp);
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
    }
    if (newEvent->m_subids.size() != 1) {
        SE_THROW("new CalDAV item did not contain exactly one VEVENT");
    }
    std::string subid = *newEvent->m_subids.begin();

    // Determine whether we already know the merged item even though
    // our caller didn't.
    std::string davLUID = luid;
    std::string knownSubID = callerSubID;
    if (davLUID.empty()) {
        EventCache::iterator it = m_cache.findByUID(newEvent->m_UID);
        if (it != m_cache.end()) {
            davLUID = it->first;
            knownSubID = subid;
        }
    }

    if (davLUID.empty()) {
        // New VEVENT; should not be part of an existing merged item
        // ("meeting series").
        //
        // If another app created a resource with the same UID,
        // then two things can happen:
        // 1. server merges the data (Google)
        // 2. adding the item is rejected (standard compliant CalDAV server)
        //
        // If the UID is truely new, then
        // 3. the server may rename the item
        //
        // The following code deals with case 3 and also covers case
        // 1, but our usual Google workarounds (for example, no
        // patching of SEQUENCE) were not applied and thus sending the
        // item might fail.
        //
        // Case 2 is not currently handled and causes the sync to fail.
        // This is in line with the current design ("concurrency detected,
        // causes error, fixed by trying again in slow sync").
        InsertItemResult res;
        // Yahoo expects resource names to match UID + ".ics".
        std::string name = newEvent->m_UID + ".ics";
        std::string buffer;
        const std::string *data;
        if (!settings().googleChildHack() || subid.empty()) {
            // avoid re-encoding item data
            data = &item;
        } else {
            // sanitize item first: when adding child event without parent,
            // then the RECURRENCE-ID confuses Google
            eptr<char> icalstr(ical_strdup(icalcomponent_as_ical_string(newEvent->m_calendar)));
            buffer = icalstr.get();
            Event::escapeRecurrenceID(buffer);
            data = &buffer;
        }
        SE_LOG_DEBUG(this, NULL, "inserting new VEVENT");
        res = insertItem(name, *data, true);
        subres.m_mainid = res.m_luid;
        subres.m_uid = newEvent->m_UID;
        subres.m_subid = subid;
        subres.m_revision = res.m_revision;

        EventCache::iterator it = m_cache.find(res.m_luid);
        if (it != m_cache.end()) {
            // merge into existing Event
            Event &event = loadItem(*it->second);
            event.m_etag = res.m_revision;
            if (event.m_subids.find(subid) != event.m_subids.end()) {
                // was already in that item but caller didn't seem to know,
                // and now we replaced the data on the CalDAV server
                subres.m_state = ITEM_REPLACED;
            } else {
                // add to merged item
                event.m_subids.insert(subid);                
            }
            icalcomponent_merge_component(event.m_calendar,
                                          newEvent->m_calendar.release()); // function destroys merged calendar
        } else {
            // Google Calendar adds a default alarm each time a VEVENT is added
            // anew. Avoid that by resending our data if necessary (= no alarm set).
            if (settings().googleAlarmHack() &&
                !icalcomponent_get_first_component(firstcomp, ICAL_VALARM_COMPONENT)) {
                // add to cache, then update it
                newEvent->m_DAVluid = res.m_luid;
                newEvent->m_etag = res.m_revision;
                m_cache[newEvent->m_DAVluid] = newEvent;

                // potentially need to know sequence and mod time on server:
                // keep pointer (clears pointer in newEvent),
                // then get and parse new copy from server
                eptr<icalcomponent> calendar = newEvent->m_calendar;

                if (settings().googleUpdateHack()) {
                    loadItem(*newEvent);

                    // increment in original data
                    newEvent->m_sequence++;
                    newEvent->m_lastmodtime++;
                    Event::setSequence(firstcomp, newEvent->m_sequence);
                    icalproperty *lastmod = icalcomponent_get_first_property(firstcomp, ICAL_LASTMODIFIED_PROPERTY);
                    if (lastmod) {
                        lastmodtime = icaltime_from_timet(newEvent->m_lastmodtime, false);
                        lastmodtime.is_utc = 1;
                        icalproperty_set_lastmodified(lastmod, lastmodtime);
                    }
                    icalproperty *dtstamp = icalcomponent_get_first_property(firstcomp, ICAL_DTSTAMP_PROPERTY);
                    if (dtstamp) {
                        icalproperty_set_dtstamp(dtstamp, lastmodtime);
                    }
                    // re-encode below
                    data = &buffer;
                }
                bool mangleRecurrenceID = settings().googleChildHack() && !subid.empty();
                if (data == &buffer || mangleRecurrenceID) {
                    eptr<char> icalstr(ical_strdup(icalcomponent_as_ical_string(calendar)));
                    buffer = icalstr.get();
                }
                if (mangleRecurrenceID) {
                    Event::escapeRecurrenceID(buffer);
                }
                SE_LOG_DEBUG(NULL, NULL, "resending VEVENT to get rid of VALARM");
                res = insertItem(name, *data, true);
                newEvent->m_etag =
                    subres.m_revision = res.m_revision;
                newEvent->m_calendar = calendar;
            } else {
                // add to cache without further changes
                newEvent->m_DAVluid = res.m_luid;
                newEvent->m_etag = res.m_revision;
                m_cache[newEvent->m_DAVluid] = newEvent;
            }
        }
    } else {
        if (!subid.empty() && subid != knownSubID) {
            SE_THROW(StringPrintf("new CalDAV item does not have right RECURRENCE-ID: item %s != expected %s",
                                  subid.c_str(), knownSubID.c_str()));
        }
        Event &event = loadItem(davLUID);

        if (subid.empty() && subid != knownSubID) {
            // fix incomplete iCalendar 2.0 item: should have had a RECURRENCE-ID
            icalcomponent *newcomp =
                icalcomponent_get_first_component(newEvent->m_calendar, ICAL_VEVENT_COMPONENT);
            icalproperty *prop = icalcomponent_get_first_property(newcomp, ICAL_RECURRENCEID_PROPERTY);
            if (prop) {
                icalcomponent_remove_property(newcomp, prop);
                icalproperty_free(prop);
            }

            // reconstruct RECURRENCE-ID with known value and TZID from start time of
            // the parent event or (if not found) the current event
            eptr<icalproperty> rid(icalproperty_new_recurrenceid(icaltime_from_string(knownSubID.c_str())),
                                   "new rid");
            icalproperty *dtstart = NULL;
            icalcomponent *comp;
            // look for parent first
            for (comp = icalcomponent_get_first_component(event.m_calendar, ICAL_VEVENT_COMPONENT);
                 comp && !dtstart;
                 comp = icalcomponent_get_next_component(event.m_calendar, ICAL_VEVENT_COMPONENT)) {
                if (!icalcomponent_get_first_property(comp, ICAL_RECURRENCEID_PROPERTY)) {
                    dtstart = icalcomponent_get_first_property(comp, ICAL_DTSTART_PROPERTY);
                }
            }
            // fall back to current event
            if (!dtstart) {
                dtstart = icalcomponent_get_first_property(newcomp, ICAL_DTSTART_PROPERTY);
            }
            // ignore missing TZID
            if (dtstart) {
                icalparameter *tzid = icalproperty_get_first_parameter(dtstart, ICAL_TZID_PARAMETER);
                if (tzid) {
                    icalproperty_set_parameter(rid, icalparameter_new_clone(tzid));
                }
            }

            // finally add RECURRENCE-ID and fix newEvent's meta information
            icalcomponent_add_property(newcomp, rid.release());
            subid = knownSubID;
            newEvent->m_subids.erase("");
            newEvent->m_subids.insert(subid);
        }

        // no changes expected yet, copy previous attributes
        subres.m_mainid = davLUID;
        subres.m_uid = event.m_UID;
        subres.m_subid = subid;
        subres.m_revision = event.m_etag;

        // Google hack: increase sequence number if smaller or equal to
        // sequence on server. Server rejects update otherwise.
        // See http://code.google.com/p/google-caldav-issues/issues/detail?id=26
        if (settings().googleUpdateHack()) {
            // always bump SEQ by one before PUT
            event.m_sequence++;
            if (newEvent->m_sequence < event.m_sequence) {
                // override in new event, existing ones will be updated below
                Event::setSequence(firstcomp, event.m_sequence);
            } else {
                // new event sequence is equal or higher, use that
                event.m_sequence = newEvent->m_sequence;
            }
        }

        // update cache: find old VEVENT and remove it before adding new one,
        // update last modified time of all other components
        icalcomponent *removeme = NULL;
        for (icalcomponent *comp = icalcomponent_get_first_component(event.m_calendar, ICAL_VEVENT_COMPONENT);
             comp;
             comp = icalcomponent_get_next_component(event.m_calendar, ICAL_VEVENT_COMPONENT)) {
            if (Event::getSubID(comp) == subid) {
                removeme = comp;
            } else if (settings().googleUpdateHack()) {
                // increase modification time stamps to that of the new item,
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
                // set SEQ to the one increased above
                Event::setSequence(comp, event.m_sequence);
            }
        }
        if (davLUID != luid) {
            // caller didn't know final UID: if found, then tell him to
            // merge the data and try again
            if (removeme) {
                subres.m_state = ITEM_NEEDS_MERGE;
                goto done;
            } else {
                event.m_subids.insert(subid);
            }
        } else {
            if (removeme) {
                // this is what we expect when the caller mentions the DAV LUID
                icalcomponent_remove_component(event.m_calendar, removeme);
                icalcomponent_free(removeme);
            } else {
                // caller confused?!
                SE_THROW("event not found");
            }
        }

        icalcomponent_merge_component(event.m_calendar,
                                      newEvent->m_calendar.release()); // function destroys merged calendar
        eptr<char> icalstr(ical_strdup(icalcomponent_as_ical_string(event.m_calendar)));
        std::string data = icalstr.get();

        // Google gets confused when adding a child without parent,
        // replace in that case.
        bool haveParent = event.m_subids.find("") != event.m_subids.end();
        if (settings().googleChildHack() && !haveParent) {
            Event::escapeRecurrenceID(data);
        }

        // TODO: avoid updating item on server immediately?
        try {
            SE_LOG_DEBUG(this, NULL, "updating VEVENT");
            InsertItemResult res = insertItem(event.m_DAVluid, data, true);
            if (res.m_state != ITEM_OKAY ||
                res.m_luid != event.m_DAVluid) {
                // should not merge with anything, if so, our cache was invalid
                SE_THROW("CalDAV item not updated as expected");
            }
            event.m_etag = res.m_revision;
            subres.m_revision = event.m_etag;
        } catch (const TransportStatusException &ex) {
            if (ex.syncMLStatus() == 403 &&
                strstr(ex.what(), "You don't have access to change that event")) {
                // Google Calendar sometimes refuses writes for specific items,
                // typically meetings organized by someone else.
#if 1
                // Treat like a temporary, per item error to avoid aborting the
                // whole sync session. Doesn't really solve the problem (client
                // and server remain out of sync and will run into this again and
                // again), but better than giving up on all items or ignoring the
                // problem.
                SE_THROW_EXCEPTION_STATUS(StatusException,
                                          "CalDAV peer rejected updated with 403, keep trying",
                                          SyncMLStatus(417));
#else
                // Assume that the item hasn't changed and mark it as "merged".
                // This is incorrect. The 403 error has been seen in cases where
                // a detached recurrence had to be added to an existing meeting
                // series. Ignoring the problem means would keep the detached
                // recurrence out of the server permanently.
                SE_LOG_INFO(this, NULL, "%s: not updated because CalDAV server refused write access for it",
                            getSubDescription(event, subid).c_str());
                subres.m_merged = true;
                subres.m_revision = event.m_etag;
#endif
            } else if (ex.syncMLStatus() == 409 &&
                       strstr(ex.what(), "Can only store an event with a newer DTSTAMP")) {
                SE_LOG_DEBUG(NULL, NULL, "resending VEVENT with updated SEQUENCE/LAST-MODIFIED/DTSTAMP to work around 409");

                // Sometimes a PUT of two linked events updates one of them on the server
                // (visible in modified SEQUENCE and LAST-MODIFIED values) and then
                // fails with 409 because, presumably, the other item now has
                // too low SEQUENCE/LAST-MODIFIED/DTSTAMP values.
                //
                // An attempt with splitting the PUT in advance worked for some cases,
                // but then it still happened for others. So let's use brute force and
                // try again once more after reading the updated event anew.
                eptr<icalcomponent> fullcal = event.m_calendar;
                loadItem(event);
                event.m_sequence++;
                lastmodtime = icaltime_from_timet(event.m_lastmodtime, false);
                lastmodtime.is_utc = 1;
                event.m_calendar = fullcal;
                for (icalcomponent *comp = icalcomponent_get_first_component(event.m_calendar, ICAL_VEVENT_COMPONENT);
                     comp;
                     comp = icalcomponent_get_next_component(event.m_calendar, ICAL_VEVENT_COMPONENT)) {
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
                    Event::setSequence(comp, event.m_sequence);
                }
                eptr<char> icalstr(ical_strdup(icalcomponent_as_ical_string(event.m_calendar)));
                std::string data = icalstr.get();
                InsertItemResult res = insertItem(event.m_DAVluid, data, true);
                if (res.m_state != ITEM_OKAY ||
                    res.m_luid != event.m_DAVluid) {
                    // should not merge with anything, if so, our cache was invalid
                    SE_THROW("CalDAV item not updated as expected");
                }
                event.m_etag = res.m_revision;
                subres.m_revision = event.m_etag;
            } else {
                throw;
            }
        }
    }

 done:
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
        icalcomponent *parent = NULL;
        for (icalcomponent *comp = icalcomponent_get_first_component(event.m_calendar, ICAL_VEVENT_COMPONENT);
             comp;
             comp = icalcomponent_get_next_component(event.m_calendar, ICAL_VEVENT_COMPONENT)) {
            if (Event::getSubID(comp) == subid) {
                eptr<icalcomponent> clone(icalcomponent_new_clone(comp), "VEVENT");
                if (subid.empty()) {
                    parent = clone.get();
                }
                icalcomponent_add_component(calendar, clone.release());
                found = true;
                break;
            }
        }

        if (!found) {
            SE_THROW("event not found");
        }

        // tell engine and peers about EXDATEs implied by
        // RECURRENCE-IDs in detached recurrences by creating
        // X-SYNCEVOLUTION-EXDATE-DETACHED in the parent
        if (parent && event.m_subids.size() > 1) {
            // remove all old X-SYNCEVOLUTION-EXDATE-DETACHED (just in case)
            removeSyncEvolutionExdateDetached(parent);

            // now populate with RECURRENCE-IDs of detached recurrences
            for (icalcomponent *comp = icalcomponent_get_first_component(event.m_calendar, ICAL_VEVENT_COMPONENT);
                 comp;
                 comp = icalcomponent_get_next_component(event.m_calendar, ICAL_VEVENT_COMPONENT)) {
                icalproperty *prop = icalcomponent_get_first_property(comp, ICAL_RECURRENCEID_PROPERTY);
                if (prop) {
                    eptr<char> rid(ical_strdup(icalproperty_get_value_as_string(prop)));
                    icalproperty *exdate = icalproperty_new_from_string(StringPrintf("X-SYNCEVOLUTION-EXDATE-DETACHED:%s", rid.get()).c_str());
                    if (exdate) {
                        icalparameter *tzid = icalproperty_get_first_parameter(prop, ICAL_TZID_PARAMETER);
                        if (tzid) {
                            icalproperty_add_parameter(exdate, icalparameter_new_clone(tzid));
                        }
#if 0
                        // not needed
                        if (icalproperty_get_recurrenceid(exdate).is_date) {
                            icalproperty_add_parameter(exdate, icalparameter_new_value(ICAL_VALUE_DATE));
                        }
#endif
                        icalcomponent_add_property(parent, exdate);
                    }
                }
            }
        }

        eptr<char> icalstr(ical_strdup(icalcomponent_as_ical_string(calendar)));
        item = icalstr.get();
    }
}

void CalDAVSource::Event::escapeRecurrenceID(std::string &data)
{
    boost::replace_all(data,
                       "\nRECURRENCE-ID",
                       "\nX-SYNCEVOLUTION-RECURRENCE-ID");
}

void CalDAVSource::Event::unescapeRecurrenceID(std::string &data)
{
    boost::replace_all(data,
                       "\nX-SYNCEVOLUTION-RECURRENCE-ID",
                       "\nRECURRENCE-ID");
}

std::string CalDAVSource::removeSubItem(const string &davLUID, const std::string &subid)
{
    EventCache::iterator it = m_cache.find(davLUID);
    if (it == m_cache.end()) {
        // gone already
        throwError(STATUS_NOT_FOUND, "deleting item: " + davLUID);
        return "";
    }
    // use item as it is, load only if it is not going to be removed entirely
    Event &event = *it->second;

    if (event.m_subids.size() == 1) {
        // remove entire merged item, nothing will be left after removal
        if (*event.m_subids.begin() != subid) {
            SE_LOG_DEBUG(this, NULL, "%s: request to remove the %s recurrence: only the %s recurrence exists",
                         davLUID.c_str(),
                         SubIDName(subid).c_str(),
                         SubIDName(*event.m_subids.begin()).c_str());
            throwError(STATUS_NOT_FOUND, "remove sub-item: " + SubIDName(subid) + " in " + davLUID);
            return event.m_etag;
        } else {
            try {
                removeItem(event.m_DAVluid);
            } catch (const TransportStatusException &ex) {
                if (ex.syncMLStatus() == 409 &&
                    strstr(ex.what(), "Can't delete a recurring event")) {
                    // Google CalDAV:
                    // HTTP/1.1 409 Can't delete a recurring event except on its organizer's calendar
                    //
                    // Workaround: remove RRULE and EXDATE before deleting
                    bool updated = false;
                    icalcomponent *comp = icalcomponent_get_first_component(event.m_calendar, ICAL_VEVENT_COMPONENT);
                    if (comp) {
                        icalproperty *prop;
                        while ((prop = icalcomponent_get_first_property(comp, ICAL_RRULE_PROPERTY)) != NULL) {
                            icalcomponent_remove_property(comp, prop);
                            icalproperty_free(prop);
                            updated = true;
                        }
                        while ((prop = icalcomponent_get_first_property(comp, ICAL_EXDATE_PROPERTY)) != NULL) {
                            icalcomponent_remove_property(comp, prop);
                            icalproperty_free(prop);
                            updated = true;
                        }
                    }
                    if (updated) {
                        SE_LOG_DEBUG(this, NULL, "Google recurring event delete hack: remove RRULE before deleting");
                        eptr<char> icalstr(ical_strdup(icalcomponent_as_ical_string(event.m_calendar)));
                        insertSubItem(davLUID, subid, icalstr.get());
                        // It has been observed that trying the DELETE immediately
                        // failed again with the same "Can't delete a recurring event"
                        // error although the event no longer has an RRULE. Seems
                        // that the Google server sometimes need a bit of time until
                        // changes really trickle through all databases. Let's
                        // try a few times before giving up.
                        for (int retry = 0; retry < 5; retry++) {
                            try {
                                SE_LOG_DEBUG(this, NULL, "Google recurring event delete hack: remove event, attempt #%d", retry);
                                removeSubItem(davLUID, subid);
                                break;
                            } catch (const TransportStatusException &ex2) {
                                if (ex2.syncMLStatus() == 409 &&
                                    strstr(ex2.what(), "Can't delete a recurring event")) {
                                    SE_LOG_DEBUG(this, NULL, "Google recurring event delete hack: try again in a second");
                                    sleep(1);
                                } else {
                                    throw;
                                }
                            }
                        }
                    } else {
                        SE_LOG_DEBUG(this, NULL, "Google recurring event delete hack not applicable, giving up");
                        throw;
                    }
                } else {
                    throw;
                }
            }
        }
        m_cache.erase(davLUID);
        return "";
    } else {
        loadItem(event);
        bool found = false;
        bool parentRemoved = false;
        for (icalcomponent *comp = icalcomponent_get_first_component(event.m_calendar, ICAL_VEVENT_COMPONENT);
             comp;
             comp = icalcomponent_get_next_component(event.m_calendar, ICAL_VEVENT_COMPONENT)) {
            if (Event::getSubID(comp) == subid) {
                icalcomponent_remove_component(event.m_calendar, comp);
                icalcomponent_free(comp);
                found = true;
                if (subid.empty()) {
                    parentRemoved = true;
                }
            }
        }
        if (!found) {
            throwError(STATUS_NOT_FOUND, "remove sub-item: " + SubIDName(subid) + " in " + davLUID);
            return event.m_etag;
        }
        event.m_subids.erase(subid);
        // TODO: avoid updating the item immediately
        eptr<char> icalstr(ical_strdup(icalcomponent_as_ical_string(event.m_calendar)));
        InsertItemResult res;
        if (parentRemoved && settings().googleChildHack()) {
            // Must avoid VEVENTs with RECURRENCE-ID in
            // event.m_calendar and the PUT request.  Brute-force
            // approach here is to encode as string, escape, and parse
            // again.
            string item = icalstr.get();
            Event::escapeRecurrenceID(item);
            event.m_calendar.set(icalcomponent_new_from_string((char *)item.c_str()), // hack for old libical
                                 "parsing iCalendar 2.0");
            res = insertItem(davLUID, item, true);
        } else {
            res = insertItem(davLUID, icalstr.get(), true);
        }
        if (res.m_state != ITEM_OKAY ||
            res.m_luid != davLUID) {
            SE_THROW("unexpected result of removing sub event");
        }
        event.m_etag = res.m_revision;
        return event.m_etag;
    }
}

void CalDAVSource::removeMergedItem(const std::string &davLUID)
{
    EventCache::iterator it = m_cache.find(davLUID);
    if (it == m_cache.end()) {
        // gone already, no need to do anything
        SE_LOG_DEBUG(this, NULL, "%s: ignoring request to delete non-existent item",
                     davLUID.c_str());
        return;
    }
    // use item as it is, load only if it is not going to be removed entirely
    Event &event = *it->second;

    // remove entire merged item, nothing will be left after removal
    try {
        removeItem(event.m_DAVluid);
    } catch (const TransportStatusException &ex) {
        if (ex.syncMLStatus() == 409 &&
            strstr(ex.what(), "Can't delete a recurring event")) {
            // Google CalDAV:
            // HTTP/1.1 409 Can't delete a recurring event except on its organizer's calendar
            //
            // Workaround: use the workarounds from removeSubItem()
            std::set<std::string> subids = event.m_subids;
            for (std::set<std::string>::reverse_iterator it = subids.rbegin();
                 it != subids.rend();
                 ++it) {
                removeSubItem(davLUID, *it);
            }
        } else {
            throw;
        }
    }

    m_cache.erase(davLUID);
}

void CalDAVSource::flushItem(const string &davLUID)
{
    // TODO: currently we always flush immediately, so no need to send data here
    EventCache::iterator it = m_cache.find(davLUID);
    if (it != m_cache.end()) {
        it->second->m_calendar.set(NULL);
    }
}

std::string CalDAVSource::getSubDescription(const string &davLUID, const string &subid)
{
    EventCache::iterator it = m_cache.find(davLUID);
    if (it == m_cache.end()) {
        // unknown item, return empty string for fallback
        return "";
    } else {
        return getSubDescription(*it->second, subid);
    }
}

std::string CalDAVSource::getSubDescription(Event &event, const string &subid)
{
    if (!event.m_calendar) {
        // Don't load (expensive!) only to provide the description.
        // Returning an empty string will trigger the fallback (logging the ID).
        return "";
    }
    for (icalcomponent *comp = icalcomponent_get_first_component(event.m_calendar, ICAL_VEVENT_COMPONENT);
         comp;
         comp = icalcomponent_get_next_component(event.m_calendar, ICAL_VEVENT_COMPONENT)) {
        if (Event::getSubID(comp) == subid) {
            std::string descr;

            const char *summary = icalcomponent_get_summary(comp);
            if (summary && summary[0]) {
                descr += summary;
            }
        
            if (true /* is event */) {
                const char *location = icalcomponent_get_location(comp);
                if (location && location[0]) {
                    if (!descr.empty()) {
                        descr += ", ";
                    }
                    descr += location;
                }
            }
            // TODO: other item types
            return descr;
        }
    }
    return "";
}

std::string CalDAVSource::getDescription(const string &luid)
{
    StringPair ids = MapSyncSource::splitLUID(luid);
    return getSubDescription(ids.first, ids.second);
}

CalDAVSource::Event &CalDAVSource::findItem(const std::string &davLUID)
{
    EventCache::iterator it = m_cache.find(davLUID);
    if (it == m_cache.end()) {
        throwError(STATUS_NOT_FOUND, "finding item: " + davLUID);
    }
    return *it->second;
}

CalDAVSource::Event &CalDAVSource::loadItem(const std::string &davLUID)
{
    Event &event = findItem(davLUID);
    return loadItem(event);
}

CalDAVSource::Event &CalDAVSource::loadItem(Event &event)
{
    if (!event.m_calendar) {
        std::string item;
        try {
            readItem(event.m_DAVluid, item, true);
        } catch (const TransportStatusException &ex) {
            if (ex.syncMLStatus() == 404) {
                // Someone must have created a detached recurrence on
                // the server without the master event. We avoid that
                // with the "Google Child Hack", but have no control
                // over other clients. So let's deal with this problem
                // after logging it.
                Exception::log();

                // We know about the event because it showed up in a REPORT.
                // So let's use such a REPORT to retrieve the desired item.
                // Not as efficient as a GET (and thus not the default), but
                // so be it.
#if 0
                // This would be fairly efficient, but runs into the same 404 error as a GET.
                std::string query =
                    StringPrintf("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                                 "<C:calendar-multiget xmlns:D=\"DAV:\"\n"
                                 "   xmlns:C=\"urn:ietf:params:xml:ns:caldav\">\n"
                                 "<D:prop>\n"
                                 "   <C:calendar-data/>\n"
                                 "</D:prop>\n"
                                 "<D:href><[CDATA[%s]]></D:href>\n"
                                 "</C:calendar-multiget>",
                                 event.m_DAVluid.c_str());
                Neon::XMLParser parser;
                std::string href, etag;
                item = "";
                parser.initReportParser(href, etag);
                parser.pushHandler(boost::bind(Neon::XMLParser::accept, "urn:ietf:params:xml:ns:caldav", "calendar-data", _2, _3),
                                   boost::bind(Neon::XMLParser::append, boost::ref(item), _2, _3));
                Neon::Request report(*getSession(), "REPORT", getCalendar().m_path, query, parser);
                report.addHeader("Content-Type", "application/xml; charset=\"utf-8\"");
                report.run();
#else
                std::string query =
                    StringPrintf("<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n"
                                 "<C:calendar-query xmlns:D=\"DAV:\"\n"
                                 "xmlns:C=\"urn:ietf:params:xml:ns:caldav\">\n"
                                 "<D:prop>\n"
                                 "<D:getetag/>\n"
                                 "<C:calendar-data/>\n"
                                 "</D:prop>\n"
                                 // filter expected by Yahoo! Calendar
                                 "<C:filter>\n"
                                 "<C:comp-filter name=\"VCALENDAR\">\n"
                                 "<C:comp-filter name=\"VEVENT\">\n"
                                 "<C:prop-filter name=\"UID\">\n"
                                 "<C:text-match collation=\"i;octet\"><![CDATA[%s]]></C:text-match>\n"
                                 "</C:prop-filter>\n"
                                 "</C:comp-filter>\n"
                                 "</C:comp-filter>\n"
                                 "</C:filter>\n"
                                 "</C:calendar-query>\n",
                                 event.m_UID.c_str());
                Timespec deadline = createDeadline();
                getSession()->startOperation("REPORT 'single item'", deadline);
                while (true) {
                    Neon::XMLParser parser;
                    parser.initReportParser();
                    item = "";
                    parser.pushHandler(boost::bind(Neon::XMLParser::accept, "urn:ietf:params:xml:ns:caldav", "calendar-data", _2, _3),
                                       boost::bind(Neon::XMLParser::append, boost::ref(item), _2, _3));
                    Neon::Request report(*getSession(), "REPORT", getCalendar().m_path, query, parser);
                    report.addHeader("Depth", "1");
                    report.addHeader("Content-Type", "application/xml; charset=\"utf-8\"");
                    if (report.run()) {
                        break;
                    }
                }
#endif
            } else {
                throw;
            }
        }
        Event::unescapeRecurrenceID(item);
        event.m_calendar.set(icalcomponent_new_from_string((char *)item.c_str()), // hack for old libical
                             "parsing iCalendar 2.0");
        Event::fixIncomingCalendar(event.m_calendar.get());

        // Sequence number/last-modified might have been increased by last save.
        // Or the cache was populated by setAllSubItems(), which doesn't give
        // us the information. In that case, UID might also still be unknown.
        // Either way, check it again.
        for (icalcomponent *comp = icalcomponent_get_first_component(event.m_calendar, ICAL_VEVENT_COMPONENT);
             comp;
             comp = icalcomponent_get_next_component(event.m_calendar, ICAL_VEVENT_COMPONENT)) {
            if (event.m_UID.empty()) {
                event.m_UID = Event::getUID(comp);
            }
            long sequence = Event::getSequence(comp);
            if (sequence > event.m_sequence) {
                event.m_sequence = sequence;
            }
            icalproperty *lastmod = icalcomponent_get_first_property(comp, ICAL_LASTMODIFIED_PROPERTY);
            if (lastmod) {
                icaltimetype lastmodtime = icalproperty_get_lastmodified(lastmod);
                time_t mod = icaltime_as_timet(lastmodtime);
                if (mod > event.m_lastmodtime) {
                    event.m_lastmodtime = mod;
                }
            }
        }
    }
    return event;
}

void CalDAVSource::Event::fixIncomingCalendar(icalcomponent *calendar)
{
    // Evolution has a problem when the parent event uses a time
    // zone and the RECURRENCE-ID uses UTC (can happen in Exchange
    // meeting invitations): then Evolution and/or libical do not
    // recognize that the detached recurrence overrides the
    // regular recurrence and display both.
    //
    // As a workaround, remember time zone of DTSTART in parent event
    // in the first loop iteration. Then below transform the RECURRENCE-ID
    // time.
    bool ridInUTC = false;
    const icaltimezone *zone = NULL;

    for (icalcomponent *comp = icalcomponent_get_first_component(calendar, ICAL_VEVENT_COMPONENT);
         comp;
         comp = icalcomponent_get_next_component(calendar, ICAL_VEVENT_COMPONENT)) {
        // remember whether we need to convert RECURRENCE-ID
        struct icaltimetype rid = icalcomponent_get_recurrenceid(comp);
        if (icaltime_is_utc(rid)) {
            ridInUTC = true;
        }

        // is parent event? -> remember time zone unless it is UTC
        static const struct icaltimetype null = { 0 };
        if (!memcmp(&rid, &null, sizeof(null))) {
            struct icaltimetype dtstart = icalcomponent_get_dtstart(comp);
            if (!icaltime_is_utc(dtstart)) {
                zone = icaltime_get_timezone(dtstart);
            }
        }

        // remove useless X-LIC-ERROR
        icalproperty *prop = icalcomponent_get_first_property(comp, ICAL_ANY_PROPERTY);
        while (prop) {
            icalproperty *next = icalcomponent_get_next_property(comp, ICAL_ANY_PROPERTY);
            const char *name = icalproperty_get_property_name(prop);
            if (name && !strcmp("X-LIC-ERROR", name)) {
                icalcomponent_remove_property(comp, prop);
                icalproperty_free(prop);
            }
            prop = next;
        }
    }

    // now update RECURRENCE-ID?
    if (zone && ridInUTC) {
        for (icalcomponent *comp = icalcomponent_get_first_component(calendar, ICAL_VEVENT_COMPONENT);
             comp;
             comp = icalcomponent_get_next_component(calendar, ICAL_VEVENT_COMPONENT)) {
            icalproperty *prop = icalcomponent_get_first_property(comp, ICAL_RECURRENCEID_PROPERTY);
            if (prop) {
                struct icaltimetype rid = icalproperty_get_recurrenceid(prop);
                if (icaltime_is_utc(rid)) {
                    rid = icaltime_convert_to_zone(rid, const_cast<icaltimezone *>(zone)); // icaltime_convert_to_zone should take a "const timezone" but doesn't
                    icalproperty_set_recurrenceid(prop, rid);
                    icalproperty_remove_parameter_by_kind(prop, ICAL_TZID_PARAMETER);
                    icalparameter *param = icalparameter_new_from_value_string(ICAL_TZID_PARAMETER,
                                                                               icaltimezone_get_tzid(const_cast<icaltimezone *>(zone)));
                    icalproperty_set_parameter(prop, param);
                }
            }
        }
    }
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

std::string CalDAVSource::Event::getUID(icalcomponent *comp)
{
    std::string uid;
    icalproperty *prop = icalcomponent_get_first_property(comp, ICAL_UID_PROPERTY);
    if (prop) {
        uid = icalproperty_get_uid(prop);
    }
    return uid;
}

void CalDAVSource::Event::setUID(icalcomponent *comp, const std::string &uid)
{
    icalproperty *prop = icalcomponent_get_first_property(comp, ICAL_UID_PROPERTY);
    if (prop) {
        icalproperty_set_uid(prop, uid.c_str());
    } else {
        icalcomponent_add_property(comp, icalproperty_new_uid(uid.c_str()));
    }
}

int CalDAVSource::Event::getSequence(icalcomponent *comp)
{
    int sequence = 0;
    icalproperty *prop = icalcomponent_get_first_property(comp, ICAL_SEQUENCE_PROPERTY);
    if (prop) {
        sequence = icalproperty_get_sequence(prop);
    }
    return sequence;
}

void CalDAVSource::Event::setSequence(icalcomponent *comp, int sequence)
{
    icalproperty *prop = icalcomponent_get_first_property(comp, ICAL_SEQUENCE_PROPERTY);
    if (prop) {
        icalproperty_set_sequence(prop, sequence);
    } else {
        icalcomponent_add_property(comp, icalproperty_new_sequence(sequence));
    }
}

CalDAVSource::EventCache::iterator CalDAVSource::EventCache::findByUID(const std::string &uid)
{
    for (iterator it = begin();
         it != end();
         ++it) {
        if (it->second->m_UID == uid) {
            return it;
        }
    }
    return end();
}

void CalDAVSource::backupData(const SyncSource::Operations::ConstBackupInfo &oldBackup,
                              const SyncSource::Operations::BackupInfo &newBackup,
                              BackupReport &backupReport)
{
    contactServer();

    // If this runs as part of the sync preparations, then we might
    // use the result to populate our m_cache. But because dumping
    // data is typically disabled, this optimization isn't really
    // worth that much.

    ItemCache cache;
    cache.init(oldBackup, newBackup, false);

    // stream directly from REPORT with full data into backup
    const std::string query =
        "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n"
        "<C:calendar-query xmlns:D=\"DAV:\"\n"
        "xmlns:C=\"urn:ietf:params:xml:ns:caldav\">\n"
        "<D:prop>\n"
        "<D:getetag/>\n"
        "<C:calendar-data/>\n"
        "</D:prop>\n"
        // filter expected by Yahoo! Calendar
        "<C:filter>\n"
        "<C:comp-filter name=\"VCALENDAR\">\n"
        "<C:comp-filter name=\"VEVENT\">\n"
        "</C:comp-filter>\n"
        "</C:comp-filter>\n"
        "</C:filter>\n"
        "</C:calendar-query>\n";
    string data;
    Neon::XMLParser parser;
    parser.initReportParser(boost::bind(&CalDAVSource::backupItem, this,
                                        boost::ref(cache),
                                        _1, _2, boost::ref(data)));
    parser.pushHandler(boost::bind(Neon::XMLParser::accept, "urn:ietf:params:xml:ns:caldav", "calendar-data", _2, _3),
                       boost::bind(Neon::XMLParser::append, boost::ref(data), _2, _3));
    Timespec deadline = createDeadline();
    getSession()->startOperation("REPORT 'full calendar'", deadline);
    while (true) {
        Neon::Request report(*getSession(), "REPORT", getCalendar().m_path, query, parser);
        report.addHeader("Depth", "1");
        report.addHeader("Content-Type", "application/xml; charset=\"utf-8\"");
        if (report.run()) {
            break;
        }
        cache.reset();
    }
    cache.finalize(backupReport);
}

int CalDAVSource::backupItem(ItemCache &cache,
                             const std::string &href,
                             const std::string &etag,
                             std::string &data)
{
    // detect and ignore empty items, like we do in appendItem()
    eptr<icalcomponent> calendar(icalcomponent_new_from_string((char *)data.c_str()), // cast is a hack for broken definition in old libical
                                 "iCalendar 2.0");
    if (icalcomponent_get_first_component(calendar, ICAL_VEVENT_COMPONENT)) {
        Event::unescapeRecurrenceID(data);
        std::string luid = path2luid(Neon::URI::parse(href).m_path);
        std::string rev = ETag2Rev(etag);
        cache.backupItem(data, luid, rev);
    } else {
        SE_LOG_DEBUG(NULL, NULL, "ignoring broken item %s during backup (is empty)", href.c_str());
    }

    // reset data for next item
    data.clear();
    return 0;
}

void CalDAVSource::restoreData(const SyncSource::Operations::ConstBackupInfo &oldBackup,
                               bool dryrun,
                               SyncSourceReport &report)
{
    // TODO: implement restore
    throw("not implemented");
}

bool CalDAVSource::typeMatches(const StringMap &props) const
{
    StringMap::const_iterator it = props.find("urn:ietf:params:xml:ns:caldav:supported-calendar-component-set");
    if (it != props.end() &&
        it->second.find("<urn:ietf:params:xml:ns:caldavcomp name='VEVENT'></urn:ietf:params:xml:ns:caldavcomp>") != std::string::npos) {
        return true;
    } else {
        return false;
    }
}

SE_END_CXX

#endif // ENABLE_DAV
