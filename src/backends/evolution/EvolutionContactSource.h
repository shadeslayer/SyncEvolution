/*
 * Copyright (C) 2005-2008 Patrick Ohly
 */

#ifndef INCL_EVOLUTIONCONTACTSOURCE
#define INCL_EVOLUTIONCONTACTSOURCE

#include <config.h>
#include "EvolutionSyncSource.h"
#include "EvolutionSmartPtr.h"

#ifdef ENABLE_EBOOK

#include <set>

/**
 * Implements access to Evolution address books.
 */
class EvolutionContactSource : public EvolutionSyncSource
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
    // implementation of EvolutionSyncSource callbacks
    //
    virtual void beginSyncThrow(bool needAll,
                                bool needPartial,
                                bool deleteLocal);
    virtual void endSyncThrow();
    virtual SyncMLStatus addItemThrow(SyncItem& item);
    virtual SyncMLStatus updateItemThrow(SyncItem& item);
    virtual SyncMLStatus deleteItemThrow(SyncItem& item);
    virtual void logItem(const string &uid, const string &info, bool debug = false);
    virtual void logItem(const SyncItem &item, const string &info, bool debug = false);

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
