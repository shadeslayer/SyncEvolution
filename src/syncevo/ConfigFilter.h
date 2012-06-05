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
#ifndef INCL_SYNC_EVOLUTION_CONFIG_FILTER
# define INCL_SYNC_EVOLUTION_CONFIG_FILTER

#include <syncevo/util.h>

#include <string>
#include <map>
#include <set>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

/** a case-insensitive string to InitStateString mapping */
class ConfigProps : public std::map<std::string, InitStateString, Nocase<std::string> > {
 public:
    /** format as <key> = <value> lines */
    operator std::string () const;

    /**
     * Add all entries from the second set of properties,
     * overwriting existing ones (in contrast to map::insert(),
     * which does not overwrite).
     */
    void add(const ConfigProps &other);

    /**
     * Return value in map or the given default, marked as unset.
     */
    InitStateString get(const std::string &key, const std::string &def = "") const;
};

/**
 * Properties for different sources.
 *
 * Source and property names are case-insensitive.
 */
class SourceProps : public std::map<std::string, ConfigProps, Nocase<std::string> >
{
 public:
    /**
     * Combine per-source property filters with filter for
     * all sources: per-source filter values always win.
     */
    ConfigProps createSourceFilter(const std::string &source) const;
};

/**
 * A pair of sync and source properties. Source properties are 
 * reached via "" for "all sources", and "<source name>" for a
 * specific source.
 */
struct ContextProps
{
    ConfigProps m_syncProps;
    SourceProps m_sourceProps;

};

/**
 * A collection of sync and source settings, including different contexts.
 *
 * Primary index is by configuration:
 * "" for unset, "@<context>" for explicit context, "foo@bar" for peer config
 *
 * Index is case-insensitive.
 */
class FullProps : public std::map<std::string, ContextProps, Nocase<std::string> >
{
 public:
    enum PropCheckMode {
        CHECK_ALL,
        IGNORE_GLOBAL_PROPS
    };

    /** any of the contained ConfigProps has entries */
    bool hasProperties(PropCheckMode mode) const;

    /**
     * Combines sync properties into one filter, giving "config"
     * priority over "context of config" and over "no specific context".
     * Contexts which do not apply to the config are silently ignored.
     * Error checking for invalid contexts in the FullProps instance
     * must be done separately.
     *
     * @param config    empty string (unknown config) or valid peer or context name
     */ 
    ConfigProps createSyncFilter(const std::string &config) const;

    /**
     * Combines source properties into one filter. Same priority rules
     * as for sync properties apply. Priorities inside each context
     * are resolved via SourceProps::createSourceFilter(). The context
     * is checked first, so "sync@foo@default" overrides "addressbook/sync".
     *
     * @param config    valid peer or context name
     * @param source    empty string (only pick properties applying to all sources) or source name
     */
    ConfigProps createSourceFilter(const std::string &config,
                                   const std::string &source) const;

    /**
     * read properties from context, then update with command line
     * properties for a) that context and b) the given config
     *
     * @param context         context name, including @ sign, empty if not needed
     * @param config          possibly non-normalized configuration name which determines
     *                        additional filters, can be empty
     * @param sources         additional sources for which sourceFilters need to be set
     * @retval syncFilter     global sync properties
     * @retval sourceFilters  entries for sources known in either context, config, or
     *                        listed explicitly,
     *                        key "" as fallback for unknown sources
     */
    void createFilters(const std::string &context,
                       const std::string &config,
                       const std::set<std::string> *sources,
                       ConfigProps &syncFilter,
                       SourceProps &sourceFilters);
};

SE_END_CXX
#endif
