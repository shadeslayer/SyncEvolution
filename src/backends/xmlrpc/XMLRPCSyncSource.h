/*
 * Copyright (C) 2009 m-otion communications GmbH <knipp@m-otion.com>, waived
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

#ifndef INCL_XMLRPCSYNCSOURCE
#define INCL_XMLRPCSYNCSOURCE

#include <syncevo/TrackingSyncSource.h>

#ifdef ENABLE_XMLRPC

#include <xmlrpc-c/base.hpp>
#include <xmlrpc-c/client_simple.hpp>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

class XMLRPCSyncSource : public TrackingSyncSource
{
  public:
    XMLRPCSyncSource(const SyncSourceParams &params,
                     const string &dataformat);


 protected:
    /* implementation of SyncSource interface */
    virtual void open();
    virtual bool isEmpty();
    virtual void close();
    virtual Databases getDatabases();
    virtual std::string getMimeType() const;
    virtual std::string getMimeVersion() const;

    /* implementation of TrackingSyncSource interface */
    virtual void listAllItems(RevisionMap_t &revisions);
    virtual InsertItemResult insertItem(const string &luid, const std::string &item, bool raw);
    void readItem(const std::string &luid, std::string &item, bool raw);
    virtual void removeItem(const string &uid);

 private:
    /**
     * @name values obtained from the source's type property
     *
     * Other sync sources only support one hard-coded type and
     * don't need such variables.
     */
    /**@{*/
    string m_mimeType;
    string m_mimeVersion;
    string m_supportedTypes;
    /**@}*/

    /** @name values obtained from the database name */
    string m_serverUrl;
    vector <string> m_splitDatabase;

    xmlrpc_c::paramList prepareParamList();
    xmlrpc_c::clientSimple client;

};

SE_END_CXX

#endif // ENABLE_XMLRPC
#endif // INCL_XMLRPCSYNCSOURCE
