/*
 * Copyright (C) 2005-2006 Patrick Ohly
 * Copyright (C) 2007 Funambol
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

#ifndef INCL_EVOLUTIONCONTACTSOURCE
#define INCL_EVOLUTIONCONTACTSOURCE

#include <config.h>
#include "EvolutionSyncSource.h"
#include "EvolutionSmartPtr.h"

#ifdef ENABLE_EBOOK

#include <libebook/e-book.h>
#include <libebook/e-vcard.h>

#include <set>

/**
 * Implements access to Evolution address books.
 */
class EvolutionContactSource : public EvolutionSyncSource
{
  public:
    /**
     * Creates a new Evolution address book source.
     *
     * @param    changeId    is used to track changes in the Evolution backend;
     *                       not specifying it implies that always all items are returned
     * @param    id          identifies the backend; not specifying it makes this instance
     *                       unusable for anything but listing backend databases
     */
    EvolutionContactSource( const string &name,
                            SyncSourceConfig *sc,
                            const string &changeId = string(""),
                            const string &id = string(""),
                            EVCardFormat vcardFormat = EVC_FORMAT_VCARD_30 );
    EvolutionContactSource( const EvolutionContactSource &other );
    virtual ~EvolutionContactSource() { close(); }

    //
    // utility function for testing:
    // returns a pointer to an Evolution contact (memory owned
    // by Evolution) with the given UID, NULL if not found or failure
    //
    EContact *getContact( const string &uid );

    // utility function: extract vcard from item in format suitable for Evolution
    string preparseVCard(SyncItem& item);

    //
    // implementation of EvolutionSyncSource
    //
    virtual sources getSyncBackends();
    virtual void open();
    virtual void close(); 
    virtual void exportData(ostream &out);
    virtual string fileSuffix() { return "vcf"; }
    virtual const char *getMimeType();
    virtual const char *getMimeVersion();
    virtual const char *getSupportedTypes() { return "text/vcard:3.0,text/x-vcard:2.1"; }
   
    virtual SyncItem *createItem( const string &uid, SyncState state );
    
    //
    // implementation of SyncSource
    //
    virtual ArrayElement *clone() { return new EvolutionContactSource(*this); }
    
  protected:
    //
    // implementation of EvolutionSyncSource callbacks
    //
    virtual void beginSyncThrow(bool needAll,
                                bool needPartial,
                                bool deleteLocal);
    virtual void endSyncThrow();
    virtual void setItemStatusThrow(const char *key, int status);
    virtual int addItemThrow(SyncItem& item);
    virtual int updateItemThrow(SyncItem& item);
    virtual int deleteItemThrow(SyncItem& item);
    virtual void logItem(const string &uid, const string &info, bool debug = false);
    virtual void logItem(SyncItem &item, const string &info, bool debug = false);

  private:
    /** valid after open(): the address book that this source references */
    eptr<EBook, GObject> m_addressbook;

    /** the format of vcards that new items are expected to have */
    const EVCardFormat m_vcardFormat;

    /**
     * a list of Evolution vcard properties which have to be encoded
     * as X-SYNCEVOLUTION-* when sending to server in 2.1 and decoded
     * back when receiving.
     */
    static const class extensions : public set<string> {
      public:
        extensions() : prefix("X-SYNCEVOLUTION-") {
            this->insert("FBURL");
            this->insert("CALURI");
        }

        const string prefix;
    } m_vcardExtensions;

    /**
     * a list of properties which SyncEvolution (in contrast to
     * the server) will only store once in each contact
     */
    static const class unique : public set<string> {
      public:
        unique () {
            insert("X-AIM");
            insert("X-GROUPWISE");
            insert("X-ICQ");
            insert("X-YAHOO");
            insert("X-EVOLUTION-ANNIVERSARY");
            insert("X-EVOLUTION-ASSISTANT");
            insert("X-EVOLUTION-BLOG-URL");
            insert("X-EVOLUTION-FILE-AS");
            insert("X-EVOLUTION-MANAGER");
            insert("X-EVOLUTION-SPOUSE");
            insert("X-EVOLUTION-VIDEO-URL");
            insert("X-MOZILLA-HTML");
            insert("FBURL");
            insert("CALURI");
        }
    } m_uniqueProperties;
};

#endif // ENABLE_EBOOK

#endif // INCL_EVOLUTIONCONTACTSOURCE
