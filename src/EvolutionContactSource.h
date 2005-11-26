/*
 * Copyright (C) 2005 Patrick Ohly
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

#include "EvolutionSyncSource.h"
#include "EvolutionSmartPtr.h"

#include <libebook/e-book.h>
#include <libebook/e-vcard.h>

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
                            const string &changeId = string(""),
                            const string &id = string(""),
                            EVCardFormat vcardFormat = EVC_FORMAT_VCARD_30 );
    virtual ~EvolutionContactSource();

    //
    // implementation of EvolutionSyncSource
    //
    virtual sources getSyncBackends();
    virtual void open();
    virtual void close();
    virtual SyncItem *createItem( const string &uid, SyncState state );

    //
    // implementation of SyncSource
    //
    virtual int addItem(SyncItem& item);
    virtual int updateItem(SyncItem& item);
    virtual int deleteItem(SyncItem& item);
    virtual int beginSync();
    virtual int endSync();
	 
  private:
    /** valid after open(): the address book that this source references */
    gptr<EBook, GObject> m_addressbook;

    /** the format of vcards that new items are expected to have */
    const EVCardFormat m_vcardFormat;

    /** the mime type which corresponds to m_vcardFormat */
    const char *getMimeType();

    /** internal implementation of endSync() which will throw an exception in case of failure */
    void endSyncThrow();
};
