/*
 * Copyright (C) 2010 Intel Corporation
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

#ifndef INCL_MAPSYNCSOURCE
#define INCL_MAPSYNCSOURCE

#include <syncevo/TrackingSyncSource.h>
#include <syncevo/util.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

class MapSyncSource;

/**
 * rev + uid + list of subid; mainid is part of the context
 */
struct SubRevisionEntry {
    std::string m_revision;
    std::string m_uid;
    set<string> m_subids;
};

/**
 * mainid to rev + uid + list of subid
 *
 * List must contain an empty entry for the main item, if and only
 * if one exists.
 */
typedef map<string, SubRevisionEntry> SubRevisionMap_t;

/**
 * This is the API that must be implemented in addition to
 * TrackingSyncSource and SyncSourceLogging by a source to
 * be wrapped by MapSyncSource.
 *
 * The original interface will only be used in "raw" mode, which
 * should bypass any kind of cache used by the implementation.
 * They are guaranteed to be passed merged items.
 *
 * The new methods with mainid and subid are using during a sync
 * and should use the cache. They work on single items but modify
 * merged items. Thus the revision string of all sub items in
 * the same merged item will get modified when manipulating
 * one of its sub items.
 */
class SubSyncSource : virtual public SyncSourceBase
{
 public:
    class SubItemResult {
    public:
        SubItemResult() :
            m_state(ITEM_OKAY)
        {}
        
        /**
         * @param mainid    the ID used to access a set of items; may be different
         *                  from a (iCalendar 2.0) UID; during an update the mainid must
         *                  not be changed, so return the original one here
         * @param subid     optional subid, same rules as for mainid
         * @param revision  the revision string of the merged item after the operation; leave empty if not used
         * @param uid       an arbitrary string, stored, but not used by MapSyncSource;
         *                  used in the CalDAV backend to associate mainid (= resource path)
         *                  with UID (= part of the item content, but with special semantic)
         * @param state     report about what was done with the data
         */
        SubItemResult(const string &mainid,
                      const string &subid,
                      const string &revision,
                      const string &uid,
                      InsertItemResultState state) :
            m_mainid(mainid),
            m_subid(subid),
            m_revision(revision),
            m_uid(uid),
            m_state(state)
        {}

        string m_mainid;
        string m_subid;
        string m_revision;
        string m_uid;
        InsertItemResultState m_state;
    };

    SubSyncSource() : m_parent(NULL) {}

    /**
     * tells SubSyncSource about MapSyncSource which wraps it,
     * for getSynthesisAPI()
     */
    void setParent(MapSyncSource *parent) { m_parent = parent; }
    MapSyncSource *getParent() const { return m_parent; }

    virtual SDKInterface *getSynthesisAPI() const;

    /** called after open() and before any of the following methods */
    virtual void begin() = 0;

    /** called after a sync */
    virtual void endSubSync(bool success) = 0;

    /**
     * A unique identifier for the current state of the complete database.
     * The semantic is the following:
     * - empty string implies "state unknown" or "identifier not supported"
     * - id not empty and ID1 == ID2 implies "nothing has changed";
     *   the inverse is not true (ids may be different although nothing has changed)
     *
     * Matches TrackingSyncSource::databaseRevision().
     */
    virtual std::string subDatabaseRevision() { return ""; }

    /**
     * Either listAllSubItems(), setAllSubItems(), or updateAllSubitems()
     * will be called after begin().
     *
     * In the first case, the sub source is expected to provide a full list
     * of its items. In the second case, the caller was able to determine
     * that its cached copy of that list is still correct and provides it
     * the the source. In the third case, some revision information is know,
     * but it may be obsolete (revision string and/or subids changed or removed)
     * or incomplete (new items missing). The callee then must update the
     * information, possibly by falling back to listAllSubItems().
     */
    virtual void listAllSubItems(SubRevisionMap_t &revisions) = 0;

    /**
     * Called instead of listAllSubItems().
     */
    virtual void updateAllSubItems(SubRevisionMap_t &revisions) {
        revisions.clear();
        listAllSubItems(revisions);
    }

    /**
     * Called instead of listAllSubItems().
     */
    virtual void setAllSubItems(const SubRevisionMap_t &revisions) = 0;

    virtual SubItemResult insertSubItem(const std::string &mainid, const std::string &subid,
                                        const std::string &item) = 0;
    virtual void readSubItem(const std::string &mainid, const std::string &subid, std::string &item) = 0;

    /**
     * Ensure that the sub-item does not exist. It is not an error to be called
     * for a non-existent sub-item or item.
     *
     * @return empty string if item is empty after removal, otherwise new revision string
     */
    virtual std::string removeSubItem(const string &mainid, const std::string &subid) = 0;

    /**
     * Remove all sub-items belonging to mainid.
     */
    virtual void removeMergedItem(const std::string &mainid) = 0;

    /**
     * Called whenever this class thinks that the item may no longer be
     * needed. Might be wrong...
     */
    virtual void flushItem(const string &mainid) = 0;

    /**
     * Describe sub-item. Might be called for item which does not exist and
     * must not throw an error in that case. Providing a description is optional
     * and should only be done when it is reasonably cheap.
     */
    virtual std::string getSubDescription(const string &mainid, const string &subid) = 0;

    /**
     * Called after MapSyncSource already populated the info structure.
     */
    virtual void updateSynthesisInfo(SynthesisInfo &info,
                                     XMLConfigFragments &fragments) {}

 private:
    MapSyncSource *m_parent;
};

/**
 * This class wraps an underlying SyncSource and maps each of the
 * underlying items into one or more items in this class. The main use case
 * are CalDAV and Exchange Web Services, where each item is a set
 * of all events with the same UID, whereas SyncEvolution and SyncML
 * treat each individual event as one item.
 *
 * Terminology:
 * - single item = item as presented by this class (VEVENT)
 * - merged item = combination of all items sharing the same luid/uid (VCALENDAR)
 * - luid = SyncEvolution locally unique ID (VEVENT), mapped to mainid+subid
 * - mainid = ID for accessing the set of items (WebDAV resource path)
 * - subid = unique ID (RECURRENCE-ID) for sub-items (VEVENT) inside underlying item (VCALENDAR)
 * - uid = another unique ID shared by underlying items (iCalendar 2.0 UID),
 *         not used by this class
 *
 * "luid" is composed from "mainid" and "subid" by backslash-escaping a
 * slash (/), then concatenating the results with a slash as
 * separator. If the subid is empty, no slash is added. The main
 * advantage of this scheme is that it works well with sub sources
 * which use URI escaping.
 *
 * This class expects an extended TrackingSyncSource (=
 * SubSyncSource) merely because that interface is the most convenient
 * to work with. If necessary, the logic may be refactored later on.
 *
 * This class uses much of the same infrastructure as the TrackingSyncSource,
 * except for change detection. This used to be done with TrackingSyncSource
 * as base class, but there are some key differences which made this very
 * awkward, primarily the fact that all merged items share the same revision
 * string. Now the tracking node is used to store one entry per merged item, in
 * the format "ref-<mainid> = <revision>/<uid>/<subid1>/<subid2>/..."
 *
 * The following rules apply:
 * - A single item is added if its luid is new, updated if it exists and
 *   the merged item's revision string is different, deleted if the luid is
 *   gone (same logic as in normal TrackingSyncSource).
 * - A mainid is assigned to a new merged item by creating the merged item.
 * - Changes for an existing merged item may be applied to a cache,
 *   which is explicitly flushed by this class. This implies that
 *   such local changes must keep the mainid stable and have control
 *   over the subid.
 * - Item logging is offered by this class (LoggingSyncSource), but
 *   entirely depends on the sub source to implement the functionality.
 */
class MapSyncSource :
    public TestingSyncSource, // == SyncSourceSession, SyncSourceChanges, SyncSourceDelete, SyncSourceSerialize
    virtual public SyncSourceAdmin,
    virtual public SyncSourceBlob,
    virtual public SyncSourceLogging
{
  public:
    /**
     * @param sub      must also implement TrackingSyncSource and SyncSourceLogging interfaces!
     */
    MapSyncSource(const SyncSourceParams &params,
                  const boost::shared_ptr<SubSyncSource> &sub);
    ~MapSyncSource() {}

    /** compose luid from mainid and subid */
    static std::string createLUID(const std::string &mainid, const std::string &subid);

    /** split luid into mainid (first) and subid (second) */
    static StringPair splitLUID(const std::string &luid);

    virtual void enableServerMode();
    virtual bool serverModeEnabled() const;
    virtual std::string getPeerMimeType() const { return getMimeType(); }
    virtual Databases getDatabases() { return dynamic_cast<SyncSource &>(*m_sub).getDatabases(); }
    virtual void open() { dynamic_cast<SyncSource &>(*m_sub).open(); }
    virtual void beginSync(const std::string &lastToken, const std::string &resumeToken);
    virtual std::string endSync(bool success);
    virtual bool isEmpty() { return dynamic_cast<SyncSource &>(*m_sub).getOperations().m_isEmpty(); }
    virtual InsertItemResult insertItem(const std::string &luid, const std::string &item);
    virtual void readItem(const std::string &luid, std::string &item);
    virtual void deleteItem(const string &luid);
    virtual void close() { dynamic_cast<SyncSource &>(*m_sub).close(); }
    virtual std::string getMimeType() const { return dynamic_cast<SyncSourceSerialize &>(*m_sub).getMimeType(); }
    virtual std::string getMimeVersion() const { return dynamic_cast<SyncSourceSerialize &>(*m_sub).getMimeVersion(); }
    virtual std::string getDescription(sysync::KeyH aItemKey) { return dynamic_cast<SyncSourceLogging &>(*m_sub).getDescription(aItemKey); }
    virtual std::string getDescription(const string &luid);

    /* TestingSyncSource */
    virtual void removeAllItems();

 protected:
    virtual void getSynthesisInfo(SynthesisInfo &info,
                                  XMLConfigFragments &fragments) {
        TestingSyncSource::getSynthesisInfo(info, fragments);
        m_sub->updateSynthesisInfo(info, fragments);
    }

 private:
    boost::shared_ptr<SubSyncSource> m_sub;
    /** escape / in uid with %2F, so that splitMainIDValue() and splitLUID() can use / as separator */
    static StringEscape m_escape;
    std::string m_oldLUID;

    /**
     * information about the current set of items:
     * initialized as part of beginSync(),
     * updated as items are modified,
     * stored in endSync()
     */
    SubRevisionMap_t m_revisions;

    /** on-disk representation of m_revisions */
    boost::shared_ptr<ConfigNode> m_trackingNode;

    /**
     * Stores meta information besides the item list:
     * - "databaseRevision" = result of databaseRevision() at end of last sync
     *
     * Shares the same key/value store as m_trackingNode,
     * which uses the "item-" prefix in its keys to
     * avoid name clashes.
     */
    boost::shared_ptr<ConfigNode> m_metaNode;

    /** mirrors SyncSourceRevisions::detectChanges() */
    void detectChanges(SyncSourceRevisions::ChangeMode mode);
};

SE_END_CXX
#endif // INCL_MAPSYNCSOURCE
