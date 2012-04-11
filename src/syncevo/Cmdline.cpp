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

#include <syncevo/Cmdline.h>
#include <syncevo/FilterConfigNode.h>
#include <syncevo/VolatileConfigNode.h>
#include <syncevo/IniConfigNode.h>
#include <syncevo/SyncSource.h>
#include <syncevo/SyncContext.h>
#include <syncevo/util.h>
#include "test.h"

#include <synthesis/SDK_util.h>

#include <unistd.h>
#include <errno.h>

#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <memory>
#include <set>
#include <list>
#include <algorithm>
using namespace std;

#include <boost/shared_ptr.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/tokenizer.hpp>
#include <boost/foreach.hpp>
#include <boost/range.hpp>
#include <fstream>

#include <syncevo/declarations.h>
using namespace std;
SE_BEGIN_CXX

// synopsis and options char strings
#include "CmdlineHelp.c"

Cmdline::Cmdline(int argc, const char * const * argv) :
    m_argc(argc),
    m_argv(argv),
    m_validSyncProps(SyncConfig::getRegistry()),
    m_validSourceProps(SyncSourceConfig::getRegistry())
{}

Cmdline::Cmdline(const vector<string> &args) :
    m_args(args),
    m_validSyncProps(SyncConfig::getRegistry()),
    m_validSourceProps(SyncSourceConfig::getRegistry())
{
    m_argc = args.size();
    m_argvArray.reset(new const char *[args.size()]);
    for(int i = 0; i < m_argc; i++) {
        m_argvArray[i] = m_args[i].c_str();
    }
    m_argv = m_argvArray.get();
}

Cmdline::Cmdline(const char *arg, ...) :
    m_validSyncProps(SyncConfig::getRegistry()),
    m_validSourceProps(SyncSourceConfig::getRegistry())
{
    va_list argList;
    va_start(argList, arg);
    for (const char *curr = arg;
         curr;
         curr = va_arg(argList, const char *)) {
        m_args.push_back(curr);
    }
    va_end(argList);
    m_argc = m_args.size();
    m_argvArray.reset(new const char *[m_args.size()]);
    for (int i = 0; i < m_argc; i++) {
        m_argvArray[i] = m_args[i].c_str();
    }
    m_argv = m_argvArray.get();
}

bool Cmdline::parse()
{
    vector<string> parsed;
    return parse(parsed);
}

bool Cmdline::parse(vector<string> &parsed)
{
    parsed.clear();
    if (m_argc) {
        parsed.push_back(m_argv[0]);
    }
    m_delimiter = "\n\n";

    // All command line options which ask for a specific operation,
    // like --restore, --print-config, ... Used to detect conflicting
    // operations.
    vector<string> operations;

    int opt = 1;
    bool ok;
    while (opt < m_argc) {
        parsed.push_back(m_argv[opt]);
        if (boost::iequals(m_argv[opt], "--")) {
            // separator between options and <config> <source>:
            // swallow it and leave option parsing
            opt++;
            break;
        }
        if (m_argv[opt][0] != '-') {
            if (strchr(m_argv[opt], '=')) {
                // property assignment
                if (!parseProp(UNKNOWN_PROPERTY_TYPE,
                               NULL,
                               m_argv[opt],
                               NULL)) {
                    return false;
                } else {
                    opt++;
                    continue;
                }
            } else {
                break;
            }
        }
        if (boost::iequals(m_argv[opt], "--sync") ||
            boost::iequals(m_argv[opt], "-s")) {
            opt++;
            string param;
            string cmdopt(m_argv[opt - 1]);
            if (!parseProp(SOURCE_PROPERTY_TYPE,
                           m_argv[opt - 1], opt == m_argc ? NULL : m_argv[opt],
                           "sync")) {
                return false;
            }
            parsed.push_back(m_argv[opt]);

            // disable requirement to add --run explicitly in order to
            // be compatible with traditional command lines
            m_run = true;
        } else if(boost::iequals(m_argv[opt], "--sync-property") ||
                  boost::iequals(m_argv[opt], "-y")) {
                opt++;
                if (!parseProp(SYNC_PROPERTY_TYPE,
                               m_argv[opt - 1], opt == m_argc ? NULL : m_argv[opt])) {
                    return false;
                }
                parsed.push_back(m_argv[opt]);
        } else if(boost::iequals(m_argv[opt], "--source-property") ||
                  boost::iequals(m_argv[opt], "-z")) {
            opt++;
            if (!parseProp(SOURCE_PROPERTY_TYPE,
                           m_argv[opt - 1], opt == m_argc ? NULL : m_argv[opt])) {
                return false;
            }
            parsed.push_back(m_argv[opt]);
        }else if(boost::iequals(m_argv[opt], "--template") ||
                  boost::iequals(m_argv[opt], "-l")) {
            opt++;
            if (opt >= m_argc) {
                usage(false, string("missing parameter for ") + cmdOpt(m_argv[opt - 1]));
                return false;
            }
            parsed.push_back(m_argv[opt]);
            m_template = m_argv[opt];
            m_configure = true;
            string temp = boost::trim_copy (m_template);
            if (temp.find ("?") == 0){
                m_printTemplates = true;
                m_dontrun = true;
                m_template = temp.substr (1);
            }
        } else if(boost::iequals(m_argv[opt], "--print-databases")) {
            operations.push_back(m_argv[opt]);
            m_printDatabases = true;
        } else if(boost::iequals(m_argv[opt], "--print-servers") ||
                  boost::iequals(m_argv[opt], "--print-peers") ||
                  boost::iequals(m_argv[opt], "--print-configs")) {
            operations.push_back(m_argv[opt]);
            m_printServers = true;
        } else if(boost::iequals(m_argv[opt], "--print-config") ||
                  boost::iequals(m_argv[opt], "-p")) {
            operations.push_back(m_argv[opt]);
            m_printConfig = true;
        } else if(boost::iequals(m_argv[opt], "--print-sessions")) {
            operations.push_back(m_argv[opt]);
            m_printSessions = true;
        } else if(boost::iequals(m_argv[opt], "--configure") ||
                  boost::iequals(m_argv[opt], "-c")) {
            operations.push_back(m_argv[opt]);
            m_configure = true;
        } else if(boost::iequals(m_argv[opt], "--remove")) {
            operations.push_back(m_argv[opt]);
            m_remove = true;
        } else if(boost::iequals(m_argv[opt], "--run") ||
                  boost::iequals(m_argv[opt], "-r")) {
            operations.push_back(m_argv[opt]);
            m_run = true;
        } else if(boost::iequals(m_argv[opt], "--restore")) {
            operations.push_back(m_argv[opt]);
            opt++;
            if (opt >= m_argc) {
                usage(false, string("missing parameter for ") + cmdOpt(m_argv[opt - 1]));
                return false;
            }
            m_restore = m_argv[opt];
            if (m_restore.empty()) {
                usage(false, string("missing parameter for ") + cmdOpt(m_argv[opt - 1]));
                return false;
            }
            //if can't convert it successfully, it's an invalid path
            if (!relToAbs(m_restore)) {
                usage(false, string("parameter '") + m_restore + "' for " + cmdOpt(m_argv[opt - 1]) + " must be log directory");
                return false;
            }
            parsed.push_back(m_restore);
        } else if(boost::iequals(m_argv[opt], "--before")) {
            m_before = true;
        } else if(boost::iequals(m_argv[opt], "--after")) {
            m_after = true;
        } else if (boost::iequals(m_argv[opt], "--print-items")) {
            operations.push_back(m_argv[opt]);
            m_printItems = m_accessItems = true;
        } else if ((boost::iequals(m_argv[opt], "--export") && (m_export = true)) ||
                   (boost::iequals(m_argv[opt], "--import") && (m_import = true)) ||
                   (boost::iequals(m_argv[opt], "--update") && (m_update = true))) {
            operations.push_back(m_argv[opt]);
            m_accessItems = true;
            opt++;
            if (opt >= m_argc || !m_argv[opt][0]) {
                usage(false, string("missing parameter for ") + cmdOpt(m_argv[opt - 1]));
                return false;
            }
            m_itemPath = m_argv[opt];
            if (m_itemPath != "-") {
                string dir, file;
                splitPath(m_itemPath, dir, file);
                if (dir.empty()) {
                    dir = ".";
                }
                if (!relToAbs(dir)) {
                    SyncContext::throwError(dir, errno);
                }
                m_itemPath = dir + "/" + file;
            }
            parsed.push_back(m_itemPath);
        } else if (boost::iequals(m_argv[opt], "--delimiter")) {
            opt++;
            if (opt >= m_argc) {
                usage(false, string("missing parameter for ") + cmdOpt(m_argv[opt - 1]));
                return false;
            }
            m_delimiter = m_argv[opt];
            parsed.push_back(m_delimiter);
        } else if (boost::iequals(m_argv[opt], "--delete-items")) {
            operations.push_back(m_argv[opt]);
            m_deleteItems = m_accessItems = true;
        } else if(boost::iequals(m_argv[opt], "--dry-run")) {
            m_dryrun = true;
        } else if(boost::iequals(m_argv[opt], "--migrate")) {
            operations.push_back(m_argv[opt]);
            m_migrate = true;
        } else if(boost::iequals(m_argv[opt], "--status") ||
                  boost::iequals(m_argv[opt], "-t")) {
            operations.push_back(m_argv[opt]);
            m_status = true;
        } else if(boost::iequals(m_argv[opt], "--quiet") ||
                  boost::iequals(m_argv[opt], "-q")) {
            m_quiet = true;
        } else if(boost::iequals(m_argv[opt], "--help") ||
                  boost::iequals(m_argv[opt], "-h")) {
            m_usage = true;
        } else if(boost::iequals(m_argv[opt], "--version")) {
            operations.push_back(m_argv[opt]);
            m_version = true;
        } else if (parseBool(opt, "--keyring", "-k", true, m_keyring, ok)) {
            if (!ok) {
                return false;
            }
        } else if (parseBool(opt, "--daemon", NULL, true, m_useDaemon, ok)) {
            if (!ok) {
                return false;
            }
        } else if(boost::iequals(m_argv[opt], "--monitor")||
                boost::iequals(m_argv[opt], "-m")) {
            operations.push_back(m_argv[opt]);
            m_monitor = true;
        } else if (boost::iequals(m_argv[opt], "--luids")) {
            // all following parameters are luids; can't be combined
            // with setting config and source name
            while (++opt < m_argc) {
                m_luids.push_back(CmdlineLUID::toLUID(m_argv[opt]));
            }
        } else {
            usage(false, string(m_argv[opt]) + ": unknown parameter");
            return false;
        }
        opt++;
    }

    if (opt < m_argc) {
        m_server = m_argv[opt++];
        while (opt < m_argc) {
            parsed.push_back(m_argv[opt]);
            if (m_sources.empty() ||
                !m_accessItems) {
                m_sources.insert(m_argv[opt++]);
            } else {
                // first additional parameter was source, rest are luids
                m_luids.push_back(CmdlineLUID::toLUID(m_argv[opt++]));
            }
        }
    }

    // check whether we have conflicting operations requested by user
    if (operations.size() > 1) {
        usage(false, boost::join(operations, " ") + ": mutually exclusive operations");
        return false;
    }

    // common sanity checking for item listing/import/export/update
    if (m_accessItems) {
        if ((m_import || m_update) && m_dryrun) {
            usage(false, operations[0] + ": --dry-run not supported");
            return false;
        }
    }

    return true;
}

bool Cmdline::parseBool(int opt, const char *longName, const char *shortName,
                        bool def, Bool &value,
                        bool &ok)
{
    string option = m_argv[opt];
    string param;
    size_t pos = option.find('=');
    if (pos != option.npos) {
        param = option.substr(pos + 1);
        option.resize(pos);
    }
    if ((longName && boost::iequals(option, longName)) ||
        (shortName && boost::iequals(option, shortName))) {
        ok = true;
        if (param.empty()) {
            value = def;
        } else if (boost::iequals(param, "t") ||
                   boost::iequals(param, "1") ||
                   boost::iequals(param, "true") ||
                   boost::iequals(param, "yes")) {
            value = true;
        } else if (boost::iequals(param, "f") ||
              boost::iequals(param, "0") ||
              boost::iequals(param, "false") ||
              boost::iequals(param, "no")) {
            value = false;
        } else {
            usage(false, string("parameter in '") + m_argv[opt] + "' must be 1/t/true/yes or 0/f/false/no");
            ok = false;
        }
        // was our option
        return true;
    } else {
        // keep searching for match
        return false;
    }
}

bool Cmdline::isSync()
{
    // make sure command line arguments really try to run sync
    if (m_usage || m_version ||
        m_printServers || boost::trim_copy(m_server) == "?" ||
        m_printTemplates || m_dontrun ||
        m_argc == 1 || (m_useDaemon.wasSet() && m_argc == 2) ||
        m_printDatabases ||
        m_printConfig || m_remove ||
        (m_server == "" && m_argc > 1) ||
        m_configure || m_migrate ||
        m_status || m_printSessions ||
        !m_restore.empty() ||
        m_accessItems ||
        m_dryrun ||
        (!m_run && m_props.hasProperties())) {
        return false;
    } else {
        return true;
    }
}

bool Cmdline::dontRun() const
{
    // this mimics the if() checks in run()
    if (m_usage || m_version ||
        m_printServers || boost::trim_copy(m_server) == "?" ||
        m_printTemplates) {
        return false;
    } else {
        return m_dontrun;
    }
}

void Cmdline::makeObsolete(boost::shared_ptr<SyncConfig> &from)
{
    string oldname = from->getRootPath();
    string newname, suffix;
    for (int counter = 0; true; counter++) {
        ostringstream newsuffix;
        newsuffix << ".old";
        if (counter) {
            newsuffix << "." << counter;
        }
        suffix = newsuffix.str();
        newname = oldname + suffix;
        if (from->hasPeerProperties()) {
            boost::shared_ptr<SyncConfig> renamed(new SyncConfig(from->getPeerName() + suffix));
            if (renamed->exists()) {
                // don't pick a config name which has the same peer name
                // as some other, existing config
                continue;
            }
        }

        // now renaming should succeed, but let's check anyway
        if (!rename(oldname.c_str(),
                    newname.c_str())) {
            break;
        } else if (errno != EEXIST && errno != ENOTEMPTY) {
            SE_THROW(StringPrintf("renaming %s to %s: %s",
                                  oldname.c_str(),
                                  newname.c_str(),
                                  strerror(errno)));
        }
    }

    string newConfigName;
    string oldContext = from->getContextName();
    if (from->hasPeerProperties()) {
        newConfigName = from->getPeerName() + suffix + oldContext;
    } else {
        newConfigName = oldContext + suffix;
    }
    from.reset(new SyncConfig(newConfigName));
}

void Cmdline::copyConfig(const boost::shared_ptr<SyncConfig> &from,
                         const boost::shared_ptr<SyncConfig> &to,
                         const set<string> &selectedSources)
{
    const set<string> *sources = NULL;
    set<string> allSources;
    if (!selectedSources.empty()) {
        // use explicitly selected sources
        sources = &selectedSources;
    } else {
        // need an explicit list of all sources which will be copied,
        // for the createFilters() call below
        BOOST_FOREACH(const std::string &source, from->getSyncSources()) {
            allSources.insert(source);
        }
        sources = &allSources;
    }

    // Apply config changes on-the-fly. Regardless what we do
    // (changing an existing config, migrating, creating from
    // a template), existing shared properties in the desired
    // context must be preserved unless explicitly overwritten.
    // Therefore read those, update with command line properties,
    // then set as filter.
    ConfigProps syncFilter;
    SourceProps sourceFilters;
    m_props.createFilters(to->getContextName(), to->getConfigName(), sources, syncFilter, sourceFilters);
    from->setConfigFilter(true, "", syncFilter);
    BOOST_FOREACH(const SourceProps::value_type &entry, sourceFilters) {
        from->setConfigFilter(false, entry.first, entry.second);
    }

    // Write into the requested configuration, creating it if necessary.
    to->prepareConfigForWrite();
    to->copy(*from, sources);
}

void Cmdline::finishCopy(const boost::shared_ptr<SyncConfig> &from,
                         const boost::shared_ptr<SyncContext> &to)
{
    // give a change to do something before flushing configs to files
    to->preFlush(to->getUserInterfaceNonNull());

    // done, now write it
    m_configModified = true;
    to->flush();

    // migrating peer?
    if (m_migrate &&
        from->hasPeerProperties()) {
        
        // also copy .synthesis dir
        string fromDir, toDir;
        fromDir = from->getRootPath() + "/.synthesis";
        toDir = to->getRootPath() + "/.synthesis";
        if (isDir(fromDir)) {
            cp_r(fromDir, toDir);
        }

        // Succeeded so far, remove "ConsumerReady" flag from migrated
        // config to hide that old config from normal UI users. Must
        // do this without going through SyncConfig, because that
        // would bump the version.
        BoolConfigProperty ready("ConsumerReady", "", "0");
        // Also disable auto-syncing in the migrated config.
        StringConfigProperty autosync("autoSync", "", "");
        {
            FileConfigNode node(from->getRootPath(), "config.ini", false);
            if (ready.getPropertyValue(node)) {
                ready.setProperty(node, false);
            }
            if (!autosync.getProperty(node).empty()) {
                autosync.setProperty(node, "0");
            }
            node.flush();
        }

        // same for very old configs
        {
            FileConfigNode node(from->getRootPath() + "/spds/syncml", "config.txt", false);
            if (!autosync.getProperty(node).empty()) {
                autosync.setProperty(node, "0");
            }
            node.flush();
        }

        // Set ConsumerReady for migrated SyncEvolution < 1.2
        // configs, because in older releases all existing
        // configurations where shown. SyncEvolution 1.2 is more
        // strict and assumes that ConsumerReady must be set
        // explicitly. The sync-ui always has set the flag for
        // configs created or modified with it, but the command
        // line did not. Matches similar code in
        // syncevo-dbus-server.          
        if (from->getConfigVersion(CONFIG_LEVEL_PEER, CONFIG_CUR_VERSION) == 0 /* SyncEvolution < 1.2 */) {
            to->setConsumerReady(true);
            to->flush();
        }
    }
}

void Cmdline::migratePeer(const std::string &fromPeer, const std::string &toPeer)
{
    boost::shared_ptr<SyncConfig> from(new SyncConfig(fromPeer));
    makeObsolete(from);
    // hack: move to different target config for createSyncClient()
    m_server = toPeer;
    boost::shared_ptr<SyncContext> to(createSyncClient());

    // Special case for Memotoo: explicitly set preferred sync format
    // to vCard 3.0 as part of the SyncEvolution 1.1.x -> 1.2 migration,
    // because it works better. Template was also updated in 1.2, but
    // that alone wouldn't improve existing configs.
    if (from->getConfigVersion(CONFIG_LEVEL_PEER, CONFIG_CUR_VERSION) == 0) {
        vector<string> urls = from->getSyncURL();
        if (urls.size() == 1 &&
            urls[0] == "http://sync.memotoo.com/syncML") {
            boost::shared_ptr<SyncContext> to(createSyncClient());
            m_props[to->getContextName()].m_sourceProps["addressbook"].insert(make_pair("syncFormat", "text/vcard"));
        }
    }

    copyConfig(from, to, set<string>());
    finishCopy(from, to);
}

/**
 * Finds first instance of delimiter string in other string. In
 * addition, it treats "\n\n" in a special way: that delimiter also
 * matches "\n\r\n".
 */
class FindDelimiter {
    const string m_delimiter;
public:
    FindDelimiter(const string &delimiter) :
        m_delimiter(delimiter)
    {}
    boost::iterator_range<string::iterator> operator()(string::iterator begin,
                                                       string::iterator end)
    {
        if (m_delimiter == "\n\n") {
            // match both "\n\n" and "\n\r\n"
            while (end - begin >= 2) {
                if (*begin == '\n') {
                    if (*(begin + 1) == '\n') {
                        return boost::iterator_range<string::iterator>(begin, begin + 2);
                    } else if (end - begin >= 3 &&
                               *(begin + 1) == '\r' &&
                               *(begin + 2) == '\n') {
                        return boost::iterator_range<string::iterator>(begin, begin + 3);
                    }
                }
                ++begin;
            }
            return boost::iterator_range<string::iterator>(end, end);
        } else {
            boost::sub_range<string> range(begin, end);
            return boost::find_first(range, m_delimiter);
        }
    }
};

bool Cmdline::run() {
    // --dry-run is only supported by some operations.
    // Be very strict about it and make sure it is off in all
    // potentially harmful operations, otherwise users might
    // expect it to have an effect when it doesn't.

    // TODO: check filter properties for invalid config and source
    // names

    if (m_usage) {
        usage(true);
    } else if (m_version) {
        printf("SyncEvolution %s%s\n",
               VERSION,
               SyncContext::isStableRelease() ? "" : " (pre-release)");
        printf("%s", EDSAbiWrapperInfo());
        printf("%s", SyncSource::backendsInfo().c_str());
    } else if (m_printServers || boost::trim_copy(m_server) == "?") {
        dumpConfigs("Configured servers:",
                    SyncConfig::getConfigs());
    } else if (m_printTemplates) {
        SyncConfig::DeviceList devices;
        if (m_template.empty()){
            devices.push_back (SyncConfig::DeviceDescription("", "", SyncConfig::MATCH_FOR_CLIENT_MODE));
            dumpConfigTemplates("Available configuration templates (servers):",
                    SyncConfig::getPeerTemplates(devices), false);
        } else {
            //limiting at templates for syncml clients only.
            devices.push_back (SyncConfig::DeviceDescription("", m_template, SyncConfig::MATCH_FOR_SERVER_MODE));
            dumpConfigTemplates("Available configuration templates (clients):",
                    SyncConfig::matchPeerTemplates(devices), true);
        }
    } else if (m_dontrun) {
        // user asked for information
    } else if (m_printDatabases) {
        // list databases
        const SourceRegistry &registry(SyncSource::getSourceRegistry());
        boost::shared_ptr<SyncSourceNodes> nodes;
        std::string header;
        boost::shared_ptr<SyncContext> context;
        FilterConfigNode::ConfigFilter sourceFilter = m_props.createSourceFilter(m_server, "");
        FilterConfigNode::ConfigFilter::const_iterator backend = sourceFilter.find("backend");

        if (!m_server.empty()) {
            // list for specific backend chosen via config
            if (m_sources.size() != 1) {
                SE_THROW(StringPrintf("must specify exactly one source after the config name '%s'",
                                      m_server.c_str()));
            }
            context.reset(new SyncContext(m_server));
            if (!context->exists()) {
                SE_THROW(StringPrintf("config '%s' does not exist", m_server.c_str()));
            }
            nodes.reset(new SyncSourceNodes(context->getSyncSourceNodesNoTracking(*m_sources.begin())));
            header = StringPrintf("%s/%s", m_server.c_str(), m_sources.begin()->c_str());
            if (!nodes->dataConfigExists()) {
                SE_THROW(StringPrintf("%s does not exist",
                                      header.c_str()));
            }
        } else {
            context.reset(new SyncContext);
            boost::shared_ptr<FilterConfigNode> sharedNode(new VolatileConfigNode());
            boost::shared_ptr<FilterConfigNode> configNode(new VolatileConfigNode());
            boost::shared_ptr<FilterConfigNode> hiddenNode(new VolatileConfigNode());
            boost::shared_ptr<FilterConfigNode> trackingNode(new VolatileConfigNode());
            boost::shared_ptr<FilterConfigNode> serverNode(new VolatileConfigNode());
            nodes.reset(new SyncSourceNodes(true, sharedNode, configNode, hiddenNode, trackingNode, serverNode, ""));
            header = backend != sourceFilter.end() ?
                backend->second :
                "???";
        }
        nodes->getProperties()->setFilter(sourceFilter);
        FilterConfigNode::ConfigFilter syncFilter = m_props.createSyncFilter(m_server);
        context->setConfigFilter(true, "", syncFilter);

        SyncSourceParams params("list", *nodes, context);
        if (!m_server.empty() || backend != sourceFilter.end()) {
            // list for specific backend
            auto_ptr<SyncSource> source(SyncSource::createSource(params, false, NULL));
            if (source.get() != NULL) {
                listSources(*source, header);
                SE_LOG_SHOW(NULL, NULL, "");
            } else {
                SE_LOG_SHOW(NULL, NULL, "%s:\n   cannot list databases", header.c_str());
            }
        } else {
            // list for all backends
            BOOST_FOREACH(const RegisterSyncSource *source, registry) {
                BOOST_FOREACH(const Values::value_type &alias, source->m_typeValues) {
                    if (!alias.empty() && source->m_enabled) {
                        SourceType type(*alias.begin());
                        nodes->getProperties()->setProperty("backend", type.m_backend);
                        std::string header = boost::join(alias, " = ");
                        try {
                            auto_ptr<SyncSource> source(SyncSource::createSource(params, false));
                            if (!source.get()) {
                                // silently skip backends like the "file" backend which do not support
                                // listing databases and return NULL unless configured properly
                            } else {
                                listSources(*source, header);
                                SE_LOG_SHOW(NULL, NULL, "");
                            }
                        } catch (...) {
                            SE_LOG_ERROR(NULL, NULL, "%s:\nlisting databases failed", header.c_str());
                            Exception::handle();
                        }
                    }
                }
            }
        }
    } else if (m_printConfig) {
        boost::shared_ptr<SyncConfig> config;
        ConfigProps syncFilter;
        SourceProps sourceFilters;

        if (m_template.empty()) {
            if (m_server.empty()) {
                usage(false, "--print-config requires either a --template or a server name.");
                return false;
            }
            config.reset(new SyncConfig(m_server));
            if (!config->exists()) {
                SE_LOG_ERROR(NULL, NULL, "Server '%s' has not been configured yet.", m_server.c_str());
                return false;
            }

            // No need to include a context or additional sources,
            // because reading the m_server config already includes
            // the right information.
            m_props.createFilters("", m_server, NULL, syncFilter, sourceFilters);
        } else {
            string peer, context;
            SyncConfig::splitConfigString(SyncConfig::normalizeConfigString(m_template,
                                                                            SyncConfig::NormalizeFlags(SyncConfig::NORMALIZE_SHORTHAND|SyncConfig::NORMALIZE_IS_NEW)),
                                          peer, context);
            config = SyncConfig::createPeerTemplate(peer);
            if (!config.get()) {
                SE_LOG_ERROR(NULL, NULL, "No configuration template for '%s' available.", m_template.c_str());
                return false;
            }

            // When instantiating a template, include the properties
            // of the target context as filter to preserve shared
            // properties, the final name inside that context as
            // peer config name, and the sources defined in the template.
            list<string> sourcelist = config->getSyncSources();
            set<string> sourceset(sourcelist.begin(), sourcelist.end());
            m_props.createFilters(std::string("@") + context, "",
                                  &sourceset,
                                  syncFilter, sourceFilters);
        }

        // determine whether we dump a peer or a context
        int flags = DUMP_PROPS_NORMAL;
        string peer, context;
        SyncConfig::splitConfigString(config->getConfigName(), peer, context);
        if (peer.empty()) {
            flags |= HIDE_PER_PEER;
            checkForPeerProps();
        } 

        if (m_sources.empty() ||
            m_sources.find("main") != m_sources.end()) {
            boost::shared_ptr<FilterConfigNode> syncProps(config->getProperties());
            syncProps->setFilter(syncFilter);
            dumpProperties(*syncProps, config->getRegistry(), flags);
        }

        list<string> sources = config->getSyncSources();
        sources.sort();
        BOOST_FOREACH(const string &name, sources) {
            if (m_sources.empty() ||
                m_sources.find(name) != m_sources.end()) {
                SE_LOG_SHOW(NULL, NULL, "[%s]", name.c_str());
                SyncSourceNodes nodes = config->getSyncSourceNodes(name);
                boost::shared_ptr<FilterConfigNode> sourceProps = nodes.getProperties();
                sourceProps->setFilter(sourceFilters.createSourceFilter(name));
                dumpProperties(*sourceProps, SyncSourceConfig::getRegistry(),
                               flags | ((name != *(--sources.end())) ? HIDE_LEGEND : DUMP_PROPS_NORMAL));
            }
        }
    } else if (m_configure || m_migrate) {
        if (!needConfigName()) {
            return false;
        }
        if (m_dryrun) {
            SyncContext::throwError("--dry-run not supported for configuration changes");
        }
        if (m_keyring &&
            GetLoadPasswordSignal().empty()) {
            SE_LOG_ERROR(NULL, NULL,
                         "This syncevolution binary was compiled without support for storing "
                         "passwords in a keyring or wallet, or the backends for that functionality are not usable. "
                         "Either store passwords in your configuration "
                         "files or enter them interactively on each program run.");
            return false;
        }

        // name of renamed config ("foo.old") after migration
        string newname;

        // True if the target configuration is a context like @default
        // or @foobar. Relevant in several places in the following
        // code.
        bool configureContext = false;

        bool fromScratch = false;
        string peer, context;
        SyncConfig::splitConfigString(SyncConfig::normalizeConfigString(m_server), peer, context);
        if (peer.empty()) {
            configureContext = true;
            checkForPeerProps();
        }

        // Make m_server a fully-qualified name. Useful in error
        // messages and essential for migrating "foo" where "foo"
        // happens to map to "foo@bar".  Otherwise "foo" will be
        // mapped incorrectly to "foo@default" after renaming
        // "foo@bar" to "foo.old@bar".
        //
        // The inverse problem can occur for "foo@default": after
        // renaming, "foo" without "@default" would be mapped to
        // "foo@somewhere-else" if such a config exists.
        m_server = peer + "@" + context;

        // Both config changes and migration are implemented as copying from
        // another config (template resp. old one). Migration also moves
        // the old config. The target configuration is determined by m_server,
        // but the exact semantic of it depends on the operation.
        boost::shared_ptr<SyncConfig> from;
        boost::shared_ptr<SyncContext> to;
        string origPeer;
        if (m_migrate) {
            if (!m_sources.empty()) {
                SE_LOG_ERROR(NULL, NULL, "cannot migrate individual sources");
                return false;
            }

            string oldContext = context;
            from.reset(new SyncConfig(m_server));
            if (!from->exists()) {
                // for migration into a different context, search for config without context
                oldContext = "";
                from.reset(new SyncConfig(peer));
                if (!from->exists()) {
                    SE_LOG_ERROR(NULL, NULL, "Server '%s' has not been configured yet.", m_server.c_str());
                    return false;
                }
            }

            // Check if we are migrating an individual peer inside
            // a context which itself is too old. In that case,
            // the whole context and everything inside it needs to
            // be migrated.
            if (!configureContext) {
                bool obsoleteContext = false;
                if (from->getLayout() < SyncConfig::SHARED_LAYOUT) {
                    // check whether @default context exists and is too old;
                    // in that case migrate it first
                    SyncConfig target("@default");
                    if (target.exists() &&
                        target.getConfigVersion(CONFIG_LEVEL_CONTEXT, CONFIG_CUR_VERSION) <
                        CONFIG_CONTEXT_MIN_VERSION) {
                        // migrate all peers inside @default *and* the one outside
                        origPeer = m_server;
                        m_server = "@default";
                        obsoleteContext = true;
                    }
                } else {
                    // config already is inside a context; need to check that context
                    if (from->getConfigVersion(CONFIG_LEVEL_CONTEXT, CONFIG_CUR_VERSION) <
                        CONFIG_CONTEXT_MIN_VERSION) {
                        m_server = string("@") + context;
                        obsoleteContext = true;
                    }
                }
                if (obsoleteContext) {
                    // hack: move to different config and back later
                    from.reset(new SyncConfig(m_server));
                    peer = "";
                    configureContext = true;
                }
            }

            // rename on disk and point "from" to it
            makeObsolete(from);

            // modify the config referenced by the (possibly modified) m_server
            to.reset(createSyncClient());
        } else {
            from.reset(new SyncConfig(m_server));
            // m_server known, modify it
            to.reset(createSyncClient());

            if (!from->exists()) {
                // creating from scratch, look for template
                fromScratch = true;
                string configTemplate;
                if (m_template.empty()) {
                    if (configureContext) {
                        // configuring a context, template doesn't matter =>
                        // use default "SyncEvolution" template
                        configTemplate =
                            peer = "SyncEvolution";
                        from = SyncConfig::createPeerTemplate(peer);
                    } else if (peer == "target-config") {
                        // Configuring the source context for local sync
                        // => determine template based on context name.
                        configTemplate = context;
                        from = SyncConfig::createPeerTemplate(context);
                    } else {
                        // template is the peer name
                        configTemplate = m_server;
                        from = SyncConfig::createPeerTemplate(peer);
                    }
                } else {
                    // Template is specified explicitly. It must not contain a context,
                    // because the context comes from the config name.
                    configTemplate = m_template;
                    if (SyncConfig::splitConfigString(SyncConfig::normalizeConfigString(configTemplate,
                                                                                        SyncConfig::NormalizeFlags(SyncConfig::NORMALIZE_SHORTHAND|SyncConfig::NORMALIZE_IS_NEW)),
                                                      peer, context)) {
                        SE_LOG_ERROR(NULL, NULL, "Template %s must not specify a context.", configTemplate.c_str());
                        return false;
                    }
                    string tmp;
                    SyncConfig::splitConfigString(SyncConfig::normalizeConfigString(m_server), tmp, context);
                    from = SyncConfig::createPeerTemplate(peer);
                }
                list<string> missing;
                if (!from.get()) {
                    // check if all obligatory sync properties are specified; needed
                    // for both the "is complete" check and the error message below
                    ConfigProps syncProps = m_props.createSyncFilter(to->getContextName());
                    bool complete = true;
                    BOOST_FOREACH(const ConfigProperty *prop, SyncConfig::getRegistry()) {
                        if (prop->isObligatory() &&
                            syncProps.find(prop->getMainName()) == syncProps.end()) {
                            missing.push_back(prop->getMainName());
                            complete = false;
                        }
                    }

                    // if everything was specified and no invalid template name was given, allow user
                    // to proceed with "none" template; if a template was specified, we skip
                    // this and go directly to the code below which prints an error message
                    if (complete &&
                        m_template.empty()) {
                        from = SyncConfig::createPeerTemplate("none");
                    }
                }
                if (!from.get()) {
                    SE_LOG_ERROR(NULL, NULL, "No configuration template for '%s' available.", configTemplate.c_str());
                    if (m_template.empty()) {
                        SE_LOG_INFO(NULL, NULL,
                                    "Use '--template none' and/or specify relevant properties on the command line to create a configuration without a template. Need values for: %s",
                                    boost::join(missing, ", ").c_str());
                    } else if (missing.empty()) {
                        SE_LOG_INFO(NULL, NULL, "All relevant properties seem to be set, omit the --template parameter to proceed.");
                    }
                    SE_LOG_SHOW(NULL, NULL, "");
                    SyncConfig::DeviceList devices;
                    devices.push_back(SyncConfig::DeviceDescription("", "", SyncConfig::MATCH_ALL));
                    dumpConfigTemplates("Available configuration templates (clients and servers):",
                                        SyncConfig::getPeerTemplates(devices));
                    return false;
                }
            }
        }

        // Which sources are configured is determined as follows:
        // - all sources in the template by default (empty set), except when
        // - sources are listed explicitly, and either
        // - updating an existing config or
        // - configuring a context.
        //
        // This implies that when configuring a peer from scratch, all
        // sources in the template will be created, with command line
        // source properties applied to all of them. This might not be
        // what we want, but because this is how we have done it
        // traditionally, I keep this behavior for now.
        //
        // When migrating, m_sources is empty and thus the whole set of
        // sources will be migrated. Checking it here for clarity's sake.
        set<string> sources;
        if (!m_migrate &&
            !m_sources.empty() &&
            (!fromScratch || configureContext)) {
            sources = m_sources;
        }

        // Also copy (aka create) sources listed on the command line if
        // creating from scratch and
        // - "--template none" enables the "do what I want" mode or
        // - source properties apply to it.
        // Creating from scratch with other sources is a possible typo
        // and will trigger an error below.
        if (fromScratch) {
            BOOST_FOREACH(const string &source, m_sources) {
                if (m_template == "none" ||
                    !m_props.createSourceFilter(to->getContextName(), source).empty()) {
                    sources.insert(source);
                }
            }
        }

        // Special case for migration away from "type": older
        // SyncEvolution could cope with "type" only set correctly for
        // peers. Real-world case: Memotoo config, context had "type =
        // calendar" set for address book.
        //
        // Setting "backend" based on an incorrect "type" from the
        // context would lead to a broken, unusable config. Solution:
        // take "backend" and "databaseFormat" from a peer config when
        // migrating a context.
        //
        // Note that peers are assumed to be consistent. Not attempt is
        // made to detect a config which has inconsistent peer configs.
        if (m_migrate && configureContext &&
            from->getConfigVersion(CONFIG_LEVEL_CONTEXT, CONFIG_CUR_VERSION) == 0) {
            list<string> peers = from->getPeers();
            peers.sort(); // make code below deterministic

            BOOST_FOREACH(const std::string source, from->getSyncSources()) {
                BOOST_FOREACH(const string &peer, peers) {
                    FileConfigNode node(from->getRootPath() + "/peers/" + peer + "/sources/" + source,
                                        "config.ini",
                                        true);
                    string sync = node.readProperty("sync");
                    if (sync.empty() ||
                        boost::iequals(sync, "none") ||
                        boost::iequals(sync, "disabled")) {
                        // ignore this peer, it doesn't use the source
                        continue;
                    }

                    SourceType type(node.readProperty("type"));
                    if (!type.m_backend.empty()) {
                        // found some "type": use "backend" and
                        // "dataFormat" in filter, unless the user
                        // already set a value there
                        ConfigProps syncFilter;
                        SourceProps sourceFilters;
                        set<string> set;
                        set.insert(source);
                        m_props.createFilters(to->getContextName(), "",
                                              &set, syncFilter, sourceFilters);
                        const ConfigProps &sourceFilter = sourceFilters[source];
                        if (sourceFilter.find("backend") == sourceFilter.end()) {
                            m_props[to->getContextName()].m_sourceProps[source]["backend"] = type.m_backend;
                        }
                        if (!type.m_localFormat.empty() &&
                            sourceFilter.find("databaseFormat") == sourceFilter.end()) {
                            m_props[to->getContextName()].m_sourceProps[source]["databaseFormat"] = type.m_localFormat;
                        }
                        // use it without bothering to keep looking
                        // (no consistenty check!)
                        break;
                    }
                }
            }
        }

        // copy and filter into the target config: createSyncClient()
        // creates a SyncContext for m_server, with propert
        // implementation of the password handling methods in derived
        // classes (D-Bus server, real command line)
        copyConfig(from, to, sources);

        // Sources are active now according to the server default.
        // Disable all sources not selected by user (if any selected)
        // and those which have no database.
        if (fromScratch) {
            list<string> configuredSources = to->getSyncSources();
            set<string> sources = m_sources;
            
            BOOST_FOREACH(const string &source, configuredSources) {
                boost::shared_ptr<PersistentSyncSourceConfig> sourceConfig(to->getSyncSourceConfig(source));
                string disable = "";
                set<string>::iterator entry = sources.find(source);
                bool selected = entry != sources.end();

                if (!m_sources.empty() &&
                    !selected) {
                    disable = "not selected";
                } else {
                    if (entry != sources.end()) {
                        // The command line parameter matched a valid source.
                        // All entries left afterwards must have been typos.
                        sources.erase(entry);
                    }

                    // check whether the sync source works
                    SyncSourceParams params(source, to->getSyncSourceNodes(source), to);
                    auto_ptr<SyncSource> syncSource(SyncSource::createSource(params, false, to.get()));
                    if (syncSource.get() == NULL) {
                        disable = "no backend available";
                    } else {
                        try {
                            SyncSource::Databases databases = syncSource->getDatabases();
                            if (databases.empty()) {
                                disable = "no database to synchronize";
                            }
                        } catch (...) {
                            disable = "backend failed";
                        }
                    }
                }

                // Do sanity checking of source (can it be enabled?),
                // but only set the sync mode if configuring a peer.
                // A context-only config doesn't have the "sync"
                // property.
                string syncMode;
                if (!disable.empty()) {
                    // abort if the user explicitly asked for the sync source
                    // and it cannot be enabled, otherwise disable it silently
                    if (selected) {
                        SyncContext::throwError(source + ": " + disable);
                    }
                    syncMode = "disabled";
                } else if (selected) {
                    // user absolutely wants it: enable even if off by default
                    ConfigProps filter = m_props.createSourceFilter(m_server, source);
                    ConfigProps::const_iterator sync = filter.find("sync");
                    syncMode = sync == filter.end() ? "two-way" : sync->second;
                }
                if (!syncMode.empty() &&
                    !configureContext) {
                    sourceConfig->setSync(syncMode);
                }
            }

            if (!sources.empty()) {
                SyncContext::throwError(string("no such source(s): ") + boost::join(sources, " "));
            }
        }

        // flush, move .synthesis dir, set ConsumerReady, ...
        finishCopy(from, to);

        // Now also migrate all peers inside context?
        if (configureContext && m_migrate) {
            BOOST_FOREACH(const string &peer, from->getPeers()) {
                migratePeer(peer + from->getContextName(), peer + to->getContextName());
            }
            if (!origPeer.empty()) {
                migratePeer(origPeer, origPeer + to->getContextName());
            }
        }
    } else if (m_remove) {
        if (!needConfigName()) {
            return false;
        }
        if (m_dryrun) {
            SyncContext::throwError("--dry-run not supported for removing configurations");
        }

        // extra sanity check
        if (!m_sources.empty() ||
            m_props.hasProperties()) {
            usage(false, "too many parameters for --remove");
            return false;
        } else {
            boost::shared_ptr<SyncConfig> config;
            config.reset(new SyncConfig(m_server));
            if (!config->exists()) {
                SyncContext::throwError(string("no such configuration: ") + m_server);
            }
            config->remove();
            m_configModified = true;
            return true;
        }
    } else if (m_accessItems) {
        // need access to specific source
        boost::shared_ptr<SyncContext> context;
        context.reset(createSyncClient());

        // operating on exactly one source (can be optional)
        string sourceName;
        bool haveSourceName = !m_sources.empty();
        if (haveSourceName) {
            sourceName = *m_sources.begin();
        }

        // apply filters
        context->setConfigFilter(true, "", m_props.createSyncFilter(m_server));
        context->setConfigFilter(false, "", m_props.createSourceFilter(m_server, sourceName));

        SyncSourceNodes sourceNodes = context->getSyncSourceNodesNoTracking(sourceName);
        SyncSourceParams params(sourceName, sourceNodes, context);
        cxxptr<SyncSource> source;

        try {
            source.set(SyncSource::createSource(params, true));
        } catch (const StatusException &ex) {
            // Creating the source failed. Detect some common reasons for this
            // and log those instead. None of these situations are fatal by themselves,
            // but in combination they are a problem.
            if (ex.syncMLStatus() == SyncMLStatus(sysync::LOCERR_CFGPARSE)) {
                std::list<std::string> explanation;

                explanation.push_back(ex.what());
                if (!m_server.empty() && !context->exists()) {
                    explanation.push_back(StringPrintf("configuration '%s' does not exist", m_server.c_str()));
                }
                if (haveSourceName && !sourceNodes.exists()) {
                    explanation.push_back(StringPrintf("source '%s' does not exist", sourceName.c_str()));
                } else if (!haveSourceName) {
                    explanation.push_back("no source selected");
                }
                SyncSourceConfig sourceConfig(sourceName, sourceNodes);
                if (!sourceConfig.getBackend().wasSet()) {
                    explanation.push_back("backend property not set");
                }
                SyncContext::throwError(SyncMLStatus(sysync::LOCERR_CFGPARSE),
                                        boost::join(explanation, "\n"));
            } else {
                throw;
            }
        }

        sysync::TSyError err;
#define CHECK_ERROR(_op) if (err) { SE_THROW_EXCEPTION_STATUS(StatusException, string(source->getName()) + ": " + (_op), SyncMLStatus(err)); }

        // acquire passwords before doing anything (interactive password
        // access not supported for the command line)
        {
            ConfigPropertyRegistry& registry = SyncConfig::getRegistry();
            BOOST_FOREACH(const ConfigProperty *prop, registry) {
                prop->checkPassword(context->getUserInterfaceNonNull(), m_server, *context->getProperties());
            }
        }
        {
            ConfigPropertyRegistry &registry = SyncSourceConfig::getRegistry();
            BOOST_FOREACH(const ConfigProperty *prop, registry) {
                prop->checkPassword(context->getUserInterfaceNonNull(), m_server, *context->getProperties(),
                                    source->getName(), sourceNodes.getProperties());
            }
        }

        source->open();
        const SyncSource::Operations &ops = source->getOperations();
        if (m_printItems) {
            SyncSourceLogging *logging = dynamic_cast<SyncSourceLogging *>(source.get());
            if (!ops.m_startDataRead ||
                !ops.m_readNextItem) {
                source->throwError("reading items not supported");
            }

            err = ops.m_startDataRead(*source, "", "");
            CHECK_ERROR("reading items");
            list<string> luids;
            readLUIDs(source, luids);
            BOOST_FOREACH(string &luid, luids) {
                string description;
                if (logging) {
                    description = logging->getDescription(luid);
                }
                SE_LOG_SHOW(NULL, NULL, "%s%s%s",
                            CmdlineLUID::fromLUID(luid).c_str(),
                            description.empty() ? "" : ": ",
                            description.c_str());
            }
        } else if (m_deleteItems) {
            if (!ops.m_deleteItem) {
                source->throwError("deleting items not supported");
            }
            list<string> luids;
            bool deleteAll = std::find(m_luids.begin(), m_luids.end(), "*") != m_luids.end();
            err = ops.m_startDataRead(*source, "", "");
            CHECK_ERROR("reading items");
            if (deleteAll) {
                readLUIDs(source, luids);
            } else {
                luids = m_luids;
            }
            if (ops.m_endDataRead) {
                err = ops.m_endDataRead(*source);
                CHECK_ERROR("stop reading items");
            }
            if (ops.m_startDataWrite) {
                err = ops.m_startDataWrite(*source);
                CHECK_ERROR("writing items");
            }
            BOOST_FOREACH(const string &luid, luids) {
                sysync::ItemIDType id;
                id.item = (char *)luid.c_str();
                err = ops.m_deleteItem(*source, &id);
                CHECK_ERROR("deleting item");
            }
            char *token;
            err = ops.m_endDataWrite(*source, true, &token);
            if (token) {
                free(token);
            }
            CHECK_ERROR("stop writing items");
        } else {
            SyncSourceRaw *raw = dynamic_cast<SyncSourceRaw *>(source.get());
            if (!raw) {
                source->throwError("reading/writing items directly not supported");
            }
            if (m_import || m_update) {
                err = ops.m_startDataRead(*source, "", "");
                CHECK_ERROR("reading items");
                if (ops.m_endDataRead) {
                    err = ops.m_endDataRead(*source);
                    CHECK_ERROR("stop reading items");
                }
                if (ops.m_startDataWrite) {
                    err = ops.m_startDataWrite(*source);
                    CHECK_ERROR("writing items");
                }

                cxxptr<ifstream> inFile;
                if (m_itemPath =="-" ||
                    !isDir(m_itemPath)) {
                    string content;
                    string luid;
                    if (m_itemPath == "-") {
                        context->getUserInterfaceNonNull().readStdin(content);
                    } else if (!ReadFile(m_itemPath, content)) {
                        SyncContext::throwError(m_itemPath, errno);
                    }
                    if (m_delimiter == "none") {
                        if (m_update) {
                            if (m_luids.size() != 1) {
                                SyncContext::throwError("need exactly one LUID parameter");
                            } else {
                                luid = *m_luids.begin();
                            }
                        }
                        SE_LOG_SHOW(NULL, NULL, "#0: %s",
                                    insertItem(raw, luid, content).getEncoded().c_str());
                    } else {
                        typedef boost::split_iterator<string::iterator> string_split_iterator;
                        int count = 0;
                        FindDelimiter finder(m_delimiter);

                        // when updating, check number of luids in advance
                        if (m_update) {
                            unsigned long total = 0;
                            for (string_split_iterator it =
                                     boost::make_split_iterator(content, finder);
                                 it != string_split_iterator();
                                 ++it) {
                                total++;
                            }
                            if (total != m_luids.size()) {
                                SyncContext::throwError(StringPrintf("%lu items != %lu luids, must match => aborting",
                                                                     total, (unsigned long)m_luids.size()));
                            }
                        }
                        list<string>::const_iterator luidit = m_luids.begin();
                        for (string_split_iterator it =
                                 boost::make_split_iterator(content, finder);
                             it != string_split_iterator();
                             ++it) {
                            string luid;
                            if (m_update) {
                                if (luidit == m_luids.end()) {
                                    // was checked above
                                    SyncContext::throwError("internal error, not enough luids");
                                }
                                luid = *luidit;
                                ++luidit;
                            }
                            SE_LOG_SHOW(NULL, NULL, "#%d: %s",
                                        count,
                                        insertItem(raw,
                                                   luid,
                                                   string(it->begin(), it->end())).getEncoded().c_str());
                            count++;
                        }
                    }
                } else {
                    ReadDir dir(m_itemPath);
                    int count = 0;
                    BOOST_FOREACH(const string &entry, dir) {
                        string content;
                        string path = m_itemPath + "/" + entry;
                        if (!ReadFile(path, content)) {
                            SyncContext::throwError(path, errno);
                        }
                        SE_LOG_SHOW(NULL, NULL, "#%d: %s: %s",
                                    count,
                                    entry.c_str(),
                                    insertItem(raw, "", content).getEncoded().c_str());
                    }
                }
                char *token = NULL;
                err = ops.m_endDataWrite(*source, true, &token);
                if (token) {
                    free(token);
                }
                CHECK_ERROR("stop writing items");
            } else if (m_export) {
                err = ops.m_startDataRead(*source, "", "");
                CHECK_ERROR("reading items");

                ostream *out = NULL;
                cxxptr<ofstream> outFile;
                if (m_itemPath == "-") {
                    // not actually used, falls back to SE_LOG_SHOW()
                    out = &std::cout;
                } else if(!isDir(m_itemPath)) {
                    outFile.set(new ofstream(m_itemPath.c_str()));
                    out = outFile;
                }
                if (m_luids.empty()) {
                    readLUIDs(source, m_luids);
                }
                bool haveItem = false;     // have written one item
                bool haveNewline = false;  // that item had a newline at the end
                BOOST_FOREACH(const string &luid, m_luids) {
                    string item;
                    raw->readItemRaw(luid, item);
                    if (!out) {
                        // write into directory
                        string fullPath = m_itemPath + "/" + luid;
                        ofstream file((m_itemPath + "/" + luid).c_str());
                        file << item;
                        file.close();
                        if (file.bad()) {
                            SyncContext::throwError(fullPath, errno);
                        }
                    } else {
                        std::string delimiter;
                        if (haveItem) {
                            if (m_delimiter.size() > 1 &&
                                haveNewline &&
                                m_delimiter[0] == '\n') {
                                // already wrote initial newline, skip it
                                delimiter = m_delimiter.substr(1);
                            } else {
                                delimiter = m_delimiter;
                            }
                        }
                        if (out == &std::cout) {
                            // special case, use logging infrastructure
                            SE_LOG_SHOW(NULL, NULL, "%s%s",
                                        delimiter.c_str(),
                                        item.c_str());
                            // always prints newline
                            haveNewline = true;
                        } else {
                            // write to file
                            *out << item;
                            haveNewline = boost::ends_with(item, "\n");
                        }
                        haveItem = true;
                    }
                }
                if (outFile) {
                    outFile->close();
                    if (outFile->bad()) {
                        SyncContext::throwError(m_itemPath, errno);
                    }
                }
            }
        }
        source->close();
    } else {
        if (!needConfigName()) {
            return false;
        }

        std::set<std::string> unmatchedSources;
        boost::shared_ptr<SyncContext> context;
        context.reset(createSyncClient());
        context->setConfigProps(m_props);
        context->setQuiet(m_quiet);
        context->setDryRun(m_dryrun);
        context->setConfigFilter(true, "", m_props.createSyncFilter(m_server));
        if (m_sources.empty()) {
            // Special semantic of 'no source selected': apply
            // filter (if any exists) only to sources which are
            // *active*. Configuration of inactive sources is left
            // unchanged. This way we don't activate sync sources
            // accidentally when the sync mode is modified
            // temporarily.
            BOOST_FOREACH(const std::string &source,
                          context->getSyncSources()) {
                boost::shared_ptr<PersistentSyncSourceConfig> source_config =
                    context->getSyncSourceConfig(source);
                if (!source_config->isDisabled()) {
                    context->setConfigFilter(false, source, m_props.createSourceFilter(m_server, source));
                }
            }
        } else {
            // apply (possibly empty) source filter to selected sources
            BOOST_FOREACH(const std::string &source,
                          m_sources) {
                boost::shared_ptr<PersistentSyncSourceConfig> source_config =
                        context->getSyncSourceConfig(source);
                ConfigProps filter = m_props.createSourceFilter(m_server, source);
                if (!source_config || !source_config->exists()) {
                    // invalid source name in m_sources, remember and
                    // report this below
                    unmatchedSources.insert(source);
                } else if (filter.find("sync") == filter.end()) {
                    // Sync mode is not set, must override the
                    // "sync=disabled" set below with the original
                    // sync mode for the source or (if that is also
                    // "disabled") with "two-way". The latter is part
                    // of the command line semantic that listing a
                    // source activates it.
                    string sync = source_config->getSync();
                    filter["sync"] =
                        sync == "disabled" ? "two-way" : sync;
                    context->setConfigFilter(false, source, filter);
                } else {
                    // sync mode is set, can use m_sourceProps
                    // directly to apply it
                    context->setConfigFilter(false, source, filter);
                }
            }

            // temporarily disable the rest
            FilterConfigNode::ConfigFilter disabled;
            disabled["sync"] = "disabled";
            context->setConfigFilter(false, "", disabled);
        }

        // check whether there were any sources specified which do not exist
        if (unmatchedSources.size()) {
            context->throwError(string("no such source(s): ") + boost::join(unmatchedSources, " "));
        }

        if (m_status) {
            context->status();
        } else if (m_printSessions) {
            vector<string> dirs;
            context->getSessions(dirs);
            bool first = true;
            BOOST_FOREACH(const string &dir, dirs) {
                if (first) {
                    first = false;
                } else if(!m_quiet) {
                    SE_LOG_SHOW(NULL, NULL, "");
                }
                SE_LOG_SHOW(NULL, NULL, "%s", dir.c_str());
                if (!m_quiet) {
                    SyncReport report;
                    context->readSessionInfo(dir, report);
                    ostringstream out;
                    out << report;
                    SE_LOG_SHOW(NULL, NULL, "%s", out.str().c_str());
                }
            }
        } else if (!m_restore.empty()) {
            // sanity checks: either --after or --before must be given, sources must be selected
            if ((!m_after && !m_before) ||
                (m_after && m_before)) {
                usage(false, "--restore <log dir> must be used with either --after (restore database as it was after that sync) or --before (restore data from before sync)");
                return false;
            }
            if (m_sources.empty()) {
                usage(false, "Sources must be selected explicitly for --restore to prevent accidental restore.");
                return false;
            }
            context->restore(m_restore,
                             m_after ?
                             SyncContext::DATABASE_AFTER_SYNC :
                             SyncContext::DATABASE_BEFORE_SYNC);
        } else {
            if (m_dryrun) {
                SyncContext::throwError("--dry-run not supported for running a synchronization");
            }

            // safety catch: if props are given, then --run
            // is required
            if (!m_run &&
                (m_props.hasProperties())) {
                usage(false, "Properties specified, but neither '--configure' nor '--run' - what did you want?");
                return false;
            }

            return (context->sync(&m_report) == STATUS_OK);
        }
    }

    return true;
}

void Cmdline::readLUIDs(SyncSource *source, list<string> &luids)
{
    const SyncSource::Operations &ops = source->getOperations();
    sysync::ItemIDType id;
    sysync::sInt32 status;
    sysync::TSyError err = ops.m_readNextItem(*source, &id, &status, true);
    CHECK_ERROR("next item");
    while (status != sysync::ReadNextItem_EOF) {
        luids.push_back(id.item);
        StrDispose(id.item);
        StrDispose(id.parent);
        err = ops.m_readNextItem(*source, &id, &status, false);
        CHECK_ERROR("next item");
    }
}

CmdlineLUID Cmdline::insertItem(SyncSourceRaw *source, const string &luid, const string &data)
{
    SyncSourceRaw::InsertItemResult res = source->insertItemRaw(luid, data);
    CmdlineLUID cluid;
    cluid.setLUID(res.m_luid);
    return cluid;
}

string Cmdline::cmdOpt(const char *opt, const char *param)
{
    string res = "'";
    if (opt) {
        res += opt;
    }
    if (opt && param) {
        res += " ";
    }
    if (param) {
        res += param;
    }
    res += "'";
    return res;
}

bool Cmdline::parseProp(PropertyType propertyType,
                        const char *opt,
                        const char *param,
                        const char *propname)
{
    std::string args = cmdOpt(opt, param);

    if (!param) {
        usage(false, string("missing parameter for ") + args);
        return false;
    }

    // determine property name and parameter for it
    string propstr;
    string paramstr;
    if (propname) {
        propstr = propname;
        paramstr = param;
    } else if (boost::trim_copy(string(param)) == "?") {
        paramstr = param;
    } else {
        const char *equal = strchr(param, '=');
        if (!equal) {
            usage(false, string("the '=<value>' part is missing in: ") + args);
            return false;
        }
        propstr.assign(param, equal - param);
        paramstr.assign(equal + 1);
    }
    boost::trim(propstr);
    boost::trim_left(paramstr);

    // parse full property string
    PropertySpecifier spec = PropertySpecifier::StringToPropSpec(propstr);

    // determine property type and registry
    const ConfigPropertyRegistry *validProps = NULL;
    switch (propertyType) {
    case SYNC_PROPERTY_TYPE:
        validProps = &m_validSyncProps;
        break;
    case SOURCE_PROPERTY_TYPE:
        validProps = &m_validSourceProps;
        break;
    case UNKNOWN_PROPERTY_TYPE:
        // must guess based on both registries
        if (!propstr.empty()) {
            bool isSyncProp = m_validSyncProps.find(spec.m_property) != NULL;
            bool isSourceProp = m_validSourceProps.find(spec.m_property) != NULL;

            if (isSyncProp) {
                if (isSourceProp) {
                    usage(false, StringPrintf("property '%s' in %s could be both a sync and a source property, use --sync-property or --source-property to disambiguate it", propname, args.c_str()));
                    return false;
                } else {
                    validProps = &m_validSyncProps;
                }
            } else if (isSourceProp ||
                       boost::iequals(spec.m_property, "type")) {
                validProps = &m_validSourceProps;
            } else {
                if (propname) {
                    usage(false, StringPrintf("unrecognized property '%s' in %s", propname, args.c_str()));
                } else {
                    usage(false, StringPrintf("unrecognized property in %s", args.c_str()));
                }
                return false;
            }
        } else {
            usage(false, StringPrintf("a property name must be given in %s", args.c_str()));
            return false;
        }
    }

    if (boost::trim_copy(string(param)) == "?") {
        m_dontrun = true;
        if (propname) {
            return listPropValues(*validProps, spec.m_property, opt ? opt : "");
        } else {
            return listProperties(*validProps, opt ? opt : "");
        }
    } else {
        if (boost::trim_copy(paramstr) == "?") {
            m_dontrun = true;
            return listPropValues(*validProps, spec.m_property, args);
        } else {
            const ConfigProperty *prop = validProps->find(spec.m_property);
            if (!prop && boost::iequals(spec.m_property, "type")) {
                // compatiblity mode for "type": map to the properties which
                // replaced it
                prop = validProps->find("backend");
                if (!prop) {
                    SE_LOG_ERROR(NULL, NULL, "backend: no such property");
                    return false;
                }
                SourceType sourceType(paramstr);
                string error;
                if (!prop->checkValue(sourceType.m_backend, error)) {
                    SE_LOG_ERROR(NULL, NULL, "%s: %s", args.c_str(), error.c_str());
                    return false;
                }
                ContextProps &props = m_props[spec.m_config];
                props.m_sourceProps[spec.m_source]["backend"] = sourceType.m_backend;
                props.m_sourceProps[spec.m_source]["databaseFormat"] = sourceType.m_localFormat;
                props.m_sourceProps[spec.m_source]["syncFormat"] = sourceType.m_format;
                props.m_sourceProps[spec.m_source]["forceSyncFormat"] = sourceType.m_forceFormat ? "1" : "0";
                return true;
            } else if (!prop) {
                SE_LOG_ERROR(NULL, NULL, "%s: no such property", args.c_str());
                return false;
            } else {
                string error;
                if (!prop->checkValue(paramstr, error)) {
                    SE_LOG_ERROR(NULL, NULL, "%s: %s", args.c_str(), error.c_str());
                    return false;
                } else {
                    ContextProps &props = m_props[spec.m_config];
                    if (validProps == &m_validSyncProps) {
                        // complain if sync property includes source prefix
                        if (!spec.m_source.empty()) {
                            SE_LOG_ERROR(NULL, NULL, "%s: source name '%s' not allowed in sync property",
                                         args.c_str(),
                                         spec.m_source.c_str());
                            return false;
                        }
                        props.m_syncProps[spec.m_property] = paramstr;
                    } else {
                        props.m_sourceProps[spec.m_source][spec.m_property] = paramstr;
                    }
                    return true;                        
                }
            }
        }
    }
}

bool Cmdline::listPropValues(const ConfigPropertyRegistry &validProps,
                                          const string &propName,
                                          const string &opt)
{
    const ConfigProperty *prop = validProps.find(propName);
    if (!prop && boost::iequals(propName, "type")) {
        SE_LOG_SHOW(NULL, NULL,
                    "%s\n"
                    "   <backend>[:<format>[:<version][!]]\n"
                    "   legacy property, replaced by 'backend', 'databaseFormat',\n"
                    "   'syncFormat', 'forceSyncFormat'",
                    opt.c_str());
        return true;
    } else if (!prop) {
        SE_LOG_ERROR(NULL, NULL, "%s: no such property", opt.c_str());
        return false;
    } else {
        ostringstream out;
        out << opt << endl;
        string comment = prop->getComment();

        if (comment != "") {
            list<string> commentLines;
            ConfigProperty::splitComment(comment, commentLines);
            BOOST_FOREACH(const string &line, commentLines) {
                out << "   " << line << endl;
            }
        } else {
            out << "   no documentation available" << endl;
        }
        SE_LOG_SHOW(NULL, NULL, "%s", out.str().c_str());
        return true;
    }
}

bool Cmdline::listProperties(const ConfigPropertyRegistry &validProps,
                                          const string &opt)
{
    // The first of several related properties has a comment.
    // Remember that comment and print it as late as possible,
    // that way related properties preceed their comment.
    string comment;
    bool needComma = false;
    ostringstream out;
    BOOST_FOREACH(const ConfigProperty *prop, validProps) {
        if (!prop->isHidden()) {
            string newComment = prop->getComment();

            if (newComment != "") {
                if (!comment.empty()) {
                    out << endl;
                    dumpComment(out, "   ", comment);
                    out << endl;
                    needComma = false;
                }
                comment = newComment;
            }
            std::string def = prop->getDefValue();
            if (def.empty()) {
                def = "no default";
            }
            ConfigProperty::Sharing sharing = prop->getSharing();
            if (needComma) {
                out << ", ";
            }
            out << boost::join(prop->getNames(), " = ")
                  << " (" << def << ", "
                  << ConfigProperty::sharing2str(sharing)
                  << (prop->isObligatory() ? ", required" : "")
                  << ")";
            needComma = true;
        }
    }
    out << endl;
    dumpComment(out, "   ", comment);
    SE_LOG_SHOW(NULL, NULL, "%s", out.str().c_str());
    return true;
}

static void findPeerProps(FilterConfigNode::ConfigFilter &filter,
                          ConfigPropertyRegistry &registry,
                          set<string> &peerProps)
{
    BOOST_FOREACH(StringPair entry, filter) {
        const ConfigProperty *prop = registry.find(entry.first);
        if (prop &&
            prop->getSharing() == ConfigProperty::NO_SHARING) {
            peerProps.insert(entry.first);
        }
    }
}

void Cmdline::checkForPeerProps()
{
    set<string> peerProps;

    BOOST_FOREACH(FullProps::value_type &entry, m_props) {
        ContextProps &props = entry.second;

        findPeerProps(props.m_syncProps, SyncConfig::getRegistry(), peerProps);
        BOOST_FOREACH(SourceProps::value_type &entry, props.m_sourceProps) {
            findPeerProps(entry.second, SyncSourceConfig::getRegistry(), peerProps);
        }
    }
    if (!peerProps.empty()) {
        string props = boost::join(peerProps, ", ");
        if (props == "forceSyncFormat, syncFormat") {
            // special case: these two properties might have been added by the
            // legacy "sync" property, which applies to both shared and unshared
            // properties => cannot determine that here anymore, so ignore it
        } else {
            SyncContext::throwError(string("per-peer (unshared) properties not allowed: ") +
                                    props);
        }
    }
}

void Cmdline::listSources(SyncSource &syncSource, const string &header)
{
    ostringstream out;
    out << header << ":\n";

    if (syncSource.isInactive()) {
        out << "not enabled during compilation or not usable in the current environment\n";
    } else {
        SyncSource::Databases databases = syncSource.getDatabases();

        BOOST_FOREACH(const SyncSource::Database &database, databases) {
            out << "   " << database.m_name << " (" << database.m_uri << ")";
            if (database.m_isDefault) {
                out << " <default>";
            }
            out << endl;
        }
    }
    SE_LOG_SHOW(NULL, NULL, "%s", out.str().c_str());
}

void Cmdline::dumpConfigs(const string &preamble,
                                       const SyncConfig::ConfigList &servers)
{
    ostringstream out;
    out << preamble << endl;
    BOOST_FOREACH(const SyncConfig::ConfigList::value_type &server,servers) {
        out << "   "  << server.first << " = " << server.second <<endl;
    }
    if (!servers.size()) {
        out << "   none" << endl;
    }
    SE_LOG_SHOW(NULL, NULL, "%s", out.str().c_str());
}

void Cmdline::dumpConfigTemplates(const string &preamble,
                                       const SyncConfig::TemplateList &templates,
                                       bool printRank)
{
    ostringstream out;
    out << preamble << endl;
    out << "   "  << "template name" << " = " << "template description";
    if (printRank) {
        out << "    " << "matching score in percent (100% = exact match)";
    }
    out << endl;

    BOOST_FOREACH(const SyncConfig::TemplateList::value_type server,templates) {
        out << "   "  << server->m_templateId << " = " << server->m_description;
        if (printRank){
            out << "    " << server->m_rank *20 << "%";
        }
        out << endl;
    }
    if (!templates.size()) {
        out << "   none" << endl;
    }
    SE_LOG_SHOW(NULL, NULL, "%s", out.str().c_str());
}

void Cmdline::dumpProperties(const ConfigNode &configuredProps,
                             const ConfigPropertyRegistry &allProps,
                             int flags)
{
    list<string> perPeer, perContext, global;
    ostringstream out;

    BOOST_FOREACH(const ConfigProperty *prop, allProps) {
        if (prop->isHidden() ||
            ((flags & HIDE_PER_PEER) &&
             prop->getSharing() == ConfigProperty::NO_SHARING)) {
            continue;
        }
        if (!m_quiet) {
            string comment = prop->getComment();
            if (!comment.empty()) {
                out << endl;
                dumpComment(out, "# ", comment);
            }
        }
        InitStateString value = prop->getProperty(configuredProps);
        if (!value.wasSet()) {
            out << "# ";
        }
        out << prop->getMainName() << " = " << value.get() << endl;

        list<string> *type = NULL;
        switch (prop->getSharing()) {
        case ConfigProperty::GLOBAL_SHARING:
            type = &global;
            break;
        case ConfigProperty::SOURCE_SET_SHARING:
            type = &perContext;
            break;
        case ConfigProperty::NO_SHARING:
            type = &perPeer;
            break;
        }
        if (type) {
            type->push_back(prop->getMainName());
        }
    }

    if (!m_quiet && !(flags & HIDE_LEGEND)) {
        if (!perPeer.empty() ||
            !perContext.empty() ||
            !global.empty()) {
            out << endl;
        }
        if (!perPeer.empty()) {
            out << "# per-peer (unshared) properties: " << boost::join(perPeer, ", ") << endl;
        }
        if (!perContext.empty()) {
            out << "# shared by peers in same context: " << boost::join(perContext, ", ") << endl;
        }
        if (!global.empty()) {
            out << "# global properties: " << boost::join(global, ", ") << endl;
        }
    }

    SE_LOG_SHOW(NULL, NULL, "%s", out.str().c_str());
}

void Cmdline::dumpComment(ostream &stream,
                                       const string &prefix,
                                       const string &comment)
{
    list<string> commentLines;
    ConfigProperty::splitComment(comment, commentLines);
    BOOST_FOREACH(const string &line, commentLines) {
        stream << prefix << line << endl;
    }
}

void Cmdline::usage(bool full, const string &error, const string &param)
{
    SE_LOG_SHOW(NULL, NULL, "%s", synopsis);
    if (full) {
        SE_LOG_SHOW(NULL, NULL, "\nOptions:\n%s", options);
    }

    if (error != "") {
        SE_LOG_SHOW(NULL, NULL, "");
        SE_LOG_ERROR(NULL, NULL, "%s", error.c_str());
    }
    if (param != "") {
        SE_LOG_INFO(NULL, NULL, "use '%s%s?' to get a list of valid parameters",
                    param.c_str(),
                    boost::ends_with(param, "=") ? "" : " ");
    }
}

bool Cmdline::needConfigName()
{
    if (m_server.empty()) {
        usage(false, "No configuration name specified.");
        return false;
    } else {
        return true;
    }
}


SyncContext* Cmdline::createSyncClient() {
    return new SyncContext(m_server, true);
}

#ifdef ENABLE_UNIT_TESTS

/** simple line-by-line diff */
static string diffStrings(const string &lhs, const string &rhs)
{
    ostringstream res;

    typedef boost::split_iterator<string::const_iterator> string_split_iterator;
    string_split_iterator lit =
        boost::make_split_iterator(lhs, boost::first_finder("\n", boost::is_iequal()));
    string_split_iterator rit =
        boost::make_split_iterator(rhs, boost::first_finder("\n", boost::is_iequal()));
    while (lit != string_split_iterator() &&
           rit != string_split_iterator()) {
        if (*lit != *rit) {
            res << "< " << *lit << endl;
            res << "> " << *rit << endl;
        }
        ++lit;
        ++rit;
    }

    while (lit != string_split_iterator()) {
        res << "< " << *lit << endl;
        ++lit;
    }

    while (rit != string_split_iterator()) {
        res << "> " << *rit << endl;
        ++rit;
    }

    return res.str();
}

# define CPPUNIT_ASSERT_EQUAL_DIFF( expected, actual )      \
    do { \
        string expected_ = (expected);                                  \
        string actual_ = (actual);                                      \
        if (expected_ != actual_) {                                     \
            CPPUNIT_NS::Message cpputMsg_(string("expected:\n") +       \
                                          expected_);                   \
            cpputMsg_.addDetail(string("actual:\n") +                   \
                                actual_);                               \
            cpputMsg_.addDetail(string("diff:\n") +                     \
                                diffStrings(expected_, actual_));       \
            CPPUNIT_NS::Asserter::fail( cpputMsg_,                      \
                                        CPPUNIT_SOURCELINE() );         \
        } \
    } while ( false )

// true if <word> =
static bool isPropAssignment(const string &buffer) {
    // ignore these comments (occur in type description)
    if (boost::starts_with(buffer, "KCalExtended = ") ||
        boost::starts_with(buffer, "mkcal = ") ||
        boost::starts_with(buffer, "QtContacts = ")) {
        return false;
    }
                                
    size_t start = 0;
    while (start < buffer.size() &&
           !isspace(buffer[start])) {
        start++;
    }
    if (start + 3 <= buffer.size() &&
        buffer.substr(start, 3) == " = ") {
        return true;
    } else {
        return false;
    }
}

// remove pure comment lines from buffer,
// also empty lines,
// also defaultPeer (because reference properties do not include global props)
static string filterConfig(const string &buffer)
{
    ostringstream res;

    typedef boost::split_iterator<string::const_iterator> string_split_iterator;
    for (string_split_iterator it =
             boost::make_split_iterator(buffer, boost::first_finder("\n", boost::is_iequal()));
         it != string_split_iterator();
         ++it) {
        string line = boost::copy_range<string>(*it);
        if (!line.empty() &&
            line.find("defaultPeer =") == line.npos &&
            (!boost::starts_with(line, "# ") ||
             isPropAssignment(line.substr(2)))) {
            res << line << endl;
        }
    }

    return res.str();
}

static string removeComments(const string &buffer)
{
    ostringstream res;

    typedef boost::split_iterator<string::const_iterator> string_split_iterator;
    for (string_split_iterator it =
             boost::make_split_iterator(buffer, boost::first_finder("\n", boost::is_iequal()));
         it != string_split_iterator();
         ++it) {
        string line = boost::copy_range<string>(*it);
        if (!line.empty() &&
            !boost::starts_with(line, "#")) {
            res << line << endl;
        }
    }

    return res.str();
}

// remove comment lines from scanFiles() output
static string filterFiles(const string &buffer)
{
    ostringstream res;

    typedef boost::split_iterator<string::const_iterator> string_split_iterator;
    for (string_split_iterator it =
             boost::make_split_iterator(buffer, boost::first_finder("\n", boost::is_iequal()));
         it != string_split_iterator();
         ++it) {
        string line = boost::copy_range<string>(*it);
        if (line.find(":#") == line.npos) {
            res << line;
            // do not add extra newline after last newline
            if (!line.empty() || it->end() < buffer.end()) {
                res << endl;
            }
        }
    }

    return res.str();
}


static string injectValues(const string &buffer)
{
    string res = buffer;

#if 0
    // username/password not set in templates, only in configs created
    // via the command line - not anymore, but if it ever comes back,
    // here's the place for it
    boost::replace_first(res,
                         "# username = ",
                         "username = your SyncML server account name");
    boost::replace_first(res,
                         "# password = ",
                         "password = your SyncML server password");
#endif
    return res;
}

// remove lines indented with spaces
static string filterIndented(const string &buffer)
{
    ostringstream res;
    bool first = true;

    typedef boost::split_iterator<string::const_iterator> string_split_iterator;
    for (string_split_iterator it =
             boost::make_split_iterator(buffer, boost::first_finder("\n", boost::is_iequal()));
         it != string_split_iterator();
         ++it) {
        if (!boost::starts_with(*it, " ")) {
            if (!first) {
                res << endl;
            } else {
                first = false;
            }
            res << *it;
        }
    }

    return res.str();
}

// sort lines by file, preserving order inside each line
static void sortConfig(string &config)
{
    // file name, line number, property
    typedef pair<string, pair<int, string> > line_t;
    vector<line_t> lines;
    typedef boost::split_iterator<string::iterator> string_split_iterator;
    int linenr = 0;
    for (string_split_iterator it =
             boost::make_split_iterator(config, boost::first_finder("\n", boost::is_iequal()));
         it != string_split_iterator();
         ++it, ++linenr) {
        string line(it->begin(), it->end());
        if (line.empty()) {
            continue;
        }

        size_t colon = line.find(':');
        string prefix = line.substr(0, colon);
        lines.push_back(make_pair(prefix, make_pair(linenr, line.substr(colon))));
    }

    // stable sort because of line number
    sort(lines.begin(), lines.end());

    size_t len = config.size();
    config.resize(0);
    config.reserve(len);
    BOOST_FOREACH(const line_t &line, lines) {
        config += line.first;
        config += line.second.second;
        config += "\n";
    }
}

// convert the internal config dump to .ini style (--print-config)
static string internalToIni(const string &config)
{
    ostringstream res;

    string section;
    typedef boost::split_iterator<string::const_iterator> string_split_iterator;
    for (string_split_iterator it =
             boost::make_split_iterator(config, boost::first_finder("\n", boost::is_iequal()));
         it != string_split_iterator();
         ++it) {
        string line(it->begin(), it->end());
        if (line.empty()) {
            continue;
        }

        size_t colon = line.find(':');
        string prefix = line.substr(0, colon);

        // internal values are not part of the --print-config output
        if (boost::contains(prefix, ".internal.ini") ||
            boost::contains(line, "= internal value")) {
            continue;
        }

        // --print-config also doesn't duplicate the "type" property
        // => remove the shared property
        if (boost::contains(line, ":type = ") &&
            boost::starts_with(line, "sources/")) {
            continue;
        }

        // sources/<name>/config.ini or
        // spds/sources/<name>/config.ini
        size_t endslash = prefix.rfind('/');
        if (endslash != line.npos && endslash > 1) {
            size_t slash = prefix.rfind('/', endslash - 1);
            if (slash != line.npos) {
                string newsource = prefix.substr(slash + 1, endslash - slash - 1);
                if (newsource != section &&
                    prefix.find("/sources/") != prefix.npos &&
                    newsource != "syncml") {
                    res << "[" << newsource << "]" << endl;
                    section = newsource;
                }
            }
        }
        string assignment = line.substr(colon + 1);
        // substitude aliases with generic values
        boost::replace_first(assignment, "= syncml:auth-md5", "= md5");
        boost::replace_first(assignment, "= syncml:auth-basix", "= basic");
        res << assignment << endl;
    }

    return res.str();
}

/** result of removeComments(filterRandomUUID(filterConfig())) for Google Calendar template/config */
static const std::string googlecaldav =
               "syncURL = https://www.google.com/calendar/dav/%u/user/?SyncEvolution=Google\n"
               "printChanges = 0\n"
               "dumpData = 0\n"
               "deviceId = fixed-devid\n"
               "IconURI = image://themedimage/icons/services/google-calendar\n"
               "ConsumerReady = 1\n"
               "peerType = WebDAV\n"
               "[calendar]\n"
               "sync = two-way\n"
               "backend = CalDAV\n";

/** result of removeComments(filterRandomUUID(filterConfig())) for Yahoo Calendar + Contacts */
static const std::string yahoo =
               "printChanges = 0\n"
               "dumpData = 0\n"
               "deviceId = fixed-devid\n"
               "IconURI = image://themedimage/icons/services/yahoo\n"
               "ConsumerReady = 1\n"
               "peerType = WebDAV\n"
               "[addressbook]\n"
               "sync = disabled\n"
               "backend = CardDAV\n"
               "[calendar]\n"
               "sync = two-way\n"
               "backend = CalDAV\n";

/**
 * Testing is based on a text representation of a directory
 * hierarchy where each line is of the format
 * <file path>:<line in file>
 *
 * The order of files is alphabetical, of lines in the file as
 * in the file. Lines in the file without line break cannot
 * be represented.
 *
 * The root of the hierarchy is not part of the representation
 * itself.
 */
class CmdlineTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(CmdlineTest);
    CPPUNIT_TEST(testFramework);
    CPPUNIT_TEST(testSetupScheduleWorld);
    CPPUNIT_TEST(testFutureConfig);
    CPPUNIT_TEST(testPeerConfigMigration);
    CPPUNIT_TEST(testContextConfigMigration);
    CPPUNIT_TEST(testSetupDefault);
    CPPUNIT_TEST(testSetupRenamed);
    CPPUNIT_TEST(testSetupFunambol);
    CPPUNIT_TEST(testSetupSynthesis);
    CPPUNIT_TEST(testPrintServers);
    CPPUNIT_TEST(testPrintConfig);
    CPPUNIT_TEST(testPrintFileTemplates);
    CPPUNIT_TEST(testPrintFileTemplatesConfig);
    CPPUNIT_TEST(testTemplate);
    CPPUNIT_TEST(testMatchTemplate);
    CPPUNIT_TEST(testAddSource);
    CPPUNIT_TEST(testSync);
    CPPUNIT_TEST(testWebDAV);
    CPPUNIT_TEST(testConfigure);
    CPPUNIT_TEST(testConfigureTemplates);
    CPPUNIT_TEST(testConfigureSources);
    CPPUNIT_TEST(testOldConfigure);
    CPPUNIT_TEST(testPrintDatabases);
    CPPUNIT_TEST(testMigrate);
    CPPUNIT_TEST(testMigrateContext);
    CPPUNIT_TEST(testMigrateAutoSync);
    CPPUNIT_TEST(testItemOperations);
    CPPUNIT_TEST_SUITE_END();
    
public:
    CmdlineTest() :
        m_testDir("CmdlineTest")
    {
    }

    void setUp()
    {
        rm_r(m_testDir);
        mkdir_p(m_testDir);
    }

protected:

    /** verify that createFiles/scanFiles themselves work */
    void testFramework() {
        const string root(m_testDir);
        const string content("baz:line\n"
                             "caz/subdir:booh\n"
                             "caz/subdir2/sub:# comment\n"
                             "caz/subdir2/sub:# foo = bar\n"
                             "caz/subdir2/sub:# empty = \n"
                             "caz/subdir2/sub:# another comment\n"
                             "foo:bar1\n"
                             "foo:\n"
                             "foo: \n"
                             "foo:bar2\n");
        const string filtered("baz:line\n"
                              "caz/subdir:booh\n"
                              "caz/subdir2/sub:# foo = bar\n"
                              "caz/subdir2/sub:# empty = \n"
                              "foo:bar1\n"
                              "foo: \n"
                              "foo:bar2\n");
        createFiles(root, content);
        string res = scanFiles(root);
        CPPUNIT_ASSERT_EQUAL_DIFF(filtered, res);
    }

    void removeRandomUUID(string &buffer) {
        string uuidstr = "deviceId = syncevolution-";
        size_t uuid = buffer.find(uuidstr);
        CPPUNIT_ASSERT(uuid != buffer.npos);
        size_t end = buffer.find("\n", uuid + uuidstr.size());
        CPPUNIT_ASSERT(end != buffer.npos);
        buffer.replace(uuid, end - uuid, "deviceId = fixed-devid");
    }

    string filterRandomUUID(const string &buffer) {
        string copy = buffer;
        removeRandomUUID(copy);
        return copy;
    }

    /** create new configurations */
    void testSetupScheduleWorld() { doSetupScheduleWorld(false); }
    void doSetupScheduleWorld(bool shared) {
        string root;
        ScopedEnvChange templates("SYNCEVOLUTION_TEMPLATE_DIR", "templates");
        ScopedEnvChange xdg("XDG_CONFIG_HOME", m_testDir);
        ScopedEnvChange home("HOME", m_testDir);

        root = m_testDir;
        root += "/syncevolution/default";
        string peer;
        if (shared) {
            peer = root + "/peers/scheduleworld";
        } else {
            peer = root;
        }

        {
            rm_r(peer);
            TestCmdline cmdline("--configure",
                                "--sync-property", "proxyHost = proxy",
                                "scheduleworld",
                                "addressbook",
                                NULL);
            cmdline.doit();
            string res = scanFiles(root);
            removeRandomUUID(res);
            string expected = ScheduleWorldConfig();
            sortConfig(expected);
            boost::replace_first(expected,
                                 "# proxyHost = ",
                                 "proxyHost = proxy");
            boost::replace_all(expected,
                               "sync = two-way",
                               "sync = disabled");
            boost::replace_first(expected,
                                 "addressbook/config.ini:sync = disabled",
                                 "addressbook/config.ini:sync = two-way");
            CPPUNIT_ASSERT_EQUAL_DIFF(expected, res);
        }

        {
            rm_r(peer);
            TestCmdline cmdline("--configure",
                                "--sync-property", "deviceID = fixed-devid",
                                "scheduleworld",
                                NULL);
            cmdline.doit();
            string res = scanFiles(root);
            string expected = ScheduleWorldConfig();
            sortConfig(expected);
            CPPUNIT_ASSERT_EQUAL_DIFF(expected, res);
        }
    }

    void expectTooOld() {
        bool caught = false;
        try {
            SyncConfig config("scheduleworld");
        } catch (const StatusException &ex) {
            caught = true;
            if (ex.syncMLStatus() != STATUS_RELEASE_TOO_OLD) {
                throw;
            } else {
                CPPUNIT_ASSERT_EQUAL(StringPrintf("SyncEvolution %s is too old to read configuration 'scheduleworld', please upgrade SyncEvolution.", VERSION),
                                     string(ex.what()));
            }
        }
        CPPUNIT_ASSERT(caught);
    }

    void testFutureConfig() {
        ScopedEnvChange xdg("XDG_CONFIG_HOME", m_testDir);
        ScopedEnvChange home("HOME", m_testDir);

        doSetupScheduleWorld(false);
        // bump min/cur version to something not supported, then
        // try to read => should fail
        IniFileConfigNode root(m_testDir, "/syncevolution/.internal.ini", false);
        IniFileConfigNode context(m_testDir + "/syncevolution/default", ".internal.ini", false);
        IniFileConfigNode peer(m_testDir + "/syncevolution/default/peers/scheduleworld", ".internal.ini", false);
        root.setProperty("rootMinVersion", StringPrintf("%d", CONFIG_ROOT_MIN_VERSION + 1));
        root.setProperty("rootCurVersion", StringPrintf("%d", CONFIG_ROOT_CUR_VERSION + 1));
        root.flush();
        context.setProperty("contextMinVersion", StringPrintf("%d", CONFIG_CONTEXT_MIN_VERSION + 1));
        context.setProperty("contextCurVersion", StringPrintf("%d", CONFIG_CONTEXT_CUR_VERSION + 1));
        context.flush();
        peer.setProperty("peerMinVersion", StringPrintf("%d", CONFIG_PEER_MIN_VERSION + 1));
        peer.setProperty("peerCurVersion", StringPrintf("%d", CONFIG_PEER_CUR_VERSION + 1));
        peer.flush();

        expectTooOld();

        root.setProperty("rootMinVersion", StringPrintf("%d", CONFIG_ROOT_MIN_VERSION));
        root.flush();
        expectTooOld();

        context.setProperty("contextMinVersion", StringPrintf("%d", CONFIG_CONTEXT_MIN_VERSION));
        context.flush();
        expectTooOld();

        // okay now
        peer.setProperty("peerMinVersion", StringPrintf("%d", CONFIG_PEER_MIN_VERSION));
        peer.flush();
        SyncConfig config("scheduleworld");
    }

    void expectMigration(const std::string &config) {
        bool caught = false;
        try {
            SyncConfig c(config);
            c.prepareConfigForWrite();
        } catch (const StatusException &ex) {
            caught = true;
            if (ex.syncMLStatus() != STATUS_MIGRATION_NEEDED) {
                throw;
            } else {
                CPPUNIT_ASSERT_EQUAL(StringPrintf("Proceeding would modify config '%s' such that the "
                                                  "previous SyncEvolution release will not be able to use it. "
                                                  "Stopping now. Please explicitly acknowledge this step by "
                                                  "running the following command on the command line: "
                                                  "syncevolution --migrate '%s'",
                                                  config.c_str(),
                                                  config.c_str()),
                                     string(ex.what()));
            }
        }
        CPPUNIT_ASSERT(caught);
    }

    void testPeerConfigMigration() {
        ScopedEnvChange xdg("XDG_CONFIG_HOME", m_testDir);
        ScopedEnvChange home("HOME", m_testDir);

        doSetupScheduleWorld(false);
        // decrease min/cur version to something no longer supported,
        // then try to write => should migrate in release mode and fail otherwise
        IniFileConfigNode peer(m_testDir + "/syncevolution/default/peers/scheduleworld", ".internal.ini", false);
        peer.setProperty("peerMinVersion", StringPrintf("%d", CONFIG_PEER_CUR_VERSION - 1));
        peer.setProperty("peerCurVersion", StringPrintf("%d", CONFIG_PEER_CUR_VERSION - 1));
        peer.flush();

        SyncContext::setStableRelease(false);
        expectMigration("scheduleworld");

        SyncContext::setStableRelease(true);
        {
            SyncConfig config("scheduleworld");
            config.prepareConfigForWrite();
        }
        {
            TestCmdline cmdline("--print-servers", NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("Configured servers:\n"
                                      "   scheduleworld = CmdlineTest/syncevolution/default/peers/scheduleworld\n"
                                      "   scheduleworld.old = CmdlineTest/syncevolution/default/peers/scheduleworld.old\n",
                                      cmdline.m_out.str());
        }

        // should be okay now
        SyncContext::setStableRelease(false);
        {
            SyncConfig config("scheduleworld");
            config.prepareConfigForWrite();
        }

        // do the same migration with command line
        SyncContext::setStableRelease(false);
        rm_r(m_testDir + "/syncevolution/default/peers/scheduleworld");
        CPPUNIT_ASSERT_EQUAL(0, rename((m_testDir + "/syncevolution/default/peers/scheduleworld.old").c_str(),
                                       (m_testDir + "/syncevolution/default/peers/scheduleworld").c_str()));
        {
            TestCmdline cmdline("--migrate", "scheduleworld", NULL);
            cmdline.doit();
        }
        {
            SyncConfig config("scheduleworld");
            config.prepareConfigForWrite();
        }        
        {
            TestCmdline cmdline("--print-servers", NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("Configured servers:\n"
                                      "   scheduleworld = CmdlineTest/syncevolution/default/peers/scheduleworld\n"
                                      "   scheduleworld.old = CmdlineTest/syncevolution/default/peers/scheduleworld.old\n",
                                      cmdline.m_out.str());
        }
    }

    void testContextConfigMigration() {
        ScopedEnvChange xdg("XDG_CONFIG_HOME", m_testDir);
        ScopedEnvChange home("HOME", m_testDir);

        doSetupScheduleWorld(false);
        // decrease min/cur version to something no longer supported,
        // then try to write => should migrate in release mode and fail otherwise
        IniFileConfigNode context(m_testDir + "/syncevolution/default", ".internal.ini", false);
        context.setProperty("contextMinVersion", StringPrintf("%d", CONFIG_CONTEXT_CUR_VERSION - 1));
        context.setProperty("contextCurVersion", StringPrintf("%d", CONFIG_CONTEXT_CUR_VERSION - 1));
        context.flush();

        SyncContext::setStableRelease(false);
        expectMigration("@default");

        SyncContext::setStableRelease(true);
        {
            SyncConfig config("@default");
            config.prepareConfigForWrite();
        }
        {
            TestCmdline cmdline("--print-servers", NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("Configured servers:\n"
                                      "   scheduleworld = CmdlineTest/syncevolution/default/peers/scheduleworld\n"
                                      "   scheduleworld.old@default.old = CmdlineTest/syncevolution/default.old/peers/scheduleworld.old\n",
                                      cmdline.m_out.str());
        }

        // should be okay now
        SyncContext::setStableRelease(false);
        {
            SyncConfig config("@default");
            config.prepareConfigForWrite();
        }

        // do the same migration with command line
        SyncContext::setStableRelease(false);
        rm_r(m_testDir + "/syncevolution/default");
        CPPUNIT_ASSERT_EQUAL(0, rename((m_testDir + "/syncevolution/default.old/peers/scheduleworld.old").c_str(),
                                       (m_testDir + "/syncevolution/default.old/peers/scheduleworld").c_str()));
        CPPUNIT_ASSERT_EQUAL(0, rename((m_testDir + "/syncevolution/default.old").c_str(),
                                       (m_testDir + "/syncevolution/default").c_str()));
        {
            TestCmdline cmdline("--migrate", "@default", NULL);
            cmdline.doit();
        }
        {
            SyncConfig config("@default");
            config.prepareConfigForWrite();
        }        
        {
            TestCmdline cmdline("--print-servers", NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("Configured servers:\n"
                                      "   scheduleworld = CmdlineTest/syncevolution/default/peers/scheduleworld\n"
                                      "   scheduleworld.old@default.old = CmdlineTest/syncevolution/default.old/peers/scheduleworld.old\n",
                                      cmdline.m_out.str());
        }
    }


    void testSetupDefault() {
        string root;
        ScopedEnvChange templates("SYNCEVOLUTION_TEMPLATE_DIR", "templates");
        ScopedEnvChange xdg("XDG_CONFIG_HOME", m_testDir);
        ScopedEnvChange home("HOME", m_testDir);

        root = m_testDir;
        root += "/syncevolution/default";
        TestCmdline cmdline("--configure",
                            "--template", "default",
                            "--sync-property", "deviceID = fixed-devid",
                            "some-other-server",
                            NULL);
        cmdline.doit();
        string res = scanFiles(root, "some-other-server");
        string expected = DefaultConfig();
        sortConfig(expected);
        boost::replace_all(expected, "/syncevolution/", "/some-other-server/");
        CPPUNIT_ASSERT_EQUAL_DIFF(expected, res);
    }

    void testSetupRenamed() {
        string root;
        ScopedEnvChange templates("SYNCEVOLUTION_TEMPLATE_DIR", "templates");
        ScopedEnvChange xdg("XDG_CONFIG_HOME", m_testDir);
        ScopedEnvChange home("HOME", m_testDir);

        root = m_testDir;
        root += "/syncevolution/default";
        TestCmdline cmdline("--configure",
                            "--template", "scheduleworld",
                            "--sync-property", "deviceID = fixed-devid",
                            "scheduleworld2",
                            NULL);
        cmdline.doit();
        string res = scanFiles(root, "scheduleworld2");
        string expected = ScheduleWorldConfig();
        sortConfig(expected);
        boost::replace_all(expected, "/scheduleworld/", "/scheduleworld2/");
        CPPUNIT_ASSERT_EQUAL_DIFF(expected, res);
    }

    void testSetupFunambol() { doSetupFunambol(false); }
    void doSetupFunambol(bool shared) {
        string root;
        ScopedEnvChange templates("SYNCEVOLUTION_TEMPLATE_DIR", "templates");
        ScopedEnvChange xdg("XDG_CONFIG_HOME", m_testDir);
        ScopedEnvChange home("HOME", m_testDir);

        root = m_testDir;
        root += "/syncevolution/default";
        string peer;
        if (shared) {
            peer = root + "/peers/funambol";
        } else {
            peer = root;
        }

        rm_r(peer);
        const char * const argv_fixed[] = {
                "--configure",
                "--sync-property", "deviceID = fixed-devid",
                // templates are case-insensitive
                "FunamBOL",
                NULL
        }, * const argv_shared[] = {
            "--configure",
            "FunamBOL",
            NULL
        };
        TestCmdline cmdline(shared ? argv_shared : argv_fixed);
        cmdline.doit();
        string res = scanFiles(root, "funambol");
        string expected = FunambolConfig();
        sortConfig(expected);
        CPPUNIT_ASSERT_EQUAL_DIFF(expected, res);
    }

    void testSetupSynthesis() { doSetupSynthesis(false); }
    void doSetupSynthesis(bool shared) {
        string root;
        ScopedEnvChange templates("SYNCEVOLUTION_TEMPLATE_DIR", "templates");
        ScopedEnvChange xdg("XDG_CONFIG_HOME", m_testDir);
        ScopedEnvChange home("HOME", m_testDir);

        root = m_testDir;
        root += "/syncevolution/default";
        string peer;
        if (shared) {
            peer = root + "/peers/synthesis";
        } else {
            peer = root;
        }
        rm_r(peer);
        const char * const argv_fixed[] = {
                "--configure",
                "--sync-property", "deviceID = fixed-devid",
                "synthesis",
                NULL
        }, * const argv_shared[] = {
            "--configure",
            "synthesis",
            NULL
        };
        TestCmdline cmdline(shared ? argv_shared : argv_fixed);
        cmdline.doit();
        string res = scanFiles(root, "synthesis");
        string expected = SynthesisConfig();
        sortConfig(expected);
        CPPUNIT_ASSERT_EQUAL_DIFF(expected, res);
    }

    void testTemplate() {
        ScopedEnvChange templates("SYNCEVOLUTION_TEMPLATE_DIR", "templates");
        ScopedEnvChange xdg("XDG_CONFIG_HOME", m_testDir);
        ScopedEnvChange home("HOME", m_testDir);

        TestCmdline failure("--template", NULL);

        CPPUNIT_ASSERT(!failure.m_cmdline->parse());
        CPPUNIT_ASSERT_NO_THROW(failure.expectUsageError("[ERROR] missing parameter for '--template'\n"));

        TestCmdline help("--template", "? ", NULL);
        help.doit();
        CPPUNIT_ASSERT_EQUAL_DIFF("Available configuration templates (servers):\n"
                                  "   template name = template description\n"
                                  "   eGroupware = http://www.egroupware.org\n"
                                  "   Funambol = http://my.funambol.com\n"
                                  "   Google_Calendar = event sync via CalDAV, use for the 'target-config@google-calendar' config\n"
                                  "   Google_Contacts = contact sync via SyncML, see http://www.google.com/support/mobile/bin/topic.py?topic=22181\n"
                                  "   Goosync = http://www.goosync.com/\n"
                                  "   Memotoo = http://www.memotoo.com\n"
                                  "   Mobical = https://www.everdroid.com\n"
                                  "   Oracle = http://www.oracle.com/technology/products/beehive/index.html\n"
                                  "   Ovi = http://www.ovi.com\n"
                                  "   ScheduleWorld = server no longer in operation\n"
                                  "   SyncEvolution = http://www.syncevolution.org\n"
                                  "   Synthesis = http://www.synthesis.ch\n"
                                  "   WebDAV = contact and event sync using WebDAV, use for the 'target-config@<server>' config\n"
                                  "   Yahoo = contact and event sync using WebDAV, use for the 'target-config@yahoo' config\n",
                                  help.m_out.str());
        CPPUNIT_ASSERT_EQUAL_DIFF("", help.m_err.str());
    }

    void testMatchTemplate() {
        ScopedEnvChange templates("SYNCEVOLUTION_TEMPLATE_DIR", "testcases/templates");
        ScopedEnvChange xdg("XDG_CONFIG_HOME", "/dev/null");

        TestCmdline help1("--template", "?nokia 7210c", NULL);
        help1.doit();
        CPPUNIT_ASSERT_EQUAL_DIFF("Available configuration templates (clients):\n"
                "   template name = template description    matching score in percent (100% = exact match)\n"
                "   Nokia_7210c = Template for Nokia S40 series Phone    100%\n"
                "   SyncEvolution_Client = SyncEvolution server side template    40%\n",
                help1.m_out.str());
        CPPUNIT_ASSERT_EQUAL_DIFF("", help1.m_err.str());
        TestCmdline help2("--template", "?nokia", NULL);
        help2.doit();
        CPPUNIT_ASSERT_EQUAL_DIFF("Available configuration templates (clients):\n"
                "   template name = template description    matching score in percent (100% = exact match)\n"
                "   Nokia_7210c = Template for Nokia S40 series Phone    100%\n"
                "   SyncEvolution_Client = SyncEvolution server side template    40%\n",
                help2.m_out.str());
        CPPUNIT_ASSERT_EQUAL_DIFF("", help2.m_err.str());
        TestCmdline help3("--template", "?7210c", NULL);
        help3.doit();
        CPPUNIT_ASSERT_EQUAL_DIFF("Available configuration templates (clients):\n"
                "   template name = template description    matching score in percent (100% = exact match)\n"
                "   Nokia_7210c = Template for Nokia S40 series Phone    60%\n"
                "   SyncEvolution_Client = SyncEvolution server side template    20%\n",
                help3.m_out.str());
        CPPUNIT_ASSERT_EQUAL_DIFF("", help3.m_err.str());
        TestCmdline help4("--template", "?syncevolution client", NULL);
        help4.doit();
        CPPUNIT_ASSERT_EQUAL_DIFF("Available configuration templates (clients):\n"
                "   template name = template description    matching score in percent (100% = exact match)\n"
                "   SyncEvolution_Client = SyncEvolution server side template    100%\n"
                "   Nokia_7210c = Template for Nokia S40 series Phone    40%\n",
                help4.m_out.str());
        CPPUNIT_ASSERT_EQUAL_DIFF("", help4.m_err.str());
    }

    void testPrintServers() {
        ScopedEnvChange templates("SYNCEVOLUTION_TEMPLATE_DIR", "templates");
        ScopedEnvChange xdg("XDG_CONFIG_HOME", m_testDir);
        ScopedEnvChange home("HOME", m_testDir);

        doSetupScheduleWorld(false);
        doSetupSynthesis(true);
        doSetupFunambol(true);

        TestCmdline cmdline("--print-servers", NULL);
        cmdline.doit();
        CPPUNIT_ASSERT_EQUAL_DIFF("Configured servers:\n"
                                  "   funambol = CmdlineTest/syncevolution/default/peers/funambol\n"
                                  "   scheduleworld = CmdlineTest/syncevolution/default/peers/scheduleworld\n"
                                  "   synthesis = CmdlineTest/syncevolution/default/peers/synthesis\n",
                                  cmdline.m_out.str());
        CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
    }

    void testPrintConfig() {
        ScopedEnvChange templates("SYNCEVOLUTION_TEMPLATE_DIR", "templates");
        ScopedEnvChange xdg("XDG_CONFIG_HOME", m_testDir);
        ScopedEnvChange home("HOME", m_testDir);

        testSetupFunambol();

        {
            TestCmdline failure("--print-config", NULL);
            CPPUNIT_ASSERT(failure.m_cmdline->parse());
            CPPUNIT_ASSERT(!failure.m_cmdline->run());
            CPPUNIT_ASSERT_NO_THROW(failure.expectUsageError("[ERROR] --print-config requires either a --template or a server name.\n"));
        }

        {
            TestCmdline failure("--print-config", "foo", NULL);
            CPPUNIT_ASSERT(failure.m_cmdline->parse());
            CPPUNIT_ASSERT(!failure.m_cmdline->run());
            CPPUNIT_ASSERT_EQUAL_DIFF("", failure.m_out.str());
            CPPUNIT_ASSERT_EQUAL(string("[ERROR] Server 'foo' has not been configured yet.\n"),
                                 failure.m_err.str());
        }

        {
            TestCmdline failure("--print-config", "--template", "foo", NULL);
            CPPUNIT_ASSERT(failure.m_cmdline->parse());
            CPPUNIT_ASSERT(!failure.m_cmdline->run());
            CPPUNIT_ASSERT_EQUAL_DIFF("", failure.m_out.str());
            CPPUNIT_ASSERT_EQUAL(string("[ERROR] No configuration template for 'foo' available.\n"),
                                 failure.m_err.str());
        }

        {
            TestCmdline cmdline("--print-config", "--template", "scheduleworld", NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
            string actual = cmdline.m_out.str();
            // deviceId must be the one from Funambol
            CPPUNIT_ASSERT(boost::contains(actual, "deviceId = fixed-devid"));
            string filtered = injectValues(filterConfig(actual));
            CPPUNIT_ASSERT_EQUAL_DIFF(filterConfig(internalToIni(ScheduleWorldConfig())),
                                      filtered);
            // there should have been comments
            CPPUNIT_ASSERT(actual.size() > filtered.size());
        }

        {
            TestCmdline cmdline("--print-config", "--template", "scheduleworld@nosuchcontext", NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
            string actual = cmdline.m_out.str();
            // deviceId must *not* be the one from Funambol because of the new context
            CPPUNIT_ASSERT(!boost::contains(actual, "deviceId = fixed-devid"));
        }

        {
            TestCmdline cmdline("--print-config", "--template", "Default", NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
            string actual = injectValues(filterConfig(cmdline.m_out.str()));
            CPPUNIT_ASSERT(boost::contains(actual, "deviceId = fixed-devid"));
            CPPUNIT_ASSERT_EQUAL_DIFF(filterConfig(internalToIni(DefaultConfig())),
                                      actual);
        }

        {
            TestCmdline cmdline("--print-config", "funambol", NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
            CPPUNIT_ASSERT_EQUAL_DIFF(filterConfig(internalToIni(FunambolConfig())),
                                      injectValues(filterConfig(cmdline.m_out.str())));
        }

        {
            // override context and template properties
            TestCmdline cmdline("--print-config", "--template", "scheduleworld",
                                "syncURL=foo",
                                "database=Personal",
                                "--source-property", "sync=disabled",
                                NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
            string expected = filterConfig(internalToIni(ScheduleWorldConfig()));
            boost::replace_first(expected,
                                 "syncURL = http://sync.scheduleworld.com/funambol/ds",
                                 "syncURL = foo");
            boost::replace_all(expected,
                               "# database = ",
                               "database = Personal");
            boost::replace_all(expected,
                               "sync = two-way",
                               "sync = disabled");
            string actual = injectValues(filterConfig(cmdline.m_out.str()));
            CPPUNIT_ASSERT(boost::contains(actual, "deviceId = fixed-devid"));
            CPPUNIT_ASSERT_EQUAL_DIFF(expected,
                                      actual);
        }

        {
            // override context and template properties, using legacy property name
            TestCmdline cmdline("--print-config", "--template", "scheduleworld",
                                "--sync-property", "syncURL=foo",
                                "--source-property", "evolutionsource=Personal",
                                "--source-property", "sync=disabled",
                                NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
            string expected = filterConfig(internalToIni(ScheduleWorldConfig()));
            boost::replace_first(expected,
                                 "syncURL = http://sync.scheduleworld.com/funambol/ds",
                                 "syncURL = foo");
            boost::replace_all(expected,
                               "# database = ",
                               "database = Personal");
            boost::replace_all(expected,
                               "sync = two-way",
                               "sync = disabled");
            string actual = injectValues(filterConfig(cmdline.m_out.str()));
            CPPUNIT_ASSERT(boost::contains(actual, "deviceId = fixed-devid"));
            CPPUNIT_ASSERT_EQUAL_DIFF(expected,
                                      actual);
        }

        {
            TestCmdline cmdline("--print-config", "--quiet",
                                "--template", "scheduleworld",
                                "funambol",
                                NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
            string actual = cmdline.m_out.str();
            CPPUNIT_ASSERT(boost::contains(actual, "deviceId = fixed-devid"));
            CPPUNIT_ASSERT_EQUAL_DIFF(internalToIni(ScheduleWorldConfig()),
                                      injectValues(filterConfig(actual)));
        }

        {
            // change shared source properties, then check template again
            TestCmdline cmdline("--configure",
                                "--source-property", "database=Personal",
                                "funambol",
                                NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
        }
        {
            TestCmdline cmdline("--print-config", "--quiet",
                                "--template", "scheduleworld",
                                "funambol",
                                NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
            string expected = filterConfig(internalToIni(ScheduleWorldConfig()));
            // from modified Funambol config
            boost::replace_all(expected,
                               "# database = ",
                               "database = Personal");
            string actual = injectValues(filterConfig(cmdline.m_out.str()));
            CPPUNIT_ASSERT(boost::contains(actual, "deviceId = fixed-devid"));
            CPPUNIT_ASSERT_EQUAL_DIFF(expected,
                                      actual);
        }

        {
            // print config => must not use settings from default context
            TestCmdline cmdline("--print-config", "--template", "scheduleworld@nosuchcontext", NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
            // source settings *not* from modified Funambol config
            string expected = filterConfig(internalToIni(ScheduleWorldConfig()));
            string actual = injectValues(filterConfig(cmdline.m_out.str()));
            CPPUNIT_ASSERT(!boost::contains(actual, "deviceId = fixed-devid"));
            removeRandomUUID(actual);
            CPPUNIT_ASSERT_EQUAL_DIFF(expected,
                                      actual);
        }

        {
            // create config => again, must not use settings from default context
            TestCmdline cmdline("--configure", "--template", "scheduleworld", "other@other", NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
        }
        {
            TestCmdline cmdline("--print-config", "other@other", NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
            // source settings *not* from modified Funambol config
            string expected = filterConfig(internalToIni(ScheduleWorldConfig()));
            string actual = injectValues(filterConfig(cmdline.m_out.str()));
            CPPUNIT_ASSERT(!boost::contains(actual, "deviceId = fixed-devid"));
            removeRandomUUID(actual);
            CPPUNIT_ASSERT_EQUAL_DIFF(expected,
                                      actual);
        }
    }

    void testPrintFileTemplates() {
        // use local copy of templates in build dir (no need to install)
        ScopedEnvChange templates("SYNCEVOLUTION_TEMPLATE_DIR", "./templates");
        ScopedEnvChange xdg("XDG_CONFIG_HOME", m_testDir);
        ScopedEnvChange home("HOME", m_testDir);

        doPrintFileTemplates();
    }

    void testPrintFileTemplatesConfig() {
        // simulate reading templates from user's XDG HOME
        symlink("../templates", (m_testDir + "/syncevolution-templates").c_str());
        ScopedEnvChange templates("SYNCEVOLUTION_TEMPLATE_DIR", "/dev/null");
        ScopedEnvChange xdg("XDG_CONFIG_HOME", m_testDir);
        ScopedEnvChange home("HOME", m_testDir);

        doPrintFileTemplates();
    }

    void doPrintFileTemplates() {
        // Compare only the properties which are really set.
        //
        // note that "backend" will be take from the @default context if one
        // exists, so run this before setting up Funambol below
        {
            TestCmdline cmdline("--print-config", "--template", "google calendar", NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF(googlecaldav,
                                      removeComments(filterRandomUUID(filterConfig(cmdline.m_out.str()))));
        }

        {
            TestCmdline cmdline("--print-config", "--template", "yahoo", NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF(yahoo,
                                      removeComments(filterRandomUUID(filterConfig(cmdline.m_out.str()))));
        }

        testSetupFunambol();

        {
            TestCmdline cmdline("--print-config", "--template", "scheduleworld", NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
            string actual = cmdline.m_out.str();
            // deviceId must be the one from Funambol
            CPPUNIT_ASSERT(boost::contains(actual, "deviceId = fixed-devid"));
            string filtered = injectValues(filterConfig(actual));
            CPPUNIT_ASSERT_EQUAL_DIFF(filterConfig(internalToIni(ScheduleWorldConfig())),
                                      filtered);
            // there should have been comments
            CPPUNIT_ASSERT(actual.size() > filtered.size());
        }

        {
            TestCmdline cmdline("--print-config", "funambol", NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
            CPPUNIT_ASSERT_EQUAL_DIFF(filterConfig(internalToIni(FunambolConfig())),
                                      injectValues(filterConfig(cmdline.m_out.str())));
        }
    }

    void testAddSource() {
        string root;
        ScopedEnvChange templates("SYNCEVOLUTION_TEMPLATE_DIR", "templates");
        ScopedEnvChange xdg("XDG_CONFIG_HOME", m_testDir);
        ScopedEnvChange home("HOME", m_testDir);

        testSetupScheduleWorld();

        root = m_testDir;
        root += "/syncevolution/default";

        {
            TestCmdline cmdline("--configure",
                                "--source-property", "uri = dummy",
                                "scheduleworld",
                                "xyz",
                                NULL);
            cmdline.doit();
            string res = scanFiles(root);
            string expected = ScheduleWorldConfig();
            expected += "\n"
                "peers/scheduleworld/sources/xyz/.internal.ini:# adminData = \n"
                "peers/scheduleworld/sources/xyz/.internal.ini:# synthesisID = 0\n"
                "peers/scheduleworld/sources/xyz/config.ini:# sync = disabled\n"
                "peers/scheduleworld/sources/xyz/config.ini:uri = dummy\n"
                "peers/scheduleworld/sources/xyz/config.ini:# syncFormat = \n"
                "peers/scheduleworld/sources/xyz/config.ini:# forceSyncFormat = 0\n"
                "sources/xyz/config.ini:# backend = select backend\n"
                "sources/xyz/config.ini:# database = \n"
                "sources/xyz/config.ini:# databaseFormat = \n"
                "sources/xyz/config.ini:# databaseUser = \n"
                "sources/xyz/config.ini:# databasePassword = ";
            sortConfig(expected);
            CPPUNIT_ASSERT_EQUAL_DIFF(expected, res);
        }
    }

    void testSync() {
        TestCmdline failure("--sync", NULL);
        CPPUNIT_ASSERT(!failure.m_cmdline->parse());
        CPPUNIT_ASSERT_NO_THROW(failure.expectUsageError("[ERROR] missing parameter for '--sync'\n"));

        TestCmdline failure2("--sync", "foo", NULL);
        CPPUNIT_ASSERT(!failure2.m_cmdline->parse());
        CPPUNIT_ASSERT_EQUAL_DIFF("", failure2.m_out.str());
        CPPUNIT_ASSERT_EQUAL_DIFF("[ERROR] '--sync foo': not one of the valid values (two-way, slow, refresh-from-local, refresh-from-remote = refresh, one-way-from-local, one-way-from-remote = one-way, refresh-from-client = refresh-client, refresh-from-server = refresh-server, one-way-from-client = one-way-client, one-way-from-server = one-way-server, disabled = none)\n", failure2.m_err.str());

        TestCmdline help("--sync", " ?", NULL);
        help.doit();
        CPPUNIT_ASSERT_EQUAL_DIFF("--sync\n"
                                  "   Requests a certain synchronization mode when initiating a sync:\n"
                                  "   \n"
                                  "     two-way\n"
                                  "       only send/receive changes since last sync\n"
                                  "     slow\n"
                                  "       exchange all items\n"
                                  "     refresh-from-remote\n"
                                  "       discard all local items and replace with\n"
                                  "       the items on the peer\n"
                                  "     refresh-from-local\n"
                                  "       discard all items on the peer and replace\n"
                                  "       with the local items\n"
                                  "     one-way-from-remote\n"
                                  "       transmit changes from peer\n"
                                  "     one-way-from-local\n"
                                  "       transmit local changes\n"
                                  "     disabled (or none)\n"
                                  "       synchronization disabled\n"
                                  "   \n"
                                  "   refresh/one-way-from-server/client are also supported. Their use is\n"
                                  "   discouraged because the direction of the data transfer depends\n"
                                  "   on the role of the local side (can be server or client), which is\n"
                                  "   not always obvious.\n"
                                  "   \n"
                                  "   When accepting a sync session in a SyncML server (HTTP server), only\n"
                                  "   sources with sync != disabled are made available to the client,\n"
                                  "   which chooses the final sync mode based on its own configuration.\n"
                                  "   When accepting a sync session in a SyncML client (local sync with\n"
                                  "   the server contacting SyncEvolution on a device), the sync mode\n"
                                  "   specified in the client is typically overriden by the server.\n",
                                  help.m_out.str());
        CPPUNIT_ASSERT_EQUAL_DIFF("", help.m_err.str());

        TestCmdline filter("--sync", "refresh-from-server", NULL);
        CPPUNIT_ASSERT(filter.m_cmdline->parse());
        CPPUNIT_ASSERT(!filter.m_cmdline->run());
        CPPUNIT_ASSERT_NO_THROW(filter.expectUsageError("[ERROR] No configuration name specified.\n"));
        CPPUNIT_ASSERT_EQUAL_DIFF("sync = refresh-from-server",
                                  string(filter.m_cmdline->m_props[""].m_sourceProps[""]));
        CPPUNIT_ASSERT_EQUAL_DIFF("",                                  string(filter.m_cmdline->m_props[""].m_syncProps));

        TestCmdline filter2("--source-property", "sync=refresh", NULL);
        CPPUNIT_ASSERT(filter2.m_cmdline->parse());
        CPPUNIT_ASSERT(!filter2.m_cmdline->run());
        CPPUNIT_ASSERT_NO_THROW(filter2.expectUsageError("[ERROR] No configuration name specified.\n"));
        CPPUNIT_ASSERT_EQUAL_DIFF("sync = refresh",
                                  string(filter2.m_cmdline->m_props[""].m_sourceProps[""]));
        CPPUNIT_ASSERT_EQUAL_DIFF("",
                                  string(filter2.m_cmdline->m_props[""].m_syncProps));

        TestCmdline filter3("--source-property", "xyz=1", NULL);
        CPPUNIT_ASSERT(!filter3.m_cmdline->parse());
        CPPUNIT_ASSERT_EQUAL(string(""), filter3.m_out.str());
        CPPUNIT_ASSERT_EQUAL(string("[ERROR] '--source-property xyz=1': no such property\n"), filter3.m_err.str());

        TestCmdline filter4("xyz=1", NULL);
        CPPUNIT_ASSERT(!filter4.m_cmdline->parse());
        CPPUNIT_ASSERT_NO_THROW(filter4.expectUsageError("[ERROR] unrecognized property in 'xyz=1'\n"));

        TestCmdline filter5("=1", NULL);
        CPPUNIT_ASSERT(!filter5.m_cmdline->parse());
        CPPUNIT_ASSERT_NO_THROW(filter5.expectUsageError("[ERROR] a property name must be given in '=1'\n"));
    }

    void testWebDAV() {
#ifdef ENABLE_DAV
        ScopedEnvChange templates("SYNCEVOLUTION_TEMPLATE_DIR", "templates");
        ScopedEnvChange xdg("XDG_CONFIG_HOME", m_testDir);
        ScopedEnvChange home("HOME", m_testDir);

        // configure Yahoo under a different name, with explicit template selection
        {
            TestCmdline cmdline("--configure",
                                "--template", "yahoo",
                                "target-config@my-yahoo",
                                NULL);
            cmdline.doit();
        }
        {
            TestCmdline cmdline("--print-config", "target-config@my-yahoo", NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF(yahoo,
                                      removeComments(filterRandomUUID(filterConfig(cmdline.m_out.str()))));
        }

        // configure Google Calendar with template derived from config name
        {
            TestCmdline cmdline("--configure",
                                "target-config@google-calendar",
                                NULL);
            cmdline.doit();
        }
        {
            TestCmdline cmdline("--print-config", "target-config@google-calendar", NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF(googlecaldav,
                                      removeComments(filterRandomUUID(filterConfig(cmdline.m_out.str()))));
        }

        // test "template not found" error cases
        {
            TestCmdline cmdline("--configure",
                                "--template", "yahooxyz",
                                "target-config@my-yahoo-xyz",
                                NULL);
            CPPUNIT_ASSERT(cmdline.m_cmdline->parse());
            CPPUNIT_ASSERT(!cmdline.m_cmdline->run());
            static const char error[] = "[ERROR] No configuration template for 'yahooxyz' available.\n";
            static const char hint[] = "\nAvailable configuration templates (clients and servers):\n";
            std::string out = cmdline.m_out.str();
            std::string err = cmdline.m_err.str();
            std::string all = cmdline.m_all.str();
            CPPUNIT_ASSERT(boost::starts_with(out, hint));
            CPPUNIT_ASSERT(boost::ends_with(out, "\n"));
            CPPUNIT_ASSERT(!boost::ends_with(out, "\n\n"));
            CPPUNIT_ASSERT_EQUAL(string(error),
                                 err);
            CPPUNIT_ASSERT(boost::starts_with(all, string(error) + hint));
            CPPUNIT_ASSERT(boost::ends_with(all, "\n"));
            CPPUNIT_ASSERT(!boost::ends_with(all, "\n\n"));
        }
        {
            TestCmdline cmdline("--configure",
                                "target-config@foobar",
                                NULL);
            CPPUNIT_ASSERT(cmdline.m_cmdline->parse());
            CPPUNIT_ASSERT(!cmdline.m_cmdline->run());
            static const char error[] = "[ERROR] No configuration template for 'foobar' available.\n";
            static const char hint[] = "[INFO] Use '--template none' and/or specify relevant properties on the command line to create a configuration without a template. Need values for: syncURL\n\nAvailable configuration templates (clients and servers):\n";
            std::string out = cmdline.m_out.str();
            std::string err = cmdline.m_err.str();
            std::string all = cmdline.m_all.str();
            CPPUNIT_ASSERT(boost::starts_with(out, hint));
            CPPUNIT_ASSERT(boost::ends_with(out, "\n"));
            CPPUNIT_ASSERT(!boost::ends_with(out, "\n\n"));
            CPPUNIT_ASSERT_EQUAL(string(error),
                                 err);
            CPPUNIT_ASSERT(boost::starts_with(all, string(error) + hint));
            CPPUNIT_ASSERT(boost::ends_with(all, "\n"));
            CPPUNIT_ASSERT(!boost::ends_with(all, "\n\n"));
        }
#endif
    }

    void testConfigure() {
        ScopedEnvChange templates("SYNCEVOLUTION_TEMPLATE_DIR", "templates");
        ScopedEnvChange xdg("XDG_CONFIG_HOME", m_testDir);
        ScopedEnvChange home("HOME", m_testDir);

        testSetupScheduleWorld();
        string expected = doConfigure(ScheduleWorldConfig(), "sources/addressbook/config.ini:");

        {
            // updating "type" for peer is mapped to updating "backend",
            // "databaseFormat", "syncFormat", "forceSyncFormat"
            TestCmdline cmdline("--configure",
                                "--source-property", "addressbook/type=file:text/vcard:3.0",
                                "scheduleworld",
                                NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_out.str());
            boost::replace_first(expected,
                                 "backend = addressbook",
                                 "backend = file");
            boost::replace_first(expected,
                                 "# databaseFormat = ",
                                 "databaseFormat = text/vcard");
            boost::replace_first(expected,
                                 "# forceSyncFormat = 0",
                                 "forceSyncFormat = 0");
            CPPUNIT_ASSERT_EQUAL_DIFF(expected,
                                      filterConfig(printConfig("scheduleworld")));
            string shared = filterConfig(printConfig("@default"));
            CPPUNIT_ASSERT(shared.find("backend = file") != shared.npos);
            CPPUNIT_ASSERT(shared.find("databaseFormat = text/vcard") != shared.npos);
        }

        {
            // updating type for context must not affect peer
            TestCmdline cmdline("--configure",
                                "--source-property", "type=file:text/x-vcard:2.1",
                                "@default", "addressbook",
                                NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_out.str());
            boost::replace_first(expected,
                                 "databaseFormat = text/vcard",
                                 "databaseFormat = text/x-vcard");
            CPPUNIT_ASSERT_EQUAL_DIFF(expected,
                                      filterConfig(printConfig("scheduleworld")));
            string shared = filterConfig(printConfig("@default"));
            CPPUNIT_ASSERT(shared.find("backend = file") != shared.npos);
            CPPUNIT_ASSERT(shared.find("databaseFormat = text/x-vcard") != shared.npos);
        }

        string syncProperties("syncURL (no default, unshared, required)\n"
                              "\n"
                              "username (no default, unshared)\n"
                              "\n"
                              "password (no default, unshared)\n"
                              "\n"
                              "logdir (no default, shared)\n"
                              "\n"
                              "loglevel (0, unshared)\n"
                              "\n"
                              "printChanges (TRUE, unshared)\n"
                              "\n"
                              "dumpData (TRUE, unshared)\n"
                              "\n"
                              "maxlogdirs (10, shared)\n"
                              "\n"
                              "autoSync (0, unshared)\n"
                              "\n"
                              "autoSyncInterval (30M, unshared)\n"
                              "\n"
                              "autoSyncDelay (5M, unshared)\n"
                              "\n"
                              "preventSlowSync (TRUE, unshared)\n"
                              "\n"
                              "useProxy (FALSE, unshared)\n"
                              "\n"
                              "proxyHost (no default, unshared)\n"
                              "\n"
                              "proxyUsername (no default, unshared)\n"
                              "\n"
                              "proxyPassword (no default, unshared)\n"
                              "\n"
                              "clientAuthType (md5, unshared)\n"
                              "\n"
                              "RetryDuration (5M, unshared)\n"
                              "\n"
                              "RetryInterval (2M, unshared)\n"
                              "\n"
                              "remoteIdentifier (no default, unshared)\n"
                              "\n"
                              "PeerIsClient (FALSE, unshared)\n"
                              "\n"
                              "SyncMLVersion (no default, unshared)\n"
                              "\n"
                              "PeerName (no default, unshared)\n"
                              "\n"
                              "deviceId (no default, shared)\n"
                              "\n"
                              "remoteDeviceId (no default, unshared)\n"
                              "\n"
                              "enableWBXML (TRUE, unshared)\n"
                              "\n"
                              "maxMsgSize (150000, unshared), maxObjSize (4000000, unshared)\n"
                              "\n"
                              "SSLServerCertificates (" SYNCEVOLUTION_SSL_SERVER_CERTIFICATES ", unshared)\n"
                              "\n"
                              "SSLVerifyServer (TRUE, unshared)\n"
                              "\n"
                              "SSLVerifyHost (TRUE, unshared)\n"
                              "\n"
                              "WebURL (no default, unshared)\n"
                              "\n"
                              "IconURI (no default, unshared)\n"
                              "\n"
                              "ConsumerReady (FALSE, unshared)\n"
                              "\n"
                              "peerType (no default, unshared)\n"
                              "\n"
                              "defaultPeer (no default, global)\n");

        string sourceProperties("sync (disabled, unshared, required)\n"
                                "\n"
                                "uri (no default, unshared)\n"
                                "\n"
                                "backend (select backend, shared)\n"
                                "\n"
                                "syncFormat (no default, unshared)\n"
                                "\n"
                                "forceSyncFormat (FALSE, unshared)\n"
                                "\n"
                                "database = evolutionsource (no default, shared)\n"
                                "\n"
                                "databaseFormat (no default, shared)\n"
                                "\n"
                                "databaseUser = evolutionuser (no default, shared), databasePassword = evolutionpassword (no default, shared)\n");

        {
            TestCmdline cmdline("--sync-property", "?",
                                NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
            CPPUNIT_ASSERT_EQUAL_DIFF(syncProperties,
                                      filterIndented(cmdline.m_out.str()));
        }

        {
            TestCmdline cmdline("--source-property", "?",
                                NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
            CPPUNIT_ASSERT_EQUAL_DIFF(sourceProperties,
                                      filterIndented(cmdline.m_out.str()));
        }

        {
            TestCmdline cmdline("--source-property", "?",
                                "--sync-property", "?",
                                NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
            CPPUNIT_ASSERT_EQUAL_DIFF(sourceProperties + syncProperties,
                                      filterIndented(cmdline.m_out.str()));
        }

        {
            TestCmdline cmdline("--sync-property", "?",
                                "--source-property", "?",
                                NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
            CPPUNIT_ASSERT_EQUAL_DIFF(syncProperties + sourceProperties,
                                      filterIndented(cmdline.m_out.str()));
        }

        {
            TestCmdline cmdline("--source-property", "sync=?",
                                NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
            CPPUNIT_ASSERT_EQUAL_DIFF("'--source-property sync=?'\n",
                                      filterIndented(cmdline.m_out.str()));
        }

        {
            TestCmdline cmdline("sync=?",
                                NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
            CPPUNIT_ASSERT_EQUAL_DIFF("'sync=?'\n",
                                      filterIndented(cmdline.m_out.str()));
        }

        {
            TestCmdline cmdline("syncURL=?",
                                NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
            CPPUNIT_ASSERT_EQUAL_DIFF("'syncURL=?'\n",
                                      filterIndented(cmdline.m_out.str()));
        }
    }

    /**
     * Test semantic of config creation (instead of updating) with and without
     * templates. See BMC #14805.
     */
    void testConfigureTemplates() {
        ScopedEnvChange templates("SYNCEVOLUTION_TEMPLATE_DIR", "templates");
        ScopedEnvChange xdg("XDG_CONFIG_HOME", m_testDir);
        ScopedEnvChange home("HOME", m_testDir);

        rm_r(m_testDir);
        {
            // catch possible typos like "sheduleworld"
            TestCmdline failure("--configure", "foo", NULL);
            CPPUNIT_ASSERT(failure.m_cmdline->parse());
            CPPUNIT_ASSERT(!failure.m_cmdline->run());
            static const char error[] = "[ERROR] No configuration template for 'foo@default' available.\n";
            static const char hint[] = "[INFO] Use '--template none' and/or specify relevant properties on the command line to create a configuration without a template. Need values for: syncURL\n\nAvailable configuration templates (clients and servers):\n";
            std::string out = failure.m_out.str();
            std::string err = failure.m_err.str();
            std::string all = failure.m_all.str();
            CPPUNIT_ASSERT(boost::starts_with(out, hint));
            CPPUNIT_ASSERT(boost::ends_with(out, "\n"));
            CPPUNIT_ASSERT(!boost::ends_with(out, "\n\n"));
            CPPUNIT_ASSERT_EQUAL(string(error),
                                 err);
            CPPUNIT_ASSERT(boost::starts_with(all, string(error) + hint));
            CPPUNIT_ASSERT(boost::ends_with(all, "\n"));
            CPPUNIT_ASSERT(!boost::ends_with(all, "\n\n"));
        }

        rm_r(m_testDir);
        {
            // catch possible typos like "sheduleworld" when
            // enough properties are specified to continue without
            // a template
            TestCmdline failure("--configure", "syncURL=http://foo.com", "--template", "foo", "bar", NULL);
            CPPUNIT_ASSERT(failure.m_cmdline->parse());
            CPPUNIT_ASSERT(!failure.m_cmdline->run());

            static const char error[] = "[ERROR] No configuration template for 'foo' available.\n";
            static const char hint[] = "[INFO] All relevant properties seem to be set, omit the --template parameter to proceed.\n\nAvailable configuration templates (clients and servers):\n";
            std::string out = failure.m_out.str();
            std::string err = failure.m_err.str();
            std::string all = failure.m_all.str();
            CPPUNIT_ASSERT(boost::starts_with(out, hint));
            CPPUNIT_ASSERT(boost::ends_with(out, "\n"));
            CPPUNIT_ASSERT(!boost::ends_with(out, "\n\n"));
            CPPUNIT_ASSERT_EQUAL(string(error),
                                 err);
            CPPUNIT_ASSERT(boost::starts_with(all, string(error) + hint));
            CPPUNIT_ASSERT(boost::ends_with(all, "\n"));
            CPPUNIT_ASSERT(!boost::ends_with(all, "\n\n"));
        }

        string fooconfig =
            StringPrintf("syncevolution/.internal.ini:rootMinVersion = %d\n"
                         "syncevolution/.internal.ini:rootCurVersion = %d\n"
                         "syncevolution/default/.internal.ini:contextMinVersion = %d\n"
                         "syncevolution/default/.internal.ini:contextCurVersion = %d\n"
                         "syncevolution/default/config.ini:deviceId = fixed-devid\n"
                         "syncevolution/default/peers/foo/.internal.ini:peerMinVersion = %d\n"
                         "syncevolution/default/peers/foo/.internal.ini:peerCurVersion = %d\n",
                         CONFIG_ROOT_MIN_VERSION, CONFIG_ROOT_CUR_VERSION,
                         CONFIG_CONTEXT_MIN_VERSION, CONFIG_CONTEXT_CUR_VERSION,
                         CONFIG_PEER_MIN_VERSION, CONFIG_PEER_CUR_VERSION);

        string syncurl =
            "syncevolution/default/peers/foo/config.ini:syncURL = local://@bar\n";

        string configsource =
            "syncevolution/default/peers/foo/sources/eds_event/config.ini:sync = two-way\n"
            "syncevolution/default/sources/eds_event/config.ini:backend = calendar\n";

        rm_r(m_testDir);
        {
            // allow user to proceed if they wish: should result in no sources configured
            TestCmdline failure("--configure", "--template", "none", "foo", NULL);
            CPPUNIT_ASSERT(failure.m_cmdline->parse());
            bool success  = failure.m_cmdline->run();
            CPPUNIT_ASSERT_EQUAL_DIFF("", failure.m_out.str());
            CPPUNIT_ASSERT_EQUAL_DIFF("", failure.m_err.str());
            CPPUNIT_ASSERT(success);
            string res = scanFiles(m_testDir);
            removeRandomUUID(res);
            CPPUNIT_ASSERT_EQUAL_DIFF(fooconfig, filterFiles(res));
        }

        rm_r(m_testDir);
        {
            // allow user to proceed if they wish: should result in no sources configured,
            // even if general source properties are specified
            TestCmdline failure("--configure", "--template", "none", "backend=calendar", "foo", NULL);
            bool success = failure.m_cmdline->parse() && failure.m_cmdline->run();
            CPPUNIT_ASSERT_EQUAL_DIFF("", failure.m_out.str());
            CPPUNIT_ASSERT_EQUAL_DIFF("", failure.m_err.str());
            CPPUNIT_ASSERT(success);
            string res = scanFiles(m_testDir);
            removeRandomUUID(res);
            CPPUNIT_ASSERT_EQUAL_DIFF(fooconfig, filterFiles(res));
        }

        rm_r(m_testDir);
        {
            // allow user to proceed if they wish: should result in no sources configured,
            // even if specific source properties are specified
            TestCmdline failure("--configure", "--template", "none", "eds_event/backend=calendar", "foo", NULL);
            bool success = failure.m_cmdline->parse() && failure.m_cmdline->run();
            CPPUNIT_ASSERT_EQUAL_DIFF("", failure.m_out.str());
            CPPUNIT_ASSERT_EQUAL_DIFF("", failure.m_err.str());
            CPPUNIT_ASSERT(success);
            string res = scanFiles(m_testDir);
            removeRandomUUID(res);
            CPPUNIT_ASSERT_EQUAL_DIFF(fooconfig, filterFiles(res));
        }

        rm_r(m_testDir);
        {
            // allow user to proceed if they wish and possible: here eds_event is not usable
            TestCmdline failure("--configure", "--template", "none", "foo", "eds_event", NULL);
            CPPUNIT_ASSERT(failure.m_cmdline->parse());
            bool caught = false;
            try {
                CPPUNIT_ASSERT(failure.m_cmdline->run());
            } catch (const StatusException &ex) {
                if (!strcmp(ex.what(), "eds_event: no backend available")) {
                    caught = true;
                } else {
                    throw;
                }
            }
            CPPUNIT_ASSERT(caught);
        }

        rm_r(m_testDir);
        {
            // allow user to proceed if they wish and possible: here eds_event is not configurable
            TestCmdline failure("--configure", "syncURL=local://@bar", "foo", "eds_event", NULL);
            CPPUNIT_ASSERT(failure.m_cmdline->parse());
            bool caught = false;
            try {
                CPPUNIT_ASSERT(failure.m_cmdline->run());
            } catch (const StatusException &ex) {
                if (!strcmp(ex.what(), "no such source(s): eds_event")) {
                    caught = true;
                } else {
                    throw;
                }
            }
            CPPUNIT_ASSERT(caught);
        }

        rm_r(m_testDir);
        {
            // allow user to proceed if they wish and possible: here eds_event is not configurable (wrong context)
            TestCmdline failure("--configure", "syncURL=local://@bar", "eds_event/backend@xyz=calendar", "foo", "eds_event", NULL);
            CPPUNIT_ASSERT(failure.m_cmdline->parse());
            bool caught = false;
            try {
                CPPUNIT_ASSERT(failure.m_cmdline->run());
            } catch (const StatusException &ex) {
                if (!strcmp(ex.what(), "no such source(s): eds_event")) {
                    caught = true;
                } else {
                    throw;
                }
            }
            CPPUNIT_ASSERT(caught);
        }

        rm_r(m_testDir);
        {
            // allow user to proceed if they wish: configure exactly the specified sources
            TestCmdline failure("--configure", "--template", "none", "backend=calendar", "foo", "eds_event", NULL);
            CPPUNIT_ASSERT(failure.m_cmdline->parse());
            CPPUNIT_ASSERT(failure.m_cmdline->run());
            CPPUNIT_ASSERT_EQUAL_DIFF("", failure.m_out.str());
            CPPUNIT_ASSERT_EQUAL_DIFF("", failure.m_err.str());

            string res = scanFiles(m_testDir);
            removeRandomUUID(res);
            CPPUNIT_ASSERT_EQUAL_DIFF(fooconfig + configsource, filterFiles(res));
        }

        rm_r(m_testDir);
        {
            // allow user to proceed if they provide enough information: should result in no sources configured
            TestCmdline failure("--configure", "syncURL=local://@bar", "foo", NULL);
            CPPUNIT_ASSERT(failure.m_cmdline->parse());
            CPPUNIT_ASSERT(failure.m_cmdline->run());
            CPPUNIT_ASSERT_EQUAL_DIFF("", failure.m_out.str());
            CPPUNIT_ASSERT_EQUAL_DIFF("", failure.m_err.str());
            string res = scanFiles(m_testDir);
            removeRandomUUID(res);
            CPPUNIT_ASSERT_EQUAL_DIFF(fooconfig + syncurl, filterFiles(res));
        }

        rm_r(m_testDir);
        {
            // allow user to proceed if they provide enough information;
            // source created because listed and usable
            TestCmdline failure("--configure", "syncURL=local://@bar", "backend=calendar", "foo", "eds_event", NULL);
            CPPUNIT_ASSERT(failure.m_cmdline->parse());
            CPPUNIT_ASSERT(failure.m_cmdline->run());
            CPPUNIT_ASSERT_EQUAL_DIFF("", failure.m_out.str());
            CPPUNIT_ASSERT_EQUAL_DIFF("", failure.m_err.str());
            string res = scanFiles(m_testDir);
            removeRandomUUID(res);
            CPPUNIT_ASSERT_EQUAL_DIFF(fooconfig + syncurl + configsource, filterFiles(res));
        }

        rm_r(m_testDir);
        {
            // allow user to proceed if they provide enough information;
            // source created because listed and usable
            TestCmdline failure("--configure", "syncURL=local://@bar", "eds_event/backend@default=calendar", "foo", "eds_event", NULL);
            CPPUNIT_ASSERT(failure.m_cmdline->parse());
            CPPUNIT_ASSERT(failure.m_cmdline->run());
            CPPUNIT_ASSERT_EQUAL_DIFF("", failure.m_out.str());
            CPPUNIT_ASSERT_EQUAL_DIFF("", failure.m_err.str());
            string res = scanFiles(m_testDir);
            removeRandomUUID(res);
            CPPUNIT_ASSERT_EQUAL_DIFF(fooconfig + syncurl + configsource, filterFiles(res));
        }
    }


    void testConfigureSources() {
        ScopedEnvChange templates("SYNCEVOLUTION_TEMPLATE_DIR", "templates");
        ScopedEnvChange xdg("XDG_CONFIG_HOME", m_testDir);
        ScopedEnvChange home("HOME", m_testDir);

        // create from scratch with only addressbook configured
        {
            TestCmdline cmdline("--configure",
                                "--source-property", "database = file://tmp/test",
                                "--source-property", "type = file:text/x-vcard",
                                "@foobar",
                                "addressbook",
                                NULL);
            cmdline.doit();
        }
        string root = m_testDir;
        root += "/syncevolution/foobar";
        string res = scanFiles(root);
        removeRandomUUID(res);
        string expected =
            StringPrintf(".internal.ini:contextMinVersion = %d\n"
                         ".internal.ini:contextCurVersion = %d\n"
                         "config.ini:# logdir = \n"
                         "config.ini:# maxlogdirs = 10\n"
                         "config.ini:deviceId = fixed-devid\n"
                         "sources/addressbook/config.ini:backend = file\n"
                         "sources/addressbook/config.ini:database = file://tmp/test\n"
                         "sources/addressbook/config.ini:databaseFormat = text/x-vcard\n"
                         "sources/addressbook/config.ini:# databaseUser = \n"
                         "sources/addressbook/config.ini:# databasePassword = \n",
                         CONFIG_CONTEXT_MIN_VERSION,
                         CONFIG_CONTEXT_CUR_VERSION);
        CPPUNIT_ASSERT_EQUAL_DIFF(expected, res);

        // add calendar
        {
            TestCmdline cmdline("--configure",
                                "--source-property", "database@foobar = file://tmp/test2",
                                "--source-property", "backend = calendar",
                                "@foobar",
                                "calendar",
                                NULL);
            cmdline.doit();
        }
        res = scanFiles(root);
        removeRandomUUID(res);
        expected +=
            "sources/calendar/config.ini:backend = calendar\n"
            "sources/calendar/config.ini:database = file://tmp/test2\n"
            "sources/calendar/config.ini:# databaseFormat = \n"
            "sources/calendar/config.ini:# databaseUser = \n"
            "sources/calendar/config.ini:# databasePassword = \n";
        CPPUNIT_ASSERT_EQUAL_DIFF(expected, res);

        // add ScheduleWorld peer: must reuse existing backend settings
        {
            TestCmdline cmdline("--configure",
                                "scheduleworld@foobar",
                                NULL);
            cmdline.doit();
        }
        res = scanFiles(root);
        removeRandomUUID(res);
        expected = ScheduleWorldConfig();
        boost::replace_all(expected,
                           "addressbook/config.ini:backend = addressbook",
                           "addressbook/config.ini:backend = file");
        boost::replace_all(expected,
                           "addressbook/config.ini:# database = ",
                           "addressbook/config.ini:database = file://tmp/test");
        boost::replace_all(expected,
                           "addressbook/config.ini:# databaseFormat = ",
                           "addressbook/config.ini:databaseFormat = text/x-vcard");
        boost::replace_all(expected,
                           "calendar/config.ini:# database = ",
                           "calendar/config.ini:database = file://tmp/test2");
        sortConfig(expected);
        CPPUNIT_ASSERT_EQUAL_DIFF(expected, res);

        // disable all sources except for addressbook
        {
            TestCmdline cmdline("--configure",
                                "--source-property", "addressbook/sync=two-way",
                                "--source-property", "sync=none",
                                "scheduleworld@foobar",
                                NULL);
            cmdline.doit();
        }
        res = scanFiles(root);
        removeRandomUUID(res);
        boost::replace_all(expected, "sync = two-way", "sync = disabled");
        boost::replace_first(expected, "sync = disabled", "sync = two-way");
        CPPUNIT_ASSERT_EQUAL_DIFF(expected, res);

        // override type in template while creating from scratch
        {
            TestCmdline cmdline("--configure",
                                "--template", "SyncEvolution",
                                "--source-property", "addressbook/type=file:text/vcard:3.0",
                                "--source-property", "calendar/type=file:text/calendar:2.0",
                                "syncevo@syncevo",
                                NULL);
            cmdline.doit();
        }
        string syncevoroot = m_testDir + "/syncevolution/syncevo";
        res = scanFiles(syncevoroot + "/sources/addressbook");
        CPPUNIT_ASSERT(res.find("backend = file\n") != res.npos);
        CPPUNIT_ASSERT(res.find("databaseFormat = text/vcard\n") != res.npos);
        res = scanFiles(syncevoroot + "/sources/calendar");
        CPPUNIT_ASSERT(res.find("backend = file\n") != res.npos);
        CPPUNIT_ASSERT(res.find("databaseFormat = text/calendar\n") != res.npos);
    }

    void testOldConfigure() {
        ScopedEnvChange templates("SYNCEVOLUTION_TEMPLATE_DIR", "templates");
        ScopedEnvChange xdg("XDG_CONFIG_HOME", m_testDir);
        ScopedEnvChange home("HOME", m_testDir);

        string oldConfig = OldScheduleWorldConfig();
        InitList<string> props = InitList<string>("serverNonce") +
            "clientNonce" +
            "devInfoHash" +
            "HashCode" +
            "ConfigDate" +
            "deviceData" +
            "adminData" +
            "synthesisID" +
            "rootMinVersion" +
            "rootCurVersion" +
            "contextMinVersion" +
            "contextCurVersion" +
            "peerMinVersion" +
            "peerCurVersion" +
            "lastNonce" +
            "last";
        BOOST_FOREACH(string &prop, props) {
            boost::replace_all(oldConfig,
                               prop + " = ",
                               prop + " = internal value");
        }

        rm_r(m_testDir);
        createFiles(m_testDir + "/.sync4j/evolution/scheduleworld", oldConfig);

        // Cannot read/and write old format anymore.
        SyncContext::setStableRelease(false);
        expectMigration("scheduleworld");

        // Migrate explicitly.
        {
            TestCmdline cmdline("--migrate", "scheduleworld", NULL);
            cmdline.doit();
        }

        // now test with new format
        string expected = ScheduleWorldConfig();
        boost::replace_first(expected, "# ConsumerReady = 0", "ConsumerReady = 1");
        boost::replace_first(expected, "# database = ", "database = xyz");
        boost::replace_first(expected, "# databaseUser = ", "databaseUser = foo");
        boost::replace_first(expected, "# databasePassword = ", "databasePassword = bar");
        // migrating "type" sets forceSyncFormat (always)
        // and databaseFormat (if format was part of type, as for addressbook)
        boost::replace_all(expected, "# forceSyncFormat = 0", "forceSyncFormat = 0");
        boost::replace_first(expected, "# databaseFormat = ", "databaseFormat = text/vcard");
        doConfigure(expected, "sources/addressbook/config.ini:");
    }

    string doConfigure(const string &SWConfig, const string &addressbookPrefix) {
        string expected;

        {
            TestCmdline cmdline("--configure",
                                "--source-property", "sync = disabled",
                                "scheduleworld",
                                NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_out.str());
            expected = filterConfig(internalToIni(SWConfig));
            boost::replace_all(expected,
                               "sync = two-way",
                               "sync = disabled");
            CPPUNIT_ASSERT_EQUAL_DIFF(expected,
                                      filterConfig(printConfig("scheduleworld")));
        }

        {
            TestCmdline cmdline("--configure",
                                "--source-property", "sync = one-way-from-server",
                                "scheduleworld",
                                "addressbook",
                                NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_out.str());
            expected = SWConfig;
            boost::replace_all(expected,
                               "sync = two-way",
                               "sync = disabled");
            boost::replace_first(expected,
                                 addressbookPrefix + "sync = disabled",
                                 addressbookPrefix + "sync = one-way-from-server");
            expected = filterConfig(internalToIni(expected));
            CPPUNIT_ASSERT_EQUAL_DIFF(expected,
                                      filterConfig(printConfig("scheduleworld")));
        }

        {
            TestCmdline cmdline("--configure",
                                "--sync", "two-way",
                                "-z", "database=source",
                                // note priority of suffix: most specific wins
                                "--sync-property", "maxlogdirs@scheduleworld@default=20",
                                "--sync-property", "maxlogdirs@default=10",
                                "--sync-property", "maxlogdirs=5",
                                "-y", "LOGDIR@default=logdir",
                                "scheduleworld",
                                NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_out.str());
            boost::replace_all(expected,
                               "sync = one-way-from-server",
                               "sync = two-way");
            boost::replace_all(expected,
                               "sync = disabled",
                               "sync = two-way");
            boost::replace_all(expected,
                               "# database = ",
                               "database = source");
            boost::replace_all(expected,
                               "database = xyz",
                               "database = source");
            boost::replace_all(expected,
                               "# maxlogdirs = 10",
                               "maxlogdirs = 20");
            boost::replace_all(expected,
                               "# logdir = ",
                               "logdir = logdir");
            CPPUNIT_ASSERT_EQUAL_DIFF(expected,
                                      filterConfig(printConfig("scheduleworld")));
        }

        return expected;
    }

    void testPrintDatabases() {
        {
            // full output
            TestCmdline cmdline("--print-databases", (char *)0);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
            // exact output varies, do not test
        }
        bool haveEDS;
        {
            // limit output to one specific backend
            TestCmdline cmdline("--print-databases", "backend=evolution-contacts", (char *)0);
            cmdline.doit();
            if (cmdline.m_err.str().find("not one of the valid values") != std::string::npos) {
                // not enabled, only this error messages expected
                CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_out.str());
            } else {
                // enabled, no error, one entry
                haveEDS = true;
                CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
                CPPUNIT_ASSERT(boost::starts_with(cmdline.m_out.str(), "evolution-contacts:\n"));
                int entries = 0;
		std::string out = cmdline.m_out.str();
                BOOST_FOREACH(const std::string &line,
                              boost::tokenizer< boost::char_separator<char> >(out,
                                                                              boost::char_separator<char>("\n"))) {
                    if (!boost::starts_with(line, " ")) {
                        entries++;
                    }
                }
                CPPUNIT_ASSERT_EQUAL(1, entries);
            }
        }
        if (haveEDS) {
            // limit output to one specific backend, chosen via config
            {
                TestCmdline cmdline("--configure", "backend=evolution-contacts", "@foo-config", "bar-source", (char *)0);
                cmdline.doit();
                CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_out.str());
            }
            {
                TestCmdline cmdline("--print-databases", "@foo-config", "bar-source", (char *)0);
                cmdline.doit();
                CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
                CPPUNIT_ASSERT(boost::starts_with(cmdline.m_out.str(), "@foo-config/bar-source:\n"));
                int entries = 0;
		std::string out = cmdline.m_out.str();
                BOOST_FOREACH(const std::string &line,
                              boost::tokenizer< boost::char_separator<char> >(out,
                                                                              boost::char_separator<char>("\n"))) {
                    if (!boost::starts_with(line, " ")) {
                        entries++;
                    }
                }
                CPPUNIT_ASSERT_EQUAL(1, entries);
            }
        }
    }

    void testMigrate() {
        ScopedEnvChange templates("SYNCEVOLUTION_TEMPLATE_DIR", "templates");
        ScopedEnvChange xdg("XDG_CONFIG_HOME", m_testDir);
        ScopedEnvChange home("HOME", m_testDir);

        string oldRoot = m_testDir + "/.sync4j/evolution/scheduleworld";
        string newRoot = m_testDir + "/syncevolution/default";

        string oldConfig = OldScheduleWorldConfig();

        {
            // migrate old config
            createFiles(oldRoot, oldConfig);
            string createdConfig = scanFiles(oldRoot);
            TestCmdline cmdline("--migrate",
                                "scheduleworld",
                                NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_out.str());

            string migratedConfig = scanFiles(newRoot);
            string expected = ScheduleWorldConfig();
            sortConfig(expected);
            // migrating SyncEvolution < 1.2 configs sets
            // ConsumerReady, to keep config visible in the updated
            // sync-ui
            boost::replace_all(expected, "# ConsumerReady = 0", "ConsumerReady = 1");
            boost::replace_first(expected, "# database = ", "database = xyz");
            boost::replace_first(expected, "# databaseUser = ", "databaseUser = foo");
            boost::replace_first(expected, "# databasePassword = ", "databasePassword = bar");
            // migrating "type" sets forceSyncFormat (always)
            // and databaseFormat (if format was part of type, as for addressbook)
            boost::replace_all(expected, "# forceSyncFormat = 0", "forceSyncFormat = 0");
            boost::replace_first(expected, "# databaseFormat = ", "databaseFormat = text/vcard");
            CPPUNIT_ASSERT_EQUAL_DIFF(expected, migratedConfig);
            string renamedConfig = scanFiles(oldRoot + ".old");
            CPPUNIT_ASSERT_EQUAL_DIFF(createdConfig, renamedConfig);
        }

        {
            // rewrite existing config with obsolete properties
            // => these properties should get removed
            //
            // There is one limitation: shared nodes are not rewritten.
            // This is acceptable.
            createFiles(newRoot + "/peers/scheduleworld",
                        "config.ini:# obsolete comment\n"
                        "config.ini:obsoleteprop = foo\n",
                        true);
            string createdConfig = scanFiles(newRoot, "scheduleworld");

            TestCmdline cmdline("--migrate",
                                "scheduleworld",
                                NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_out.str());

            string migratedConfig = scanFiles(newRoot, "scheduleworld");
            string expected = ScheduleWorldConfig();
            sortConfig(expected);
            boost::replace_all(expected, "# ConsumerReady = 0", "ConsumerReady = 1");
            boost::replace_first(expected, "# database = ", "database = xyz");
            boost::replace_first(expected, "# databaseUser = ", "databaseUser = foo");
            boost::replace_first(expected, "# databasePassword = ", "databasePassword = bar");
            boost::replace_all(expected, "# forceSyncFormat = 0", "forceSyncFormat = 0");
            boost::replace_first(expected, "# databaseFormat = ", "databaseFormat = text/vcard");
            CPPUNIT_ASSERT_EQUAL_DIFF(expected, migratedConfig);
            string renamedConfig = scanFiles(newRoot, "scheduleworld.old.1");
            boost::replace_first(createdConfig, "ConsumerReady = 1", "ConsumerReady = 0");
            boost::replace_all(createdConfig, "/scheduleworld/", "/scheduleworld.old.1/");
            CPPUNIT_ASSERT_EQUAL_DIFF(createdConfig, renamedConfig);
        }

        {
            // migrate old config with changes and .synthesis directory, a second time
            createFiles(oldRoot, oldConfig);
            createFiles(oldRoot,
                        ".synthesis/dummy-file.bfi:dummy = foobar\n"
                        "spds/sources/addressbook/changes/config.txt:foo = bar\n"
                        "spds/sources/addressbook/changes/config.txt:foo2 = bar2\n",
                        true);
            string createdConfig = scanFiles(oldRoot);
            rm_r(newRoot);
            TestCmdline cmdline("--migrate",
                                "scheduleworld",
                                NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_out.str());

            string migratedConfig = scanFiles(newRoot);
            string expected = ScheduleWorldConfig();
            sortConfig(expected);
            boost::replace_all(expected, "# ConsumerReady = 0", "ConsumerReady = 1");
            boost::replace_first(expected, "# database = ", "database = xyz");
            boost::replace_first(expected, "# databaseUser = ", "databaseUser = foo");
            boost::replace_first(expected, "# databasePassword = ", "databasePassword = bar");
            boost::replace_all(expected, "# forceSyncFormat = 0", "forceSyncFormat = 0");
            boost::replace_first(expected, "# databaseFormat = ", "databaseFormat = text/vcard");
            boost::replace_first(expected,
                                 "peers/scheduleworld/sources/addressbook/config.ini",
                                 "peers/scheduleworld/sources/addressbook/.other.ini:foo = bar\n"
                                 "peers/scheduleworld/sources/addressbook/.other.ini:foo2 = bar2\n"
                                 "peers/scheduleworld/sources/addressbook/config.ini");
            boost::replace_first(expected,
                                 "peers/scheduleworld/config.ini",
                                 "peers/scheduleworld/.synthesis/dummy-file.bfi:dummy = foobar\n"
                                 "peers/scheduleworld/config.ini");
            CPPUNIT_ASSERT_EQUAL_DIFF(expected, migratedConfig);
            string renamedConfig = scanFiles(oldRoot + ".old.1");
            boost::replace_first(createdConfig, "ConsumerReady = 1", "ConsumerReady = 0");
            CPPUNIT_ASSERT_EQUAL_DIFF(createdConfig, renamedConfig);
        }

        {
            string otherRoot = m_testDir + "/syncevolution/other";
            rm_r(otherRoot);

            // migrate old config into non-default context
            createFiles(oldRoot, oldConfig);
            string createdConfig = scanFiles(oldRoot);
            {
                TestCmdline cmdline("--migrate",
                                    "scheduleworld@other",
                                    NULL);
                cmdline.doit();
                CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
                CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_out.str());
            }

            string migratedConfig = scanFiles(otherRoot);
            string expected = ScheduleWorldConfig();
            sortConfig(expected);
            boost::replace_all(expected, "# ConsumerReady = 0", "ConsumerReady = 1");
            boost::replace_first(expected, "# database = ", "database = xyz");
            boost::replace_first(expected, "# databaseUser = ", "databaseUser = foo");
            boost::replace_first(expected, "# databasePassword = ", "databasePassword = bar");
            boost::replace_all(expected, "# forceSyncFormat = 0", "forceSyncFormat = 0");
            boost::replace_first(expected, "# databaseFormat = ", "databaseFormat = text/vcard");
            CPPUNIT_ASSERT_EQUAL_DIFF(expected, migratedConfig);
            string renamedConfig = scanFiles(oldRoot + ".old");
            CPPUNIT_ASSERT_EQUAL_DIFF(createdConfig, renamedConfig);

            // migrate the migrated config again inside the "other" context,
            // with no "default" context which might interfere with the tests
            //
            // ConsumerReady was set as part of previous migration,
            // must be removed during migration to hide the migrated
            // config from average users.
            rm_r(newRoot);
            {
                TestCmdline cmdline("--migrate",
                                    "scheduleworld@other",
                                    NULL);
                cmdline.doit();
                CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
                CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_out.str());
            }
            migratedConfig = scanFiles(otherRoot, "scheduleworld");
            expected = ScheduleWorldConfig();
            sortConfig(expected);
            boost::replace_all(expected, "# ConsumerReady = 0", "ConsumerReady = 1");
            boost::replace_first(expected, "# database = ", "database = xyz");
            boost::replace_first(expected, "# databaseUser = ", "databaseUser = foo");
            boost::replace_first(expected, "# databasePassword = ", "databasePassword = bar");
            boost::replace_all(expected, "# forceSyncFormat = 0", "forceSyncFormat = 0");
            boost::replace_first(expected, "# databaseFormat = ", "databaseFormat = text/vcard");
            CPPUNIT_ASSERT_EQUAL_DIFF(expected, migratedConfig);
            renamedConfig = scanFiles(otherRoot, "scheduleworld.old.3");
            boost::replace_all(expected, "/scheduleworld/", "/scheduleworld.old.3/");
            boost::replace_all(expected, "ConsumerReady = 1", "ConsumerReady = 0");
            CPPUNIT_ASSERT_EQUAL_DIFF(expected, renamedConfig);

            // migrate once more, this time without the explicit context in
            // the config name => must not change the context, need second .old dir
            {
                TestCmdline cmdline("--migrate",
                                    "scheduleworld",
                                    NULL);
                cmdline.doit();
                CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
                CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_out.str());
            }
            migratedConfig = scanFiles(otherRoot, "scheduleworld");
            boost::replace_all(expected, "/scheduleworld.old.3/", "/scheduleworld/");
            boost::replace_all(expected, "ConsumerReady = 0", "ConsumerReady = 1");          
            CPPUNIT_ASSERT_EQUAL_DIFF(expected, migratedConfig);
            renamedConfig = scanFiles(otherRoot, "scheduleworld.old.4");
            boost::replace_all(expected, "/scheduleworld/", "/scheduleworld.old.4/");
            boost::replace_all(expected, "ConsumerReady = 1", "ConsumerReady = 0");
            CPPUNIT_ASSERT_EQUAL_DIFF(expected, renamedConfig);

            // remove ConsumerReady: must be remain unset when migrating
            // hidden SyncEvolution >= 1.2 configs
            {
                TestCmdline cmdline("--configure",
                                    "--sync-property", "ConsumerReady=0",
                                    "scheduleworld",
                                    NULL);
                cmdline.doit();
                CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
                CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_out.str());
            }

            // migrate once more => keep ConsumerReady unset
            {
                TestCmdline cmdline("--migrate",
                                    "scheduleworld",
                                    NULL);
                cmdline.doit();
                CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
                CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_out.str());
            }
            migratedConfig = scanFiles(otherRoot, "scheduleworld");
            boost::replace_all(expected, "/scheduleworld.old.4/", "/scheduleworld/");
            CPPUNIT_ASSERT_EQUAL_DIFF(expected, migratedConfig);
            renamedConfig = scanFiles(otherRoot, "scheduleworld.old.5");
            boost::replace_all(expected, "/scheduleworld/", "/scheduleworld.old.5/");
            CPPUNIT_ASSERT_EQUAL_DIFF(expected, renamedConfig);
        }
    }

    void testMigrateContext()
    {
        // Migrate context containing a peer. Must also migrate peer.
        // Covers special case of inconsistent "type".

        ScopedEnvChange templates("SYNCEVOLUTION_TEMPLATE_DIR", "templates");
        ScopedEnvChange xdg("XDG_CONFIG_HOME", m_testDir);
        ScopedEnvChange home("HOME", m_testDir);

        string root = m_testDir + "/syncevolution/default";

        string oldConfig =
            "config.ini:logDir = none\n"
            "peers/scheduleworld/config.ini:syncURL = http://sync.scheduleworld.com/funambol/ds\n"
            "peers/scheduleworld/config.ini:# username = \n"
            "peers/scheduleworld/config.ini:# password = \n"

            "peers/scheduleworld/sources/addressbook/config.ini:sync = two-way\n"
            "peers/scheduleworld/sources/addressbook/config.ini:uri = card3\n"
            "peers/scheduleworld/sources/addressbook/config.ini:type = addressbook:text/vcard\n" // correct!
            "sources/addressbook/config.ini:type = calendar\n" // wrong!

            "peers/funambol/config.ini:syncURL = http://sync.funambol.com/funambol/ds\n"
            "peers/funambol/config.ini:# username = \n"
            "peers/funambol/config.ini:# password = \n"

            "peers/funambol/sources/calendar/config.ini:sync = refresh-from-server\n"
            "peers/funambol/sources/calendar/config.ini:uri = cal\n"
            "peers/funambol/sources/calendar/config.ini:type = calendar\n" // correct!
            "peers/funambol/sources/addressbook/config.ini:# sync = disabled\n"
            "peers/funambol/sources/addressbook/config.ini:type = file\n" // not used for context because source disabled
            "sources/calendar/config.ini:type = memos\n" // wrong!

            "peers/memotoo/config.ini:syncURL = http://sync.memotoo.com/memotoo/ds\n"
            "peers/memotoo/config.ini:# username = \n"
            "peers/memotoo/config.ini:# password = \n"

            "peers/memotoo/sources/memo/config.ini:sync = refresh-from-client\n"
            "peers/memotoo/sources/memo/config.ini:uri = cal\n"
            "peers/memotoo/sources/memo/config.ini:type = memo:text/plain\n" // correct!
            "sources/memo/config.ini:type = todo\n" // wrong!
            ;

        {
            createFiles(root, oldConfig);
            TestCmdline cmdline("--migrate",
                                "memo/backend=file", // override memo "backend" during migration
                                "@default",
                                NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_out.str());

            string migratedConfig = scanFiles(root);
            CPPUNIT_ASSERT(migratedConfig.find("peers/scheduleworld/") != migratedConfig.npos);
            CPPUNIT_ASSERT(migratedConfig.find("sources/addressbook/config.ini:backend = addressbook") != migratedConfig.npos);
            CPPUNIT_ASSERT(migratedConfig.find("sources/addressbook/config.ini:databaseFormat = text/vcard") != migratedConfig.npos);
            CPPUNIT_ASSERT(migratedConfig.find("peers/scheduleworld/sources/addressbook/config.ini:syncFormat = text/vcard") != migratedConfig.npos);
            CPPUNIT_ASSERT(migratedConfig.find("peers/scheduleworld/sources/addressbook/config.ini:sync = two-way") != migratedConfig.npos);
            CPPUNIT_ASSERT(migratedConfig.find("peers/scheduleworld/sources/calendar/config.ini:# sync = disabled") != migratedConfig.npos);
            CPPUNIT_ASSERT(migratedConfig.find("peers/scheduleworld/sources/memo/config.ini:# sync = disabled") != migratedConfig.npos);
            CPPUNIT_ASSERT(migratedConfig.find("sources/calendar/config.ini:backend = calendar") != migratedConfig.npos);
            CPPUNIT_ASSERT(migratedConfig.find("sources/calendar/config.ini:# databaseFormat = ") != migratedConfig.npos);
            CPPUNIT_ASSERT(migratedConfig.find("peers/funambol/sources/calendar/config.ini:# syncFormat = ") != migratedConfig.npos);
            CPPUNIT_ASSERT(migratedConfig.find("peers/funambol/sources/addressbook/config.ini:# sync = disabled") != migratedConfig.npos);
            CPPUNIT_ASSERT(migratedConfig.find("peers/funambol/sources/calendar/config.ini:sync = refresh-from-server") != migratedConfig.npos);
            CPPUNIT_ASSERT(migratedConfig.find("peers/funambol/sources/memo/config.ini:# sync = disabled") != migratedConfig.npos);
            CPPUNIT_ASSERT(migratedConfig.find("sources/memo/config.ini:backend = file") != migratedConfig.npos);
            CPPUNIT_ASSERT(migratedConfig.find("sources/memo/config.ini:databaseFormat = text/plain") != migratedConfig.npos);
            CPPUNIT_ASSERT(migratedConfig.find("peers/memotoo/sources/memo/config.ini:syncFormat = text/plain") != migratedConfig.npos);
            CPPUNIT_ASSERT(migratedConfig.find("peers/memotoo/sources/addressbook/config.ini:# sync = disabled") != migratedConfig.npos);
            CPPUNIT_ASSERT(migratedConfig.find("peers/memotoo/sources/calendar/config.ini:# sync = disabled") != migratedConfig.npos);
            CPPUNIT_ASSERT(migratedConfig.find("peers/memotoo/sources/memo/config.ini:sync = refresh-from-client") != migratedConfig.npos);
        }
    }

    void testMigrateAutoSync() {
        ScopedEnvChange templates("SYNCEVOLUTION_TEMPLATE_DIR", "templates");
        ScopedEnvChange xdg("XDG_CONFIG_HOME", m_testDir);
        ScopedEnvChange home("HOME", m_testDir);

        string oldRoot = m_testDir + "/.sync4j/evolution/scheduleworld";
        string newRoot = m_testDir + "/syncevolution/default";

        string oldConfig = "spds/syncml/config.txt:autoSync = 1\n";
        oldConfig += OldScheduleWorldConfig();

        {
            // migrate old config
            createFiles(oldRoot, oldConfig);
            string createdConfig = scanFiles(oldRoot);
            TestCmdline cmdline("--migrate",
                                "scheduleworld",
                                NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_out.str());

            string migratedConfig = scanFiles(newRoot);
            string expected = ScheduleWorldConfig();
            boost::replace_first(expected, "# autoSync = 0", "autoSync = 1");
            sortConfig(expected);
            // migrating SyncEvolution < 1.2 configs sets
            // ConsumerReady, to keep config visible in the updated
            // sync-ui
            boost::replace_all(expected, "# ConsumerReady = 0", "ConsumerReady = 1");
            boost::replace_first(expected, "# database = ", "database = xyz");
            boost::replace_first(expected, "# databaseUser = ", "databaseUser = foo");
            boost::replace_first(expected, "# databasePassword = ", "databasePassword = bar");
            // migrating "type" sets forceSyncFormat (always)
            // and databaseFormat (if format was part of type, as for addressbook)
            boost::replace_all(expected, "# forceSyncFormat = 0", "forceSyncFormat = 0");
            boost::replace_first(expected, "# databaseFormat = ", "databaseFormat = text/vcard");
            CPPUNIT_ASSERT_EQUAL_DIFF(expected, migratedConfig);
            string renamedConfig = scanFiles(oldRoot + ".old");
            // autoSync must have been unset
            boost::replace_first(createdConfig, ":autoSync = 1", ":autoSync = 0");
            CPPUNIT_ASSERT_EQUAL_DIFF(createdConfig, renamedConfig);
        }

        {
            // rewrite existing config with autoSync set
            string createdConfig = scanFiles(newRoot, "scheduleworld");

            TestCmdline cmdline("--migrate",
                                "scheduleworld",
                                NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_out.str());

            string migratedConfig = scanFiles(newRoot, "scheduleworld");
            string expected = ScheduleWorldConfig();
            boost::replace_first(expected, "# autoSync = 0", "autoSync = 1");
            sortConfig(expected);
            boost::replace_all(expected, "# ConsumerReady = 0", "ConsumerReady = 1");
            boost::replace_first(expected, "# database = ", "database = xyz");
            boost::replace_first(expected, "# databaseUser = ", "databaseUser = foo");
            boost::replace_first(expected, "# databasePassword = ", "databasePassword = bar");
            boost::replace_all(expected, "# forceSyncFormat = 0", "forceSyncFormat = 0");
            boost::replace_first(expected, "# databaseFormat = ", "databaseFormat = text/vcard");
            CPPUNIT_ASSERT_EQUAL_DIFF(expected, migratedConfig);
            string renamedConfig = scanFiles(newRoot, "scheduleworld.old.1");
            // autoSync must have been unset
            boost::replace_first(createdConfig, ":autoSync = 1", ":autoSync = 0");
            // the scheduleworld config was consumer ready, the migrated one isn't
            boost::replace_all(createdConfig, "ConsumerReady = 1", "ConsumerReady = 0");
            boost::replace_all(createdConfig, "/scheduleworld/", "/scheduleworld.old.1/");
            CPPUNIT_ASSERT_EQUAL_DIFF(createdConfig, renamedConfig);
        }
    }

    void testItemOperations() {
        ScopedEnvChange templates("SYNCEVOLUTION_TEMPLATE_DIR", "templates");
        ScopedEnvChange xdg("XDG_CONFIG_HOME", m_testDir);
        ScopedEnvChange home("HOME", m_testDir);

        {
            // "foo" not configured
            TestCmdline cmdline("--print-items",
                                "foo",
                                "bar",
                                NULL);
            cmdline.doit(false);
            CPPUNIT_ASSERT_EQUAL_DIFF("[ERROR] bar: backend not supported or not correctly configured (backend=select backend databaseFormat= syncFormat=)\nconfiguration 'foo' does not exist\nsource 'bar' does not exist\nbackend property not set", cmdline.m_err.str());
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_out.str());
        }

        {
            // "foo" not configured, no source named
            TestCmdline cmdline("--print-items",
                                "foo",
                                NULL);
            cmdline.doit(false);
            CPPUNIT_ASSERT_EQUAL_DIFF("[ERROR] backend not supported or not correctly configured (backend=select backend databaseFormat= syncFormat=)\nconfiguration 'foo' does not exist\nno source selected\nbackend property not set", cmdline.m_err.str());
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_out.str());
        }

        {
            // nothing known about source
            TestCmdline cmdline("--print-items",
                                NULL);
            cmdline.doit(false);
            CPPUNIT_ASSERT_EQUAL_DIFF("[ERROR] backend not supported or not correctly configured (backend=select backend databaseFormat= syncFormat=)\nno source selected\nbackend property not set", cmdline.m_err.str());
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_out.str());
        }

        {
            // now create foo
            TestCmdline cmdline("--configure",
                                "--template",
                                "default",
                                "foo",
                                NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_out.str());
        }

        {
            // "foo" now configured, still no source
            TestCmdline cmdline("--print-items",
                                "foo",
                                NULL);
            cmdline.doit(false);
            CPPUNIT_ASSERT_EQUAL_DIFF("[ERROR] backend not supported or not correctly configured (backend=select backend databaseFormat= syncFormat=)\nno source selected\nbackend property not set", cmdline.m_err.str());
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_out.str());
        }

        {
            // foo configured, but "bar" is not
            TestCmdline cmdline("--print-items",
                                "foo",
                                "bar",
                                NULL);
            cmdline.doit(false);
            CPPUNIT_ASSERT_EQUAL_DIFF("[ERROR] bar: backend not supported or not correctly configured (backend=select backend databaseFormat= syncFormat=)\nsource 'bar' does not exist\nbackend property not set", cmdline.m_err.str());
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_out.str());
        }

        {
            // add "bar" source, using file backend
            TestCmdline cmdline("--configure",
                                "backend=file",
                                ("database=file://" + m_testDir + "/addressbook").c_str(),
                                "databaseFormat=text/vcard",
                                "foo",
                                "bar",
                                NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_out.str());
        }

        {
            // no items yet
            TestCmdline cmdline("--print-items",
                                "foo",
                                "bar",
                                NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_out.str());
        }

        static const std::string john =
            "BEGIN:VCARD\n"
            "VERSION:3.0\n"
            "FN:John Doe\n"
            "N:Doe;John;;;\n"
            "END:VCARD\n",
            joan =
            "BEGIN:VCARD\n"
            "VERSION:3.0\n"
            "FN:Joan Doe\n"
            "N:Doe;Joan;;;\n"
            "END:VCARD\n";

        {
            // create one file
            std::string file1 = "1:" + john, file2 = "2:" + joan;
            boost::replace_all(file1, "\n", "\n1:");
            file1.resize(file1.size() - 2);
            boost::replace_all(file2, "\n", "\n2:");
            file2.resize(file2.size() - 2);
            createFiles(m_testDir + "/addressbook", file1 + file2);

            TestCmdline cmdline("--print-items",
                                "foo",
                                "bar",
                                NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
            CPPUNIT_ASSERT_EQUAL_DIFF("1\n2\n", cmdline.m_out.str());
        }

        {
            // alternatively just specify enough parameters,
            // without the foo bar config part
            TestCmdline cmdline("--print-items",
                                "backend=file",
                                ("database=file://" + m_testDir + "/addressbook").c_str(),
                                "databaseFormat=text/vcard",
                                NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
            CPPUNIT_ASSERT_EQUAL_DIFF("1\n2\n", cmdline.m_out.str());
        }

        {
            // export all
            TestCmdline cmdline("--export", "-",
                                "backend=file",
                                ("database=file://" + m_testDir + "/addressbook").c_str(),
                                "databaseFormat=text/vcard",
                                NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
            CPPUNIT_ASSERT_EQUAL_DIFF(john + "\n" + joan, cmdline.m_out.str());
        }

        {
            // export all via config
            TestCmdline cmdline("--export", "-",
                                "foo", "bar",
                                NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
            CPPUNIT_ASSERT_EQUAL_DIFF(john + "\n" + joan, cmdline.m_out.str());
        }

        {
            // export one
            TestCmdline cmdline("--export", "-",
                                "backend=file",
                                ("database=file://" + m_testDir + "/addressbook").c_str(),
                                "databaseFormat=text/vcard",
                                "--luids", "1",
                                NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
            CPPUNIT_ASSERT_EQUAL_DIFF(john, cmdline.m_out.str());
        }

        {
            // export one via config
            TestCmdline cmdline("--export", "-",
                                "foo", "bar", "1",
                                NULL);
            cmdline.doit();
            CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
            CPPUNIT_ASSERT_EQUAL_DIFF(john, cmdline.m_out.str());
        }

        // TODO: check configuration of just the source as @foo bar without peer

        {
            // check error message for missing config name
            TestCmdline cmdline((const char *)NULL);
            cmdline.doit(false);
            CPPUNIT_ASSERT_NO_THROW(cmdline.expectUsageError("[ERROR] No configuration name specified.\n"));
        }

        {
            // check error message for missing config name, version II
            TestCmdline cmdline("--run",
                                NULL);
            cmdline.doit(false);
            CPPUNIT_ASSERT_NO_THROW(cmdline.expectUsageError("[ERROR] No configuration name specified.\n"));
        }
    }

    const string m_testDir;        

private:

    /**
     * vararg constructor with NULL termination,
     * out and error stream into stringstream members
     */
    class TestCmdline : private LoggerBase {
        void init() {
            pushLogger(this);

            m_argv.reset(new const char *[m_argvstr.size() + 1]);
            m_argv[0] = "client-test";
            for (size_t index = 0;
                 index < m_argvstr.size();
                 ++index) {
                m_argv[index + 1] = m_argvstr[index].c_str();
            }

            m_cmdline.set(new Cmdline(m_argvstr.size() + 1, m_argv.get()), "cmdline");
        }

    public:
        TestCmdline(const char *arg, ...) {
            va_list argList;
            va_start (argList, arg);
            for (const char *curr = arg;
                 curr;
                 curr = va_arg(argList, const char *)) {
                m_argvstr.push_back(curr);
            }
            va_end(argList);
            init();
        }

        TestCmdline(const char * const argv[]) {
            for (int i = 0; argv[i]; i++) {
                m_argvstr.push_back(argv[i]);
            }
            init();
        }

        ~TestCmdline() {
            popLogger();
        }

        void doit(bool expectSuccess = true) {
            bool success = false;
            m_out.str("");
            m_err.str("");
            // emulates syncevolution.cpp exception handling
            try {
                success = m_cmdline->parse() &&
                    m_cmdline->run();
            } catch (const std::exception &ex) {
                m_err << "[ERROR] " << ex.what();
            } catch (...) {
                std::string explanation;
                Exception::handle(explanation);
                m_err << "[ERROR] " << explanation;
            }
            if (expectSuccess && m_err.str().size()) {
                m_out << endl << m_err.str();
            }
            CPPUNIT_ASSERT_MESSAGE(m_out.str(), success == expectSuccess);
        }

        /** verify that Cmdline::usage() produced a short usage info followed by a specific error message */
        void expectUsageError(const std::string &error)
        {
            // expect short usage info as normal output
            std::string out = m_out.str();
            std::string err = m_err.str();
            std::string all = m_all.str();
            CPPUNIT_ASSERT(boost::starts_with(out, "List databases:\n"));
            CPPUNIT_ASSERT(out.find("\nOptions:\n") == std::string::npos);
            CPPUNIT_ASSERT(boost::ends_with(out,
                                            "Remove item(s):\n"
                                            "  syncevolution --delete-items [--] <config> <source> (<luid> ... | '*')\n\n"));
            // exact error message
            CPPUNIT_ASSERT_EQUAL(error, err);

            // also check order
            CPPUNIT_ASSERT_EQUAL_DIFF(out + err, all);
        }

        // separate streams for normal messages and error messages
        ostringstream m_out, m_err;
        // combined stream with all messages
        ostringstream m_all;

        cxxptr<Cmdline> m_cmdline;

    private:
        vector<string> m_argvstr;
        boost::scoped_array<const char *> m_argv;

        /** capture output produced while test ran */
        void messagev(Level level,
                      const char *prefix,
                      const char *file,
                      int line,
                      const char *function,
                      const char *format,
                      va_list args)
        {
            if (level <= INFO) {
                ostringstream &out = level <= ERROR ? m_err : m_out;
                std::string str = StringPrintfV(format, args);
                if (level != SHOW) {
                    out << "[" << levelToStr(level) << "] ";
                    m_all << "[" << levelToStr(level) << "] ";
                }
                out << str;
                m_all << str;
                if (!boost::ends_with(str, "\n")) {
                    out << std::endl;
                    m_all << std::endl;
                }
            }
        }
        virtual bool isProcessSafe() const { return false; }
    };

    string DefaultConfig() {
        string config = ScheduleWorldConfig();
        boost::replace_first(config,
                             "syncURL = http://sync.scheduleworld.com/funambol/ds",
                             "syncURL = http://yourserver:port");
        boost::replace_first(config, "http://www.scheduleworld.com", "http://www.syncevolution.org");
        boost::replace_all(config, "ScheduleWorld", "SyncEvolution");
        boost::replace_all(config, "scheduleworld", "syncevolution");
        boost::replace_first(config, "PeerName = SyncEvolution", "# PeerName = ");
        boost::replace_first(config, "# ConsumerReady = 0", "ConsumerReady = 1");
        boost::replace_first(config, "uri = card3", "uri = addressbook");
        boost::replace_first(config, "uri = cal2", "uri = calendar");
        boost::replace_first(config, "uri = task2", "uri = todo");
        boost::replace_first(config, "uri = note", "uri = memo");
        boost::replace_first(config, "syncFormat = text/vcard", "# syncFormat = ");
        return config;
    }

    string ScheduleWorldConfig(int contextMinVersion = CONFIG_CONTEXT_MIN_VERSION,
                               int contextCurVersion = CONFIG_CONTEXT_CUR_VERSION,
                               int peerMinVersion = CONFIG_PEER_MIN_VERSION,
                               int peerCurVersion = CONFIG_PEER_CUR_VERSION) {
        // properties sorted by the order in which they are defined
        // in the sync and sync source property registry
        string config =
            StringPrintf("peers/scheduleworld/.internal.ini:peerMinVersion = %d\n"
                         "peers/scheduleworld/.internal.ini:peerCurVersion = %d\n"
                         "peers/scheduleworld/.internal.ini:# HashCode = 0\n"
                         "peers/scheduleworld/.internal.ini:# ConfigDate = \n"
                         "peers/scheduleworld/.internal.ini:# lastNonce = \n"
                         "peers/scheduleworld/.internal.ini:# deviceData = \n"
                         "peers/scheduleworld/.internal.ini:# webDAVCredentialsOkay = 0\n"
                         "peers/scheduleworld/config.ini:syncURL = http://sync.scheduleworld.com/funambol/ds\n"
                         "peers/scheduleworld/config.ini:# username = \n"
                         "peers/scheduleworld/config.ini:# password = \n"
                         ".internal.ini:contextMinVersion = %d\n"
                         ".internal.ini:contextCurVersion = %d\n"
                         "config.ini:# logdir = \n"
                         "peers/scheduleworld/config.ini:# loglevel = 0\n"
                         "peers/scheduleworld/config.ini:# printChanges = 1\n"
                         "peers/scheduleworld/config.ini:# dumpData = 1\n"
                         "config.ini:# maxlogdirs = 10\n"
                         "peers/scheduleworld/config.ini:# autoSync = 0\n"
                         "peers/scheduleworld/config.ini:# autoSyncInterval = 30M\n"
                         "peers/scheduleworld/config.ini:# autoSyncDelay = 5M\n"
                         "peers/scheduleworld/config.ini:# preventSlowSync = 1\n"
                         "peers/scheduleworld/config.ini:# useProxy = 0\n"
                         "peers/scheduleworld/config.ini:# proxyHost = \n"
                         "peers/scheduleworld/config.ini:# proxyUsername = \n"
                         "peers/scheduleworld/config.ini:# proxyPassword = \n"
                         "peers/scheduleworld/config.ini:# clientAuthType = md5\n"
                         "peers/scheduleworld/config.ini:# RetryDuration = 5M\n"
                         "peers/scheduleworld/config.ini:# RetryInterval = 2M\n"
                         "peers/scheduleworld/config.ini:# remoteIdentifier = \n"
                         "peers/scheduleworld/config.ini:# PeerIsClient = 0\n"
                         "peers/scheduleworld/config.ini:# SyncMLVersion = \n"
                         "peers/scheduleworld/config.ini:PeerName = ScheduleWorld\n"
                         "config.ini:deviceId = fixed-devid\n" /* this is not the default! */
                         "peers/scheduleworld/config.ini:# remoteDeviceId = \n"
                         "peers/scheduleworld/config.ini:# enableWBXML = 1\n"
                         "peers/scheduleworld/config.ini:# maxMsgSize = 150000\n"
                         "peers/scheduleworld/config.ini:# maxObjSize = 4000000\n"
                         "peers/scheduleworld/config.ini:# SSLServerCertificates = \n"
                         "peers/scheduleworld/config.ini:# SSLVerifyServer = 1\n"
                         "peers/scheduleworld/config.ini:# SSLVerifyHost = 1\n"
                         "peers/scheduleworld/config.ini:WebURL = http://www.scheduleworld.com\n"
                         "peers/scheduleworld/config.ini:IconURI = image://themedimage/icons/services/scheduleworld\n"
                         "peers/scheduleworld/config.ini:# ConsumerReady = 0\n"
                         "peers/scheduleworld/config.ini:# peerType = \n"

                         "peers/scheduleworld/sources/addressbook/.internal.ini:# adminData = \n"
                         "peers/scheduleworld/sources/addressbook/.internal.ini:# synthesisID = 0\n"
                         "peers/scheduleworld/sources/addressbook/config.ini:sync = two-way\n"
                         "peers/scheduleworld/sources/addressbook/config.ini:uri = card3\n"
                         "sources/addressbook/config.ini:backend = addressbook\n"
                         "peers/scheduleworld/sources/addressbook/config.ini:syncFormat = text/vcard\n"
                         "peers/scheduleworld/sources/addressbook/config.ini:# forceSyncFormat = 0\n"
                         "sources/addressbook/config.ini:# database = \n"
                         "sources/addressbook/config.ini:# databaseFormat = \n"
                         "sources/addressbook/config.ini:# databaseUser = \n"
                         "sources/addressbook/config.ini:# databasePassword = \n"

                         "peers/scheduleworld/sources/calendar/.internal.ini:# adminData = \n"
                         "peers/scheduleworld/sources/calendar/.internal.ini:# synthesisID = 0\n"
                         "peers/scheduleworld/sources/calendar/config.ini:sync = two-way\n"
                         "peers/scheduleworld/sources/calendar/config.ini:uri = cal2\n"
                         "sources/calendar/config.ini:backend = calendar\n"
                         "peers/scheduleworld/sources/calendar/config.ini:# syncFormat = \n"
                         "peers/scheduleworld/sources/calendar/config.ini:# forceSyncFormat = 0\n"
                         "sources/calendar/config.ini:# database = \n"
                         "sources/calendar/config.ini:# databaseFormat = \n"
                         "sources/calendar/config.ini:# databaseUser = \n"
                         "sources/calendar/config.ini:# databasePassword = \n"

                         "peers/scheduleworld/sources/memo/.internal.ini:# adminData = \n"
                         "peers/scheduleworld/sources/memo/.internal.ini:# synthesisID = 0\n"
                         "peers/scheduleworld/sources/memo/config.ini:sync = two-way\n"
                         "peers/scheduleworld/sources/memo/config.ini:uri = note\n"
                         "sources/memo/config.ini:backend = memo\n"
                         "peers/scheduleworld/sources/memo/config.ini:# syncFormat = \n"
                         "peers/scheduleworld/sources/memo/config.ini:# forceSyncFormat = 0\n"
                         "sources/memo/config.ini:# database = \n"
                         "sources/memo/config.ini:# databaseFormat = \n"
                         "sources/memo/config.ini:# databaseUser = \n"
                         "sources/memo/config.ini:# databasePassword = \n"

                         "peers/scheduleworld/sources/todo/.internal.ini:# adminData = \n"
                         "peers/scheduleworld/sources/todo/.internal.ini:# synthesisID = 0\n"
                         "peers/scheduleworld/sources/todo/config.ini:sync = two-way\n"
                         "peers/scheduleworld/sources/todo/config.ini:uri = task2\n"
                         "sources/todo/config.ini:backend = todo\n"
                         "peers/scheduleworld/sources/todo/config.ini:# syncFormat = \n"
                         "peers/scheduleworld/sources/todo/config.ini:# forceSyncFormat = 0\n"
                         "sources/todo/config.ini:# database = \n"
                         "sources/todo/config.ini:# databaseFormat = \n"
                         "sources/todo/config.ini:# databaseUser = \n"
                         "sources/todo/config.ini:# databasePassword = ",
                         peerMinVersion, peerCurVersion,
                         contextMinVersion, contextCurVersion);
#ifdef ENABLE_LIBSOUP
        // path to SSL certificates has to be set only for libsoup
        boost::replace_first(config,
                             "SSLServerCertificates = ",
                             "SSLServerCertificates = /etc/ssl/certs/ca-certificates.crt:/etc/pki/tls/certs/ca-bundle.crt:/usr/share/ssl/certs/ca-bundle.crt");
#endif

#if 0
        // Currently we don't have an icon for ScheduleWorld. If we
        // had (MB #2062) one, then this code would ensure that the
        // reference config also has the right path for it.
        const char *templateDir = getenv("SYNCEVOLUTION_TEMPLATE_DIR");
        if (!templateDir) {
            templateDir = TEMPLATE_DIR;
        }


        if (isDir(string(templateDir) + "/ScheduleWorld")) {
            boost::replace_all(config,
                               "# IconURI = ",
                               string("IconURI = file://") + templateDir + "/ScheduleWorld/icon.png");
        }
#endif
        return config;
    }

    string OldScheduleWorldConfig() {
        // old style paths
        string oldConfig =
            "spds/syncml/config.txt:syncURL = http://sync.scheduleworld.com/funambol/ds\n"
            "spds/syncml/config.txt:# username = \n"
            "spds/syncml/config.txt:# password = \n"
            "spds/syncml/config.txt:# logdir = \n"
            "spds/syncml/config.txt:# loglevel = 0\n"
            "spds/syncml/config.txt:# printChanges = 1\n"
            "spds/syncml/config.txt:# dumpData = 1\n"
            "spds/syncml/config.txt:# maxlogdirs = 10\n"
            "spds/syncml/config.txt:# autoSync = 0\n"
            "spds/syncml/config.txt:# autoSyncInterval = 30M\n"
            "spds/syncml/config.txt:# autoSyncDelay = 5M\n"
            "spds/syncml/config.txt:# preventSlowSync = 1\n"
            "spds/syncml/config.txt:# useProxy = 0\n"
            "spds/syncml/config.txt:# proxyHost = \n"
            "spds/syncml/config.txt:# proxyUsername = \n"
            "spds/syncml/config.txt:# proxyPassword = \n"
            "spds/syncml/config.txt:# clientAuthType = md5\n"
            "spds/syncml/config.txt:# RetryDuration = 5M\n"
            "spds/syncml/config.txt:# RetryInterval = 2M\n"
            "spds/syncml/config.txt:# remoteIdentifier = \n"
            "spds/syncml/config.txt:# PeerIsClient = 0\n"
            "spds/syncml/config.txt:# SyncMLVersion = \n"
            "spds/syncml/config.txt:PeerName = ScheduleWorld\n"
            "spds/syncml/config.txt:deviceId = fixed-devid\n" /* this is not the default! */
            "spds/syncml/config.txt:# remoteDeviceId = \n"
            "spds/syncml/config.txt:# enableWBXML = 1\n"
            "spds/syncml/config.txt:# maxMsgSize = 150000\n"
            "spds/syncml/config.txt:# maxObjSize = 4000000\n"
#ifdef ENABLE_LIBSOUP
            // path to SSL certificates is only set for libsoup
            "spds/syncml/config.txt:# SSLServerCertificates = /etc/ssl/certs/ca-certificates.crt:/etc/pki/tls/certs/ca-bundle.crt:/usr/share/ssl/certs/ca-bundle.crt\n"

#else
            "spds/syncml/config.txt:# SSLServerCertificates = \n"
#endif
            "spds/syncml/config.txt:# SSLVerifyServer = 1\n"
            "spds/syncml/config.txt:# SSLVerifyHost = 1\n"
            "spds/syncml/config.txt:WebURL = http://www.scheduleworld.com\n"
            "spds/syncml/config.txt:IconURI = image://themedimage/icons/services/scheduleworld\n"
            "spds/syncml/config.txt:# ConsumerReady = 0\n"
            "spds/sources/addressbook/config.txt:sync = two-way\n"
            "spds/sources/addressbook/config.txt:type = addressbook:text/vcard\n"
            "spds/sources/addressbook/config.txt:evolutionsource = xyz\n"
            "spds/sources/addressbook/config.txt:uri = card3\n"
            "spds/sources/addressbook/config.txt:evolutionuser = foo\n"
            "spds/sources/addressbook/config.txt:evolutionpassword = bar\n"
            "spds/sources/calendar/config.txt:sync = two-way\n"
            "spds/sources/calendar/config.txt:type = calendar\n"
            "spds/sources/calendar/config.txt:# database = \n"
            "spds/sources/calendar/config.txt:uri = cal2\n"
            "spds/sources/calendar/config.txt:# evolutionuser = \n"
            "spds/sources/calendar/config.txt:# evolutionpassword = \n"
            "spds/sources/memo/config.txt:sync = two-way\n"
            "spds/sources/memo/config.txt:type = memo\n"
            "spds/sources/memo/config.txt:# database = \n"
            "spds/sources/memo/config.txt:uri = note\n"
            "spds/sources/memo/config.txt:# evolutionuser = \n"
            "spds/sources/memo/config.txt:# evolutionpassword = \n"
            "spds/sources/todo/config.txt:sync = two-way\n"
            "spds/sources/todo/config.txt:type = todo\n"
            "spds/sources/todo/config.txt:# database = \n"
            "spds/sources/todo/config.txt:uri = task2\n"
            "spds/sources/todo/config.txt:# evolutionuser = \n"
            "spds/sources/todo/config.txt:# evolutionpassword = \n";
        return oldConfig;
    }

    string FunambolConfig() {
        string config = ScheduleWorldConfig();
        boost::replace_all(config, "/scheduleworld/", "/funambol/");
        boost::replace_all(config, "PeerName = ScheduleWorld", "PeerName = Funambol");

        boost::replace_first(config,
                             "syncURL = http://sync.scheduleworld.com/funambol/ds",
                             "syncURL = http://my.funambol.com/sync");

        boost::replace_first(config,
                             "WebURL = http://www.scheduleworld.com",
                             "WebURL = http://my.funambol.com");

        boost::replace_first(config,
                             "IconURI = image://themedimage/icons/services/scheduleworld",
                             "IconURI = image://themedimage/icons/services/funambol");

        boost::replace_first(config,
                             "# ConsumerReady = 0",
                             "ConsumerReady = 1");

        boost::replace_first(config,
                             "# enableWBXML = 1",
                             "enableWBXML = 0");

        boost::replace_first(config,
                             "# RetryInterval = 2M",
                             "RetryInterval = 0");

        boost::replace_first(config,
                             "addressbook/config.ini:uri = card3",
                             "addressbook/config.ini:uri = card");
        boost::replace_all(config,
                           "addressbook/config.ini:syncFormat = text/vcard",
                           "addressbook/config.ini:# syncFormat = ");

        boost::replace_first(config,
                             "calendar/config.ini:uri = cal2",
                             "calendar/config.ini:uri = event");
        boost::replace_all(config,
                           "calendar/config.ini:# syncFormat = ",
                           "calendar/config.ini:syncFormat = text/calendar");
        boost::replace_all(config,
                           "calendar/config.ini:# forceSyncFormat = 0",
                           "calendar/config.ini:forceSyncFormat = 1");

        boost::replace_first(config,
                             "todo/config.ini:uri = task2",
                             "todo/config.ini:uri = task");
        boost::replace_all(config,
                           "todo/config.ini:# syncFormat = ",
                           "todo/config.ini:syncFormat = text/calendar");
        boost::replace_all(config,
                           "todo/config.ini:# forceSyncFormat = 0",
                           "todo/config.ini:forceSyncFormat = 1");

        return config;
    }

    string SynthesisConfig() {
        string config = ScheduleWorldConfig();
        boost::replace_all(config, "/scheduleworld/", "/synthesis/");
        boost::replace_all(config, "PeerName = ScheduleWorld", "PeerName = Synthesis");

        boost::replace_first(config,
                             "syncURL = http://sync.scheduleworld.com/funambol/ds",
                             "syncURL = http://www.synthesis.ch/sync");

        boost::replace_first(config,
                             "WebURL = http://www.scheduleworld.com",
                             "WebURL = http://www.synthesis.ch");

        boost::replace_first(config,
                             "IconURI = image://themedimage/icons/services/scheduleworld",
                             "IconURI = image://themedimage/icons/services/synthesis");

        boost::replace_first(config,
                             "addressbook/config.ini:uri = card3",
                             "addressbook/config.ini:uri = contacts");
        boost::replace_all(config,
                           "addressbook/config.ini:syncFormat = text/vcard",
                           "addressbook/config.ini:# syncFormat = ");

        boost::replace_first(config,
                             "calendar/config.ini:uri = cal2",
                             "calendar/config.ini:uri = events");
        boost::replace_first(config,
                             "calendar/config.ini:sync = two-way",
                             "calendar/config.ini:sync = disabled");

        boost::replace_first(config,
                             "memo/config.ini:uri = note",
                             "memo/config.ini:uri = notes");

        boost::replace_first(config,
                             "todo/config.ini:uri = task2",
                             "todo/config.ini:uri = tasks");
        boost::replace_first(config,
                             "todo/config.ini:sync = two-way",
                             "todo/config.ini:sync = disabled");

        return config;
    }          

    /** create directory hierarchy, overwriting previous content */
    void createFiles(const string &root, const string &content, bool append = false) {
        if (!append) {
            rm_r(root);
        }

        size_t start = 0;
        ofstream out;
        string outname;

        out.exceptions(ios_base::badbit|ios_base::failbit);
        while (start < content.size()) {
            size_t delim = content.find(':', start);
            size_t end = content.find('\n', start);
            if (delim == content.npos ||
                end == content.npos) {
                // invalid content ?!
                break;
            }
            string newname = content.substr(start, delim - start);
            string line = content.substr(delim + 1, end - delim - 1);
            if (newname != outname) {
                if (out.is_open()) {
                    out.close();
                }
                string fullpath = root + "/" + newname;
                size_t fileoff = fullpath.rfind('/');
                mkdir_p(fullpath.substr(0, fileoff));
                out.open(fullpath.c_str(),
                         append ? (ios_base::out|ios_base::ate|ios_base::app) : (ios_base::out|ios_base::trunc));
                outname = newname;
            }
            out << line << endl;
            start = end + 1;
        }
    }

    /** turn directory hierarchy into string
     *
     * @param root       root path in file system
     * @param peer       if non-empty, then ignore all <root>/peers/<foo> directories
     *                   where <foo> != peer
     * @param onlyProps  ignore lines which are comments
     */
    string scanFiles(const string &root, const string &peer = "", bool onlyProps = true) {
        ostringstream out;

        scanFiles(root, "", peer, out, onlyProps);
        return out.str();
    }

    void scanFiles(const string &root, const string &dir, const string &peer, ostringstream &out, bool onlyProps) {
        string newroot = root;
        newroot += "/";
        newroot += dir;
        ReadDir readDir(newroot);
        sort(readDir.begin(), readDir.end());

        BOOST_FOREACH(const string &entry, readDir) {
            if (isDir(newroot + "/" + entry)) {
                if (boost::ends_with(newroot, "/peers") &&
                    !peer.empty() &&
                    entry != peer) {
                    // skip different peer directory
                    continue;
                } else {
                    scanFiles(root, dir + (dir.empty() ? "" : "/") + entry, peer, out, onlyProps);
                }
            } else {
                ifstream in;
                in.exceptions(ios_base::badbit /* failbit must not trigger exception because is set when reaching eof ?! */);
                in.open((newroot + "/" + entry).c_str());
                string line;
                while (!in.eof()) {
                    getline(in, line);
                    if ((line.size() || !in.eof()) && 
                        (!onlyProps ||
                         (boost::starts_with(line, "# ") ?
                          isPropAssignment(line.substr(2)) :
                          !line.empty()))) {
                        if (dir.size()) {
                            out << dir << "/";
                        }
                        out << entry << ":";
                        out << line << '\n';
                    }
                }
            }
        }
    }

    string printConfig(const string &server) {
        ScopedEnvChange templates("SYNCEVOLUTION_TEMPLATE_DIR", "templates");
        ScopedEnvChange xdg("XDG_CONFIG_HOME", m_testDir);
        ScopedEnvChange home("HOME", m_testDir);

        TestCmdline cmdline("--print-config", server.c_str(), NULL);
        cmdline.doit();
        CPPUNIT_ASSERT_EQUAL_DIFF("", cmdline.m_err.str());
        return cmdline.m_out.str();
    }
};

SYNCEVOLUTION_TEST_SUITE_REGISTRATION(CmdlineTest);

#endif // ENABLE_UNIT_TESTS

SE_END_CXX
