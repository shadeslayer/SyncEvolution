/*
 * Copyright (C) 2005-2009 Patrick Ohly <patrick.ohly@gmx.de>
 * Copyright (C) 2009 Intel Corporation
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

#ifndef INCL_EVOLUTIONCONTACTSOURCE
#define INCL_EVOLUTIONCONTACTSOURCE

#include <config.h>
#include "TrackingSyncSource.h"
#include "EvolutionSmartPtr.h"

#ifdef ENABLE_EBOOK

#include <set>

/**
 * Implements access to Evolution address books.
 */
class EvolutionContactSource : public TrackingSyncSource
{
  public:
    EvolutionContactSource(const EvolutionSyncSourceParams &params,
                           EVCardFormat vcardFormat = EVC_FORMAT_VCARD_30);
    EvolutionContactSource(const EvolutionContactSource &other);
    virtual ~EvolutionContactSource() { close(); }

    // utility function: extract vcard from item in format suitable for Evolution
    string preparseVCard(SyncItem& item);

    //
    // implementation of EvolutionSyncSource
    //
    virtual Databases getDatabases();
    virtual void open();
    virtual void close();
    virtual void exportData(ostream &out);
    virtual string fileSuffix() const { return "vcf"; }
    virtual const char *getMimeType() const;
    virtual const char *getMimeVersion() const;
    virtual const char *getSupportedTypes() const { return "text/vcard:3.0,text/x-vcard:2.1"; }
   
    virtual SyncItem *createItem(const string &uid);
    
  protected:
    //
    // implementation of TrackingSyncSource callbacks
    //
    virtual void listAllItems(RevisionMap_t &revisions);
    virtual InsertItemResult insertItem(const string &uid, const SyncItem &item);
    virtual void deleteItem(const string &uid);
    virtual void logItem(const string &uid, const string &info, bool debug = false);
    virtual void logItem(const SyncItem &item, const string &info, bool debug = false);

    // need to override native format: it is always vCard 3.0
    void getSynthesisInfo(string &profile,
                          string &datatypes,
                          string &native)
    {
        TrackingSyncSource::getSynthesisInfo(profile, datatypes, native);
        profile = "\"vCard\", 2";
        native = "vCard30";
    }

  private:
    /** extract REV string for contact, throw error if not found */
    std::string getRevision(const std::string &uid);

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
