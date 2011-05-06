/*
 * Copyright (C) 2008-2009 Patrick Ohly <patrick.ohly@gmx.de>
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

#include <syncevo/ConfigFilter.h>
#include <syncevo/SyncConfig.h>

SE_BEGIN_CXX

void ConfigProps::add(const ConfigProps &other)
{
    BOOST_FOREACH(const StringPair &entry, other) {
        std::pair<iterator, bool> res = insert(entry);
        if (!res.second) {
            res.first->second = entry.second;
        }
    }
}

string ConfigProps::get(const string &key, const string &def) const
{
    const_iterator it = find(key);
    if (it == end()) {
        return def;
    } else {
        return it->second;
    }
}

ConfigProps SourceProps::createSourceFilter(const std::string &source) const
{
    const_iterator it = find("");
    ConfigProps filter;
    if (it != end()) {
        filter = it->second;
    }
    if (!source.empty()) {
        it = find(source);
        if (it != end()) {
            filter.add(it->second);
        }
    }
    return filter;
}

ConfigProps FullProps::createSyncFilter(const std::string &config) const
{
    const_iterator it = find("");
    ConfigProps filter;
    if (it != end()) {
        // first unset context
        filter = it->second.m_syncProps;
    }

    if (!config.empty()) {
        std::string normal = SyncConfig::normalizeConfigString(config, SyncConfig::NORMALIZE_LONG_FORMAT);
        std::string peer, context;
        SyncConfig::splitConfigString(normal, peer, context);
        // then overwrite with context config
        it = find(std::string("@") + context);
        if (it != end()) {
            filter.add(it->second.m_syncProps);
        }
        // finally peer config, if we have one
        if (!peer.empty()) {
            it = find(normal);
            if (it != end()) {
                filter.add(it->second.m_syncProps);
            }
        }
    }
    return filter;
}

ConfigProps FullProps::createSourceFilter(const std::string &config,
                                          const std::string &source) const
{
    const_iterator it = find("");
    ConfigProps filter;
    if (it != end()) {
        // first unset context
        filter = it->second.m_sourceProps.createSourceFilter(source);
    }

    if (!config.empty()) {
        std::string normal = SyncConfig::normalizeConfigString(config, SyncConfig::NORMALIZE_LONG_FORMAT);
        std::string peer, context;
        SyncConfig::splitConfigString(normal, peer, context);
        // then overwrite with context config
        it = find(std::string("@") + context);
        if (it != end()) {
            filter.add(it->second.m_sourceProps.createSourceFilter(source));
        }
        // finally peer config, if we have one
        if (!peer.empty()) {
            it = find(normal);
            if (it != end()) {
                filter.add(it->second.m_sourceProps.createSourceFilter(source));
            }
        }
    }
    return filter;
}

bool FullProps::hasProperties() const
{
    BOOST_FOREACH(const value_type &context, *this) {
        if (!context.second.m_syncProps.empty()) {
            return true;
        }
        BOOST_FOREACH(const SourceProps::value_type &source, context.second.m_sourceProps) {
            if (!source.second.empty()) {
                return true;
            }
        }
    }

    return false;
}

void FullProps::createFilters(const string &context,
                              const string &config,
                              const set<string> *sources,
                              ConfigProps &syncFilter,
                              SourceProps &sourceFilters)
{
    boost::shared_ptr<SyncConfig> shared;

    if (!context.empty()) {
        // Read from context. If it does not exist, we simply set no properties
        // as filter. Previously there was a check for existance, but that was
        // flawed because it ignored the global property "defaultPeer".
        shared.reset(new SyncConfig(context));
        shared->getProperties()->readProperties(syncFilter);
    }

    // add command line filters for context or config?
    if (!context.empty()) {
        syncFilter.add(createSyncFilter(context));
        // default for (so far) unknown sources which might be created
        sourceFilters[""].add(createSourceFilter(context, ""));
    }
    if (!config.empty()) {
        syncFilter.add(createSyncFilter(config));
        sourceFilters[""].add(createSourceFilter(config, ""));
    }

    // build full set of all sources
    set<string> allSources;
    if (sources) {
        allSources = *sources;
    }
    if (shared) {
        std::list<std::string> tmp = shared->getSyncSources();
        allSources.insert(tmp.begin(), tmp.end());
    }
    if (!config.empty()) {
        std::list<std::string> tmp = SyncConfig(config).getSyncSources();
        allSources.insert(tmp.begin(), tmp.end());
    }

    // explicit filter for all known sources
    BOOST_FOREACH(std::string source, allSources) {
        ConfigProps &props = sourceFilters[source];
        if (shared) {
            // combine existing properties from context and command line
            // filter
            SyncSourceNodes nodes = shared->getSyncSourceNodes(source, "");
            nodes.getProperties()->readProperties(props);

            // Special case "type" property: the value in the context
            // is not preserved. Every new peer must ensure that
            // its own value is compatible (= same backend) with
            // the other peers.
            props.erase("type");

            props.add(createSourceFilter(context, source));
        }
        if (!config.empty()) {
            props.add(createSourceFilter(config, source));
        }
    }
}



SE_END_CXX
