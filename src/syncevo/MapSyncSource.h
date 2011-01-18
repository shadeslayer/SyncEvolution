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
using namespace std;

class MapSyncSource;

/**
 * This is the API that must be implemented in addition to
 * TrackingSyncSource and SyncSourceLogging by a source to
 * be wrapped by MapSyncSource.
 *
 * The original interface will only be used in "raw" mode, which
 * should bypass any kind of cache used by the implementation and are
 * guaranteed to be passed merged items.
 *
 * The new methods with uid and subid are using during a sync
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
            m_merged(false)
        {}
        
        /**
         * @param uid       the uid after the operation; during an update the uid must
         *                  not be changed, so return the original one here
         * @param subid     optional subid, same rules as for uid
         * @param revision  the revision string of the merged item after the operation; leave empty if not used
         * @param merged    set this to true if an existing sub item was updated instead of adding it
         */
        SubItemResult(const string &uid,
                      const string &subid,
                      const string &revision,
                      bool merged) :
            m_uid(uid),
            m_subid(subid),
            m_revision(revision),
            m_merged(merged)
        {}

        string m_uid;
        string m_subid;
        string m_revision;
        bool m_merged;
    };

    SubSyncSource() : m_parent(NULL) {}

    /**
     * tells SubSyncSource about MapSyncSource which wraps it,
     * for getSynthesisAPI()
     */
    void setParent(MapSyncSource *parent) { m_parent = parent; }
    MapSyncSource *getParent() const { return m_parent; }

    virtual SDKInterface *getSynthesisAPI() const;

    /**
     * uid to rev + list of subid
     *
     * Must contain an empty entry for the main item, if one exists.
     */
    typedef map<string, pair<string, set<string> > > SubRevisionMap_t;

    virtual void listAllSubItems(SubRevisionMap_t &revisions) = 0;
    virtual SubItemResult insertSubItem(const std::string &uid, const std::string &subid,
                                        const std::string &item) = 0;
    virtual void readSubItem(const std::string &uid, const std::string &subid, std::string &item) = 0;

    /**
     * @return empty string if item is empty after removal, otherwise new revision string
     */
    virtual std::string removeSubItem(const string &uid, const std::string &subid) = 0;

    /**
     * Called whenever this class thinks that the item may no longer be
     * needed. Might be wrong...
     */
    virtual void flushItem(const string &uid) = 0;

    virtual std::string getSubDescription(const string &uid, const string &subid) = 0;

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
 * - luid = SyncEvolution locally unique ID (VEVENT)
 * - uid = unique ID for underlying item (UID)
 * - subid = unique ID (RECURRENCE-ID) for sub-items (VEVENT) inside underlying item (VCALENDAR)
 * - single item = item as presented by this class
 * - merged item = item sharing the same uid
 *
 * "luid" is composed from "uid" and "subid" by backslash-escaping a
 * slash (/), then the concatenating the results with a slash as
 * separator. If the subid is empty, no slash is added. The main
 * advantage of this scheme is that it works well with sub sources
 * which use URI escaping.
 *
 * This class maps expects an extended TrackingSyncSource (=
 * SubSyncSource) merely because that interface is the most convenient
 * to work with. If necessary, the logic may be refactored later on.
 *
 * This class uses the TrackingSyncSource infrastructure. The effect that
 * all sub items of a merged item share the same revision string is modelled
 * by telling the SyncSourceRevision instance to use a tracking node which
 * maps all luid keys to the same entry.
 *
 * The following rules apply:
 * - A single item is added if its luid is new, updated if it exists and
 *   the merged item's revision string is different, deleted if the luid is
 *   gone (same logic as in normal TrackingSyncSource).
 * - A uid is assigned to a new merged item by creating the merged item.
 * - Changes for an existing merged item may be applied to a cache,
 *   which is explicitly flushed by this class. This implies that
 *   such local changes must keep the uid stable and have control
 *   over the subid.
 * - Item logging is offered by this class (LoggingSyncSource), but
 *   entirely depends on the sub source to implement the functionality.
 */
class MapSyncSource : public TrackingSyncSource,
    virtual public SyncSourceLogging
{
  public:
    /**
     * @param sub      must also implement TrackingSyncSource and SyncSourceLogging interfaces!
     */
    MapSyncSource(const SyncSourceParams &params,
                  int granularitySeconds,
                  const boost::shared_ptr<SubSyncSource> &sub);
    ~MapSyncSource() {}

    /** compose luid from uid and subid */
    static std::string createLUID(const std::string &uid, const std::string &subid);

    /** split luid into uid (first) and subid (second) */
    static StringPair splitLUID(const std::string &luid);

    virtual Databases getDatabases() { return dynamic_cast<SyncSource &>(*m_sub).getDatabases(); }
    virtual void open() { dynamic_cast<SyncSource &>(*m_sub).open(); }
    virtual bool isEmpty() { return dynamic_cast<SyncSource &>(*m_sub).getOperations().m_isEmpty(); }
    virtual void listAllItems(SyncSourceRevisions::RevisionMap_t &revisions);
    virtual InsertItemResult insertItem(const std::string &luid, const std::string &item, bool raw);
    virtual void readItem(const std::string &luid, std::string &item, bool raw);
    virtual void removeItem(const string &luid);
    virtual void close() { dynamic_cast<SyncSource &>(*m_sub).close(); }
    virtual std::string getMimeType() const { return dynamic_cast<SyncSourceSerialize &>(*m_sub).getMimeType(); }
    virtual std::string getMimeVersion() const { return dynamic_cast<SyncSourceSerialize &>(*m_sub).getMimeVersion(); }
    virtual std::string getDescription(sysync::KeyH aItemKey) { return dynamic_cast<SyncSourceLogging &>(*m_sub).getDescription(aItemKey); }
    virtual std::string getDescription(const string &luid);

 private:
    boost::shared_ptr<SubSyncSource> m_sub;
    static StringEscape m_escape;
    std::string m_oldIDs;

    /**
     * Flush sub source if new luid is different from previous one.
     * Works for SyncML calendar sync based on the assumption that
     * item operations are sorted by UID/RECURRENCE-ID, which is
     * roughly true. Exception: Synthesis sorts by add/update/delete
     * first.
     */
    void checkFlush(const std::string &uid);
};

SE_END_CXX
#endif // INCL_MAPSYNCSOURCE
