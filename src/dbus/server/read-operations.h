/*
 * Copyright (C) 2011 Intel Corporation
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

#ifndef READ_OPERATIONS_H
#define READ_OPERATIONS_H

#include <syncevo/SyncSource.h>
#include <syncevo/SmartPtr.h>

#include "gdbus-cxx-bridge.h"

SE_BEGIN_CXX

class Server;

/**
 * Implements the read-only methods in a Session and the Server.
 * Only data is the server configuration name, everything else
 * is created and destroyed inside the methods.
 */
class ReadOperations
{
public:
    const std::string m_configName;

    Server &m_server;

    ReadOperations(const std::string &config_name, Server &server);

    /** the double dictionary used to represent configurations */
    typedef std::map< std::string, StringMap > Config_t;

    /** the array of reports filled by getReports() */
    typedef std::vector< StringMap > Reports_t;

    /** the array of databases used by getDatabases() */
    typedef SyncSource::Database SourceDatabase;
    typedef SyncSource::Databases SourceDatabases_t;

    /** implementation of D-Bus GetConfigs() */
    void getConfigs(bool getTemplates, std::vector<std::string> &configNames);

    /** implementation of D-Bus GetConfig() for m_configName as server configuration */
    void getConfig(bool getTemplate,
                   Config_t &config);

    /** implementation of D-Bus GetNamedConfig() for configuration named in parameter */
    void getNamedConfig(const std::string &configName,
                        bool getTemplate,
                        Config_t &config);

    /** implementation of D-Bus GetReports() for m_configName as server configuration */
    void getReports(uint32_t start, uint32_t count,
                    Reports_t &reports);

    /** Session.CheckSource() */
    void checkSource(const string &sourceName);

    /** Session.GetDatabases() */
    void getDatabases(const string &sourceName, SourceDatabases_t &databases);

private:
    /**
     * This virtual function is used to let subclass set
     * filters to config. Only used internally.
     * Return true if filters exists and have been set.
     * Otherwise, nothing is set to config
     */
    virtual bool setFilters(SyncConfig &config) { return false; }

    /**
     * utility method which constructs a SyncConfig which references a local configuration (never a template)
     *
     * In general, the config must exist, except in two cases:
     * - configName = @default (considered always available)
     * - mustExist = false (used when reading a templates for a context which might not exist yet)
     */
    boost::shared_ptr<SyncConfig> getLocalConfig(const std::string &configName, bool mustExist = true);
};

SE_END_CXX

namespace GDBusCXX {
using namespace SyncEvo;
/**
 * dbus_traits for SourceDatabase. Put it here for
 * avoiding polluting gxx-dbus-bridge.h
 */
template<> struct dbus_traits<ReadOperations::SourceDatabase> :
    public dbus_struct_traits<ReadOperations::SourceDatabase,
                              dbus_member<ReadOperations::SourceDatabase, std::string, &ReadOperations::SourceDatabase::m_name,
                              dbus_member<ReadOperations::SourceDatabase, std::string, &ReadOperations::SourceDatabase::m_uri,
                              dbus_member_single<ReadOperations::SourceDatabase, bool, &ReadOperations::SourceDatabase::m_isDefault> > > >{};
}

#endif // READ_OPERATIONS_H
