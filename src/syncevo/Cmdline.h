/*
 * Copyright (C) 2008-2009 Patrick Ohly <patrick.ohly@gmx.de>
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

#ifndef INCL_SYNC_EVOLUTION_CMDLINE
# define INCL_SYNC_EVOLUTION_CMDLINE

#include <syncevo/SyncConfig.h>
#include <syncevo/FilterConfigNode.h>
#include <syncevo/util.h>

#include <set>

#include <boost/shared_ptr.hpp>
#include <boost/scoped_array.hpp>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

class SyncSource;
class SyncSourceRaw;
class SyncContext;
class CmdlineTest;

/**
 * encodes a locally unique ID (LUID) in such a
 * way that it is treated as a plain word by shells
 */
class CmdlineLUID
{
    std::string m_encodedLUID;

public:
    /** fill with encoded LUID */
    void setEncoded(const std::string &encodedLUID) { m_encodedLUID = encodedLUID; }

    /** return encoded LUID as string */
    std::string getEncoded() const { return m_encodedLUID; }

    /** return original LUID */
    std::string toLUID() const { return toLUID(m_encodedLUID); }
    static std::string toLUID(const std::string &encoded) { return StringEscape::unescape(encoded, '%'); }

    /** fill with unencoded LUID */
    void setLUID(const std::string &luid) { m_encodedLUID = fromLUID(luid); }

    /** convert from unencoded LUID */
    static std::string fromLUID(const std::string &luid) { return StringEscape::escape(luid, '%', StringEscape::STRICT); }
};

class Cmdline {
public:
    Cmdline(int argc, const char * const *argv);
    Cmdline(const std::vector<std::string> &args);
    Cmdline(const char *arg, ...);
    virtual ~Cmdline() {}

    /**
     * parse the command line options
     *
     * @retval true if command line was okay
     */
    bool parse();

    /**
     * parse the command line options
     * relative paths in the arguments are converted to absolute paths
     * if it returns false, then the content of args is undefined.
     *
     * @retval true if command line was okay
     */
    bool parse(std::vector<std::string> &args);

    /**
     * @return false if run() still needs to be invoked, true when parse() already did
     *         the job (like --sync-property ?)
     */
    bool dontRun() const;

    bool run();

    /**
     * sync report as owned by this instance, not filled in unless
     * run() executed a sync
     */
    const SyncReport &getReport() const { return m_report; }

    /** the run() call modified configurations (added, updated, removed) */
    bool configWasModified() const { return m_configModified; }

    Bool useDaemon() { return m_useDaemon; }

    /** whether '--monitor' is set */
    bool monitor() { return m_monitor; }

    /** whether 'status' is set */
    bool status() { return m_status; }

    /* server name */
    std::string getConfigName() { return m_server; }

    /* check whether command line runs sync. It should be called after parsing. */
    bool isSync();

protected:
    // vector to store strings for arguments 
    std::vector<std::string> m_args;

    int m_argc;
    const char * const * m_argv;

    //array to store pointers of arguments
    boost::scoped_array<const char *> m_argvArray;

    /** result of sync, if one was executed */
    SyncReport m_report;

    Bool m_quiet;
    Bool m_dryrun;
    Bool m_status;
    Bool m_version;
    Bool m_usage;
    Bool m_configure;
    Bool m_remove;
    Bool m_run;
    Bool m_migrate;
    Bool m_printDatabases;
    Bool m_printServers;
    Bool m_printTemplates;
    Bool m_printConfig;
    Bool m_printSessions;
    Bool m_dontrun;
    Bool m_keyring;
    Bool m_monitor;
    Bool m_useDaemon;
    FullProps m_props;
    const ConfigPropertyRegistry &m_validSyncProps;
    const ConfigPropertyRegistry &m_validSourceProps;

    std::string m_restore;
    Bool m_before, m_after;

    Bool m_accessItems;
    std::string m_itemPath;
    std::string m_delimiter;
    std::list<std::string> m_luids;
    Bool m_printItems, m_update, m_import, m_export, m_deleteItems;

    std::string m_server;
    std::string m_template;
    std::set<std::string> m_sources;

    /** running the command line modified configuration settings (add, update, remove) */
    Bool m_configModified;

    /** compose description of cmd line option with optional parameter */
    static std::string cmdOpt(const char *opt, const char *param = NULL);

    /**
     * rename file or directory by appending .old or (if that already
     * exists) .old.x for x >= 1; updates config to point to the renamed directory
     */
    void makeObsolete(boost::shared_ptr<SyncConfig> &from);

    /**
     * Copy from one config into another, with filters
     * applied for the target. All sources are copied
     * if selectedSources is empty, otherwise only
     * those.
     */
    void copyConfig(const boost::shared_ptr<SyncConfig> &from,
                    const boost::shared_ptr<SyncConfig> &to,
                    const std::set<std::string> &selectedSources);

    /**
     * flush, move .synthesis dir, set ConsumerReady, ...
     */
    void finishCopy(const boost::shared_ptr<SyncConfig> &from,
                    const boost::shared_ptr<SyncContext> &to);

    /**
     * migrate peer config; target context must be ready
     */
    void migratePeer(const std::string &fromPeer, const std::string &toPeer);

    /**
     * parse sync or source property
     *
     * @param propertyType   sync, source, or unknown (in which case the property name must be given and must be unique)
     * @param opt            command line option as it appeard in argv (e.g. --sync|--sync-property|-z)
     * @param param          the parameter following the opt, may be NULL if none given (error!)
     * @param propname       if given, then this is the property name and param contains the param value (--sync <param>)
     */
    
    bool parseProp(PropertyType propertyType,
                   const char *opt,
                   const char *param,
                   const char *propname = NULL);

    bool listPropValues(const ConfigPropertyRegistry &validProps,
                        const std::string &propName,
                        const std::string &opt);

    bool listProperties(const ConfigPropertyRegistry &validProps,
                        const std::string &opt);

    /**
     * check that m_props don't contain
     * properties which only apply to peers, throw error
     * if found
     */
    void checkForPeerProps();

    /**
     * list all known data sources of a certain type
     */
    void listSources(SyncSource &syncSource, const std::string &header);

    void dumpConfigs(const std::string &preamble,
                     const SyncConfig::ConfigList &servers);

    void dumpConfigTemplates(const std::string &preamble,
                             const SyncConfig::TemplateList &templates,
                             bool printRank = false,
                             Logger::Level level = Logger::SHOW);

    enum DumpPropertiesFlags {
        DUMP_PROPS_NORMAL = 0,
        HIDE_LEGEND = 1<<0,       /**<
                                   * do not show the explanation which properties are shared,
                                   * used while dumping any source which is not the last one
                                   */
        HIDE_PER_PEER = 1<<1      /**<
                                   * config is for a context, not a peer, so do not show those
                                   * properties which are only per-peer
                                   */
    };
    void dumpProperties(const ConfigNode &configuredProps,
                        const ConfigPropertyRegistry &allProps,
                        int flags);

    void copyProperties(const ConfigNode &fromProps,
                        ConfigNode &toProps,
                        bool hidden,
                        const ConfigPropertyRegistry &allProps);

    void dumpComment(std::ostream &stream,
                     const std::string &prefix,
                     const std::string &comment);

    /** ensure that m_server was set, false if error message was necessary */
    bool needConfigName();

    /** print usage information */
    void usage(bool full,
               const std::string &error = std::string(""),
               const std::string &param = std::string(""));

    /**
     * This is a factory method used to delay sync client creation to its
     * subclass. The motivation is to let user implement their own 
     * clients to avoid dependency.
     * @return the created sync client
     */
    virtual SyncContext* createSyncClient();

    friend class CmdlineTest;

 private:
    /**
     * Utility function to check m_argv[opt] against a specific boolean
     * parameter of the form "<longName|shortName>[=yes/1/t/true/no/0/f/false].
     *
     * @param opt        current index in m_argv
     * @param longName   long form of the parameter, including --, may be NULL
     * @param shortName  short form, including -, may be NULL
     * @param  def       default value if m_argv[opt] contains no explicit value
     * @retval value     if and only if m_argv[opt] matches, then this is set to to true or false
     * @retval ok        true if parsing succeeded, false if not and error message was printed
     */
    bool parseBool(int opt, const char *longName, const char *shortName,
                   bool def, Bool &value,
                   bool &ok);

    /**
     * Fill list with all local IDs of the given source.
     * Unsafe characters are escaped with SafeConfigNode::escape(true,true).
     * startDataRead() must have been called.
     */
    void readLUIDs(SyncSource *source, std::list<std::string> &luids);

    /**
     * Add or update one item.
     * @param source     SyncSource in write mode (startWriteData must have been called)
     * @param luid       local ID, empty if item is to be added
     * @param data       the item data to insert
     * @return encoded luid of inserted item
     */
    CmdlineLUID insertItem(SyncSourceRaw *source, const std::string &luid, const std::string &data);
};


SE_END_CXX
#endif // INCL_SYNC_EVOLUTION_CMDLINE
