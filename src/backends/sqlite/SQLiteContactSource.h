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

#ifndef INCL_SQLITECONTACTSOURCE
#define INCL_SQLITECONTACTSOURCE

#include <syncevo/SyncSource.h>
#include <syncevo/PrefixConfigNode.h>
#include <syncevo/SafeConfigNode.h>
#include <SQLiteUtil.h>

#include <boost/bind.hpp>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

#ifdef ENABLE_SQLITE

/**
 * Uses SQLiteUtil for contacts with a schema inspired by the one used
 * by Mac OS X.  That schema has hierarchical tables which is not
 * supported by SQLiteUtil, therefore SQLiteContactSource uses a
 * simplified schema where each contact consists of one row in the
 * database table.
 *
 * The handling of the "N" and "ORG" property shows how mapping
 * between one property and multiple different columns works.
 *
 * Properties which can occur more than once per contact like address,
 * email and phone numbers are not supported. They would have to be
 * stored in additional tables.
 *
 * Change tracking is done by implementing a modification date as part
 * of each contact and using that as the revision string.
 * The database file is created automatically if the database ID is
 * file:///<path>.
 */
class SQLiteContactSource : public SyncSource,
    virtual public SyncSourceSession,
    virtual public SyncSourceAdmin,
    virtual public SyncSourceBlob,
    virtual public SyncSourceRevisions,
    virtual public SyncSourceDelete,
    virtual public SyncSourceLogging,
    virtual public SyncSourceChanges
{
  public:
    SQLiteContactSource(const SyncSourceParams &params) :
        SyncSource(params),
        m_trackingNode(new PrefixConfigNode("item-",
                                            boost::shared_ptr<ConfigNode>(new SafeConfigNode(params.m_nodes.getTrackingNode()))))
        {
            SyncSourceSession::init(m_operations);
            SyncSourceDelete::init(m_operations);
            SyncSourceRevisions::init(NULL, NULL, 1, m_operations);
            SyncSourceChanges::init(m_operations);

            m_operations.m_isEmpty = boost::bind(&SQLiteContactSource::isEmpty, this);
            m_operations.m_readItemAsKey = boost::bind(&SQLiteContactSource::readItemAsKey, this, _1, _2);
            m_operations.m_insertItemAsKey = boost::bind(&SQLiteContactSource::insertItemAsKey, this, _1, (sysync::cItemID)NULL, _2);
            m_operations.m_updateItemAsKey = boost::bind(&SQLiteContactSource::insertItemAsKey, this, _1, _2, _3);
            SyncSourceLogging::init(InitList<std::string> ("N_FIRST")+"N_MIDDLE"+"N_LAST", ", ", m_operations);
        }

 protected:
    /* implementation of SyncSource interface */
    virtual void open();
    virtual void close();
    virtual Databases getDatabases();
    virtual void enableServerMode();
    virtual bool serverModeEnabled() const;
    virtual std::string getPeerMimeType() const { return "text/x-vcard"; }

    /* Methods in SyncSource */
    virtual void getSynthesisInfo (SynthesisInfo &info, XMLConfigFragments &fragment);
    sysync::TSyError readItemAsKey(sysync::cItemID aID, sysync::KeyH aItemKey);
    sysync::TSyError insertItemAsKey(sysync::KeyH aItemKey, sysync::cItemID aID, sysync::ItemID newID);

    /* Methods in SyncSourceSession*/
    virtual void beginSync(const std::string &lastToken, const std::string &resumeToken);
    virtual std::string endSync(bool success);

    /* Methods in SyncSourceDelete*/
    virtual void deleteItem(const string &luid);

    /* Methods in SyncSourceRevisions */
    virtual void listAllItems(RevisionMap_t &revisions);
 private:
    /** encapsulates access to database */
    boost::shared_ptr<ConfigNode> m_trackingNode;
    SQLiteUtil m_sqlite;

    /** implements the m_isEmpty operation */
    bool isEmpty();
};

#endif // ENABLE_SQLITE

SE_END_CXX
#endif // INCL_SQLITECONTACTSOURCE
