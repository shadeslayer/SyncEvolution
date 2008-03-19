/*
 * Copyright (C) 2008 Patrick Ohly
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY, TITLE, NONINFRINGEMENT or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307  USA
 */

#include "SyncEvolutionCmdline.h"
#include "FilterConfigNode.h"
#include "VolatileConfigNode.h"
#include "EvolutionSyncSource.h"
#include "EvolutionSyncClient.h"
#include "SyncEvolutionUtil.h"

#include <iostream>
#include <memory>
#include <set>
#include <algorithm>
using namespace std;

#include <boost/shared_ptr.hpp>

SyncEvolutionCmdline::SyncEvolutionCmdline(int argc, char **argv, ostream &out, ostream &err) :
    m_argc(argc),
    m_argv(argv),
    m_out(out),
    m_err(err),
    m_validSyncProps(EvolutionSyncConfig::getRegistry()),
    m_validSourceProps(EvolutionSyncSourceConfig::getRegistry())
{}

bool SyncEvolutionCmdline::parse()
{
    int opt = 1;
    while (opt < m_argc) {
        if (m_argv[opt][0] != '-') {
            break;
        }
        if (!strcasecmp(m_argv[opt], "--sync") ||
            !strcasecmp(m_argv[opt], "-s")) {
            opt++;
            string param;
            string cmdopt(m_argv[opt - 1]);
            if (!parseProp(m_validSourceProps, m_sourceProps,
                           m_argv[opt - 1], opt == m_argc ? NULL : m_argv[opt],
                           "sync")) {
                return false;
            }
        } else if(!strcasecmp(m_argv[opt], "--sync-property") ||
                  !strcasecmp(m_argv[opt], "-y")) {
                opt++;
                if (!parseProp(m_validSyncProps, m_syncProps,
                               m_argv[opt - 1], opt == m_argc ? NULL : m_argv[opt])) {
                    return false;
                }
        } else if(!strcasecmp(m_argv[opt], "--source-property") ||
                  !strcasecmp(m_argv[opt], "-z")) {
            opt++;
            if (!parseProp(m_validSourceProps, m_sourceProps,
                           m_argv[opt - 1], opt == m_argc ? NULL : m_argv[opt])) {
                return false;
            }
        } else if(!strcasecmp(m_argv[opt], "--properties") ||
                  !strcasecmp(m_argv[opt], "-r")) {
            opt++;
            /* TODO */
        } else if(!strcasecmp(m_argv[opt], "--template") ||
                  !strcasecmp(m_argv[opt], "-l")) {
            opt++;
            if (opt >= m_argc) {
                usage(true, string("missing parameter for ") + cmdOpt(m_argv[opt - 1]));
                return false;
            }
            m_template = m_argv[opt];
        } else if(!strcasecmp(m_argv[opt], "--print-servers")) {
            m_printServers = true;
        } else if(!strcasecmp(m_argv[opt], "--print-config") ||
                  !strcasecmp(m_argv[opt], "-p")) {
            m_printConfig = true;
        } else if(!strcasecmp(m_argv[opt], "--configure") ||
                  !strcasecmp(m_argv[opt], "-c")) {
            m_configure = true;
        } else if(!strcasecmp(m_argv[opt], "--migrate")) {
            m_migrate = true;
        } else if(!strcasecmp(m_argv[opt], "--status") ||
                  !strcasecmp(m_argv[opt], "-t")) {
            m_status = true;
        } else if(!strcasecmp(m_argv[opt], "--quiet") ||
                  !strcasecmp(m_argv[opt], "-q")) {
            m_quiet = true;
        } else if(!strcasecmp(m_argv[opt], "--help") ||
                  !strcasecmp(m_argv[opt], "-h")) {
            m_usage = true;
        } else if(!strcasecmp(m_argv[opt], "--version")) {
            m_version = true;
        } else {
            usage(false, string(m_argv[opt]) + ": unknown parameter");
            return false;
        }
        opt++;
    }

    if (opt < m_argc) {
        m_server = m_argv[opt++];
        while (opt < m_argc) {
            m_sources.insert(m_argv[opt++]);
        }
    }

    return true;
}

bool SyncEvolutionCmdline::run() {
    if (m_usage) {
        usage(true);
    } else if (m_version) {
        printf("SyncEvolution %s\n", VERSION);
    } else if (m_printServers) {
        EvolutionSyncConfig::ServerList servers = EvolutionSyncConfig::getServers();
        m_out << "Configured servers:" << endl;
        for (EvolutionSyncConfig::ServerList::const_iterator it = servers.begin();
             it != servers.end();
             ++it) {
            m_out << "   "  << it->first << " = " << it->second << endl;
        }
        if (!servers.size()) {
            m_out << "   none" << endl;
        }
    } else if (m_dontrun) {
        // user asked for information
    } else if (m_argc == 1) {
        const SourceRegistry &registry(EvolutionSyncSource::getSourceRegistry());
        boost::shared_ptr<FilterConfigNode> configNode(new VolatileConfigNode());
        boost::shared_ptr<FilterConfigNode> hiddenNode(new VolatileConfigNode());
        boost::shared_ptr<FilterConfigNode> trackingNode(new VolatileConfigNode());
        SyncSourceNodes nodes(configNode, hiddenNode, trackingNode);
        EvolutionSyncSourceParams params("list", nodes, "");
        
        for (SourceRegistry::const_iterator source = registry.begin();
             source != registry.end();
             ++source) {
            for (Values::const_iterator alias = (*source)->m_typeValues.begin();
                 alias != (*source)->m_typeValues.end();
                 ++alias) {
                if (!alias->empty() && (*source)->m_enabled) {
                    configNode->setProperty("type", *alias->begin());
                    auto_ptr<EvolutionSyncSource> source(EvolutionSyncSource::createSource(params, false));
                    if (source.get() != NULL) {
                        listSources(*source, join(" = ", alias->begin(), alias->end()));
                        cout << "\n";
                    }
                }
            }
        }

        usage(false);
    } else if (m_printConfig) {
        boost::shared_ptr<EvolutionSyncConfig> config;

        if (m_template.empty()) {
            config.reset(new EvolutionSyncConfig(m_server));
            if (!config->exists()) {
                cerr << "ERROR: server '" << m_server << "' has not been configured yet." << endl;
                return false;
            }
        } else {
            config = EvolutionSyncConfig::createServerTemplate(m_template);
            if (!config.get()) {
                cerr << "ERROR: no configuration template for '" << m_template << "' available." << endl;
                return false;
            }
        }

        if (m_sources.empty()) {
            boost::shared_ptr<FilterConfigNode> syncProps(config->getProperties());
            syncProps->setFilter(m_syncProps);
            dumpProperties(*syncProps, config->getRegistry());
        }

        list<string> sources = config->getSyncSources();
        for (list<string>::const_iterator it = sources.begin();
             it != sources.end();
             ++it) {
            if (m_sources.empty() ||
                m_sources.find(*it) != m_sources.end()) {
                m_out << endl << "[" << *it << "]" << endl;
                boost::shared_ptr<PersistentEvolutionSyncSourceConfig> source(config->getSyncSourceConfig(*it));
                boost::shared_ptr<FilterConfigNode> sourceProps(source->getProperties());
                sourceProps->setFilter(m_sourceProps);
                dumpProperties(*sourceProps, source->getRegistry());
            }
        }
    } else if (m_server == "" && m_argc > 1) {
        // Options given, but no server - not sure what the user wanted?!
        usage(true, "server name missing");
        return false;
    } else if (m_configure || m_migrate) {
        
    } else {
        EvolutionSyncClient client(m_server, true, m_sources);
        client.setQuiet(m_quiet);
        client.setConfigFilter(true, m_syncProps);
        client.setConfigFilter(false, m_sourceProps);
        if (m_status) {
            client.status();
        } else {
            client.sync();
        }
    }

    return true;
}

string SyncEvolutionCmdline::cmdOpt(const char *opt, const char *param)
{
    string res = "'";
    res += opt;
    if (param) {
        res += " ";
        res += param;
    }
    res += "'";
    return res;
}

bool SyncEvolutionCmdline::parseProp(const ConfigPropertyRegistry &validProps,
                                     FilterConfigNode::ConfigFilter &props,
                                     const char *opt,
                                     const char *param,
                                     const char *propname)
{
    if (!param) {
        usage(true, string("missing parameter for ") + cmdOpt(opt, param));
        return false;
    } else if (!strcmp(param, "?")) {
        m_dontrun = true;
        if (propname) {
            return listPropValues(validProps, propname, opt);
        } else {
            return listProperties(validProps, opt);
        }
    } else {
        string propstr;
        string paramstr;
        if (propname) {
            propstr = propname;
            paramstr = param;
        } else {
            const char *equal = strchr(param, '=');
            if (!equal) {
                usage(true, string("the '=<value>' part is missing in: ") + cmdOpt(opt, param));
                return false;
            }
            propstr.assign(param, equal - param);
            paramstr.assign(equal + 1);
        }

        if (paramstr == "?") {
            m_dontrun = true;
            return listPropValues(validProps, propstr, cmdOpt(opt, param));
        } else {
            const ConfigProperty *prop = validProps.find(propstr);
            if (!prop) {
                m_err << "ERROR: " << cmdOpt(opt, param) << ": no such property" << endl;
                return false;
            } else {
                string error;
                if (!prop->checkValue(paramstr, error)) {
                    m_err << "ERROR: " << cmdOpt(opt, param) << ": " << error << endl;
                    return false;
                } else {
                    props.set(propstr, paramstr);
                    return true;                        
                }
            }
        }
    }
}

bool SyncEvolutionCmdline::listPropValues(const ConfigPropertyRegistry &validProps,
                                          const string &propName,
                                          const string &opt)
{
    const ConfigProperty *prop = validProps.find(propName);
    if (!prop) {
        m_err << "ERROR: "<< opt << ": no such property" << endl;
        return false;
    } else {
        m_out << opt << endl;
        string comment = prop->getComment();

        if (comment != "") {
            list<string> commentLines;
            ConfigProperty::splitComment(comment, commentLines);
            for (list<string>::const_iterator line = commentLines.begin();
                 line != commentLines.end();
                 ++line) {
                m_out << "   " << *line << endl;
            }
        } else {
            m_out << "   no documentation available";
        }
        return true;
    }
}

bool SyncEvolutionCmdline::listProperties(const ConfigPropertyRegistry &validProps,
                                          const string &opt)
{
    // The first of several related properties has a comment.
    // Remember that comment and print it as late as possible,
    // that way related properties preceed their comment.
    string comment;
    for (ConfigPropertyRegistry::const_iterator prop = validProps.begin();
         prop != validProps.end();
         ++prop) {
        if (!(*prop)->isHidden()) {
            string newComment = (*prop)->getComment();

            if (newComment != "") {
                dumpComment(m_out, "   ", comment);
                m_out << endl;
                comment = newComment;
            }
            m_out << (*prop)->getName() << ":" << endl;
        }
    }
    dumpComment(m_out, "   ", comment);
    return true;
}

void SyncEvolutionCmdline::listSources(EvolutionSyncSource &syncSource, const string &header)
{
    m_out << header << ":\n";
    EvolutionSyncSource::sources sources = syncSource.getSyncBackends();

    for (EvolutionSyncSource::sources::const_iterator it = sources.begin();
         it != sources.end();
         it++) {
        m_out << "   " << it->m_name << " (" << it->m_uri << ")\n";
    }
}

void SyncEvolutionCmdline::dumpProperties(const ConfigNode &configuredProps,
                                          const ConfigPropertyRegistry &allProps)
{
    for (ConfigPropertyRegistry::const_iterator it = allProps.begin();
         it != allProps.end();
         ++it) {
        if (!m_quiet) {
            m_out << endl;
            dumpComment(m_out, "# ", (*it)->getComment());
        }
        m_out << (*it)->getName() << " = " << (*it)->getProperty(configuredProps) << endl;
    }
}

void SyncEvolutionCmdline::dumpComment(ostream &stream,
                                       const string &prefix,
                                       const string &comment)
{
    list<string> commentLines;
    ConfigProperty::splitComment(comment, commentLines);
    for (list<string>::const_iterator line = commentLines.begin();
         line != commentLines.end();
         ++line) {
        stream << prefix << *line << endl;
    }
}

void SyncEvolutionCmdline::usage(bool full, const string &error, const string &param)
{
    m_out << m_argv[0] << endl;
    m_out << m_argv[0] << " [<options>] <server> [<source> ...]" << endl;
    m_out << m_argv[0] << " --help|-h" << endl;
    m_out << m_argv[0] << " --version" << endl;
    if (full) {
        m_out << endl <<
            "Options:" << endl <<
            "  --sync|-s <mode>" << endl <<
            "    Temporarily synchronize the active sources in that mode. Useful" << endl <<
            "    for a 'refresh-from-server' or 'refresh-from-client' sync which" << endl <<
            "    clears all data at one end and copies all items from the other." << endl <<
            "  " << endl <<
            "  --status|-t" << endl <<
            "    The changes made to local data since the last synchronization are" << endl <<
            "    shown without starting a new one. This can be used to see in advance" << endl <<
            "    whether the local data needs to be synchronized with the server." << endl <<
            "  " << endl <<
            "  --quiet|-q" << endl <<
            "    Suppresses most of the normal output during a synchronization. The" << endl <<
            "    log file still contains all the information." << endl <<
            "  " << endl <<
            "  --help|-h" << endl <<
            "    Prints usage information." << endl <<
            "  " << endl <<
            "  --version" << endl <<
            "    Prints the SyncEvolution version." << endl;
    }

    if (error != "") {
        m_out << endl << "ERROR: " << error << endl;
    }
    if (param != "") {
        m_out << "INFO: use '" << param << (param[param.size() - 1] == '=' ? "" : " ") <<
            "?' to get a list of valid parameters" << endl;
    }
}
