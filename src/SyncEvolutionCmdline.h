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

#include "SyncEvolutionConfig.h"
#include "FilterConfigNode.h"
#include "VolatileConfigNode.h"
#include "EvolutionSyncSource.h"
#include "EvolutionSyncClient.h"

#include <iostream>
#include <memory>
#include <set>
using namespace std;

#include <boost/shared_ptr.hpp>

class SyncEvolutionCmdline {
public:
    /**
     * @param out      stdout stream for normal messages
     * @param err      stderr stream for error messages
     */
    SyncEvolutionCmdline(int argc, char **argv, ostream &out, ostream &err) :
        m_argc(argc),
        m_argv(argv),
        m_out(out),
        m_err(err),
        m_validSyncProps(EvolutionSyncConfig::getRegistry()),
        m_validSourceProps(EvolutionSyncSourceConfig::getRegistry())
    {}

    /**
     * parse the command line options
     *
     * @retval true if command line was okay
     */
    bool parse() {
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
                if (!parseProp(m_validSyncProps, m_sourceProps,
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

    bool run() {
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
        } else if (m_server == "" && m_argc > 1) {
            // Options given, but no server - not sure what the user wanted?!
            usage(true, "server name missing");
            return false;
        } else if (m_argc == 1) {
            const struct { const char *mimeType, *kind; } kinds[] = {
                { "text/vcard",  "address books" },
                { "text/calendar", "calendars" },
                { "text/x-journal", "memos" },
                { "text/x-todo", "tasks" },
                { NULL }
            };

            boost::shared_ptr<FilterConfigNode> configNode(new VolatileConfigNode());
            boost::shared_ptr<FilterConfigNode> hiddenNode(new VolatileConfigNode());
            boost::shared_ptr<FilterConfigNode> trackingNode(new VolatileConfigNode());
            SyncSourceNodes nodes(configNode, hiddenNode, trackingNode);
            EvolutionSyncSourceParams params("list", nodes, "");
            for (int i = 0; kinds[i].mimeType; i++ ) {
                configNode->setProperty("type", kinds[i].mimeType);
                auto_ptr<EvolutionSyncSource> source(EvolutionSyncSource::createSource(params, false));
                if (source.get() != NULL) {
                    listSources(*source, kinds[i].kind);
                    cout << "\n";
                }
            }

            usage(m_argv, false);
        } else if (m_printConfig) {
            EvolutionSyncConfig config(m_server);
            boost::shared_ptr<const FilterConfigNode> syncProps(config.getProperties());
            dumpProperties(*syncProps, config.getRegistry());

            list<string> sources = config.getSyncSources();
            for (list<string>::const_iterator it = sources.begin();
                 it != sources.end();
                 ++it) {
                m_out << endl << "[" << *it << "]" << endl;
                boost::shared_ptr<PersistentEvolutionSyncSourceConfig> source(config.getSyncSourceConfig(*it));
                boost::shared_ptr<const FilterConfigNode> sourceProps(source->getProperties());
                dumpProperties(*sourceProps, source->getRegistry());
            }
        } else if (m_configure || m_migrate) {
            /** TODO */
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
    }

private:
    class Bool { 
    public:
        Bool(bool val = false) : m_value(val) {}
        operator bool () { return m_value; }
        Bool & operator = (bool val) { m_value = val; return *this; }
    private:
        bool m_value;
    };

    int m_argc;
    char **m_argv;
    ostream &m_out, &m_err;

    Bool m_quiet;
    Bool m_status;
    Bool m_version;
    Bool m_usage;
    Bool m_configure;
    Bool m_migrate;
    Bool m_printServers;
    Bool m_printConfig;
    FilterConfigNode::ConfigFilter m_syncProps, m_sourceProps;
    const ConfigPropertyRegistry &m_validSyncProps;
    const ConfigPropertyRegistry &m_validSourceProps;

    string m_server;
    string m_template;
    set<string> m_sources;

    /** compose description of cmd line option with optional parameter */
    static string cmdOpt(const char *opt, const char *param = NULL) {
        string res = "'";
        res += opt;
        if (param) {
            res += " ";
            res += param;
        }
        res += "'";
        return res;
    }

    /**
     * parse sync or source property
     *
     * @param validProps     list of valid properties
     * @retval props         add property name/value pair here
     * @param opt            command line option as it appeard in argv (e.g. --sync|--sync-property|-z)
     * @param param          the parameter following the opt, may be NULL if none given (error!)
     * @param propname       if given, then this is the property name and param contains the param value (--sync <param>)
     */
    bool parseProp(const ConfigPropertyRegistry &validProps,
                   FilterConfigNode::ConfigFilter &props,
                   const char *opt,
                   const char *param,
                   const char *propname = NULL) {
        if (!param) {
            usage(true, string("missing parameter for ") + cmdOpt(opt, param));
            return false;
        }
        if (!strcmp(param, "?")) {
            if (propname) {
                return listPropValues(validProps, propname);
            } else {
                return listProperties(validProps);
            }
        } else {
            string propstr;
            string paramstr;
            string baseopt; /**< the cmd line part without the parameter */
            if (propname) {
                propstr = propname;
                paramstr = param;
                baseopt = opt;
            } else {
                const char *equal = strchr(param, '=');
                if (!equal) {
                    usage(true, string("the '=<value>' part is missing in: ") + cmdOpt(opt, param));
                    return false;
                }
                propstr.assign(param, equal - param);
                paramstr.assign(equal + 1);
                baseopt = opt;
                baseopt += " ";
                baseopt += string(param, equal - param + 1);
            }

            /** TODO: sanity check */
            props.set(propstr, paramstr);

            if (false /* TODO */) {
                usage(true, string("invalid parameter: ") + cmdOpt(opt, param), baseopt);
                return false;
            }
        }
        
        return true;
    }

    bool listPropValues(const ConfigPropertyRegistry &validProps,
                        const string &propName) {
        /** TODO */
    }

    bool listProperties(const ConfigPropertyRegistry &validProps) {
        
    }

    /**
     * list all known data sources of a certain type
     */
    void listSources(EvolutionSyncSource &syncSource, const string &header) {
        m_out << header << ":\n";
        EvolutionSyncSource::sources sources = syncSource.getSyncBackends();

        for (EvolutionSyncSource::sources::const_iterator it = sources.begin();
             it != sources.end();
             it++) {
            m_out << it->m_name << " (" << it->m_uri << ")\n";
        }
    }

    void dumpProperties(const ConfigNode &configuredProps,
                        const ConfigPropertyRegistry &allProps) {
        for (ConfigPropertyRegistry::const_iterator it = allProps.begin();
             it != allProps.end();
             ++it) {
            m_out << endl;
            list<string> commentLines;
            ConfigProperty::splitComment((*it)->getComment(), commentLines);
            for (list<string>::const_iterator line = commentLines.begin();
                 line != commentLines.end();
                 ++line) {
                m_out << "# " << *line << endl;
            }
            m_out << (*it)->getName() << " = " << (*it)->getProperty(configuredProps) << endl;
        }
    }

    /** print usage information */
    void usage(bool full, string error = string(""), string param = string(""))
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
};
