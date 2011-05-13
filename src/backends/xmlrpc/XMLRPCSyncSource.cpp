/*
 * Copyright (C) 2009 m-otion communications GmbH <knipp@m-otion.com>, waived
 * Copyright (C) 2007-2009 Patrick Ohly <patrick.ohly@gmx.de>
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

#include "config.h"

#ifdef ENABLE_XMLRPC

#include "XMLRPCSyncSource.h"

#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

XMLRPCSyncSource::XMLRPCSyncSource(const SyncSourceParams &params,
                                   const string &dataformat) :
    TrackingSyncSource(params)
{
    if (dataformat.empty()) {
        throwError("a data format must be specified");
    }
    size_t sep = dataformat.find(':');
    if (sep == dataformat.npos) {
        throwError(string("data format not specified as <mime type>:<mime version>: " + dataformat));
    }
    m_mimeType.assign(dataformat, 0, sep);
    m_mimeVersion = dataformat.substr(sep + 1);
    m_supportedTypes = dataformat;

    string const dbid = getDatabaseID();
    boost::split(m_splitDatabase, dbid, boost::is_from_range('|', '|'));
    m_serverUrl = m_splitDatabase[0];
}

std::string XMLRPCSyncSource::getMimeType() const
{
    return m_mimeType.c_str();
}

std::string XMLRPCSyncSource::getMimeVersion() const
{
    return m_mimeVersion.c_str();
}

void XMLRPCSyncSource::open()
{
}

bool XMLRPCSyncSource::isEmpty()
{
    // TODO: provide a real implementation. Always returning false
    // here disables the "allow slow sync when no local data" heuristic
    // for preventSlowSync=1.
    return false;
}

void XMLRPCSyncSource::close()
{
}

XMLRPCSyncSource::Databases XMLRPCSyncSource::getDatabases()
{
    Databases result;

    result.push_back(Database("select database via URL",
                              "<serverUrl>"));
    return result;
}

void XMLRPCSyncSource::listAllItems(RevisionMap_t &revisions)
{

    xmlrpc_c::value result;

    client.call(m_serverUrl, "listAllItems", prepareParamList(), &result);

    if(result.type() == xmlrpc_c::value::TYPE_STRUCT) {
        xmlrpc_c::value_struct const tmp(result);
        map<string, xmlrpc_c::value> const resultMap(
            static_cast<map<string, xmlrpc_c::value> >(tmp));
        map<string, xmlrpc_c::value>::const_iterator it;

        for(it = resultMap.begin(); it != resultMap.end(); it++)
            revisions[(*it).first] = xmlrpc_c::value_string((*it).second);

    }
}

void XMLRPCSyncSource::readItem(const string &uid, std::string &item, bool raw)
{

    xmlrpc_c::paramList p = prepareParamList();
    p.add(xmlrpc_c::value_string(uid));
    xmlrpc_c::value result;

    client.call(m_serverUrl, "readItem", p, &result);

    item = xmlrpc_c::value_string(result);

}

TrackingSyncSource::InsertItemResult XMLRPCSyncSource::insertItem(const string &uid, const std::string &item, bool raw)
{

    xmlrpc_c::paramList p = prepareParamList();
    p.add(xmlrpc_c::value_string(uid));
    p.add(xmlrpc_c::value_string(item));
    xmlrpc_c::value result;

    client.call(m_serverUrl, "insertItem", p, &result);

    xmlrpc_c::value_struct const tmp(result);
    map<string, xmlrpc_c::value> const resultMap(
        static_cast<map<string, xmlrpc_c::value> >(tmp));

    if(resultMap.size() != 1) {
        throw("Return value of insertItem has wrong length.");
    }

    map<string, xmlrpc_c::value>::const_iterator it;
    it = resultMap.begin();

    return InsertItemResult((*it).first,
                            xmlrpc_c::value_string((*it).second),
                            false);
}


void XMLRPCSyncSource::removeItem(const string &uid)
{
    xmlrpc_c::paramList p = prepareParamList();
    p.add(xmlrpc_c::value_string(uid));
    xmlrpc_c::value result;

    client.call(m_serverUrl, "removeItem", p, &result);
}

xmlrpc_c::paramList XMLRPCSyncSource::prepareParamList()
{
    xmlrpc_c::paramList p;
    for(size_t i = 1; i < m_splitDatabase.size(); i++) {
        p.add(xmlrpc_c::value_string(m_splitDatabase[i]));
    }

    return p;
}

SE_END_CXX

#endif /* ENABLE_XMLRPC */

#ifdef ENABLE_MODULES
# include "XMLRPCSyncSourceRegister.cpp"
#endif
