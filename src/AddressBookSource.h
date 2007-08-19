/*
 * Copyright (C) 2007 Patrick Ohly
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

#ifndef INCL_ADDRESSBOOKSOURCE
#define INCL_ADDRESSBOOKSOURCE

#include <config.h>
#include "EvolutionSyncSource.h"
#include "EvolutionSmartPtr.h"

#ifdef ENABLE_ADDRESSBOOK

#include <AddressBook/ABAddressBookC.h>

/**
 * a smart pointer implementation for objects for which
 * a unref() function exists; in contrast to eptr the
 * base type already is a pointer
 *
 * trying to store a NULL pointer raises an exception,
 * unreferencing valid objects is done automatically
 */
template<class T> class ref {
    /** do not allow copy construction */
    ref( const ref &other) {};

    /** do not allow copying */
    void operator = ( const ref &other ) {}

 protected:
    T m_pointer;
    
  public:
    /**
     * create a smart pointer that owns the given object;
     * passing a NULL pointer and a name for the object raises an error
     */
    ref(T pointer = NULL, const char *objectName = NULL) :
        m_pointer( pointer )
    {
        if (!pointer && objectName ) {
            throw std::runtime_error(std::string("Error allocating ") + objectName);
        }
    };
    ~ref()
    {
        set( NULL );
    }

    /**
     * store another object in this pointer, replacing any which was
     * referenced there before;
     * passing a NULL pointer and a name for the object raises an error
     */
    void set( T pointer, const char *objectName = NULL )
    {
        if (m_pointer) {
            CFRelease(m_pointer);
        }
        if (!pointer && objectName) {
            throw std::runtime_error(std::string("Error allocating ") + objectName);
        }
        m_pointer = pointer;
    }

    ref<T> &operator = ( T pointer ) { set( pointer ); return *this; }
    T operator-> () { return m_pointer; }
    T operator * () { return m_pointer; }
    operator T () { return m_pointer; }
    operator bool () { return m_pointer != NULL; }
};


/**
 * Implements access to Mac OS X address book.
 */
class AddressBookSource : public EvolutionSyncSource
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
    AddressBookSource(const string &name,
                      SyncSourceConfig *sc,
                      const string &changeId = string(""),
                      const string &id = string(""));
    AddressBookSource(const AddressBookSource &other);
    virtual ~AddressBookSource() { close(); }


    //
    // implementation of EvolutionSyncSource
    //
    virtual sources getSyncBackends();
    virtual void open();
    virtual void close(); 
    virtual void exportData(ostream &out);
    virtual string fileSuffix() { return "vcf"; }
    virtual const char *getMimeType() { return "text/x-vcard"; }
    virtual const char *getMimeVersion() { return "2.1"; }
    virtual const char *getSupportedTypes() { return "text/x-vcard:2.1"; }
   
    virtual SyncItem *createItem( const string &uid, SyncState state );
    
    //
    // implementation of SyncSource
    //
    virtual ArrayElement *clone() { return new AddressBookSource(*this); }
    
  protected:
    //
    // implementation of EvolutionSyncSource callbacks
    //
    virtual void beginSyncThrow(bool needAll,
                                bool needPartial,
                                bool deleteLocal);
    virtual void endSyncThrow();
    virtual int addItemThrow(SyncItem& item);
    virtual int updateItemThrow(SyncItem& item);
    virtual int deleteItemThrow(SyncItem& item);
    virtual void logItem(const string &uid, const string &info, bool debug = false);
    virtual void logItem(SyncItem &item, const string &info, bool debug = false);

  private:
    /** valid after open(): the address book that this source references */
    ref<ABAddressBookRef> m_addressbook;
};

#endif // ENABLE_EBOOK

#endif // INCL_ADDRESSBOOKSOURCE
