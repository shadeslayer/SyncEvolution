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

#include <unistd.h>
#include <errno.h>

#include <iostream>
#include <sstream>
#include <memory>
#include <set>
#include <algorithm>
using namespace std;

#include <boost/shared_ptr.hpp>
#include <boost/algorithm/string.hpp>

SyncEvolutionCmdline::SyncEvolutionCmdline(int argc, const char * const * argv, ostream &out, ostream &err) :
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
        if (boost::iequals(m_argv[opt], "--sync") ||
            boost::iequals(m_argv[opt], "-s")) {
            opt++;
            string param;
            string cmdopt(m_argv[opt - 1]);
            if (!parseProp(m_validSourceProps, m_sourceProps,
                           m_argv[opt - 1], opt == m_argc ? NULL : m_argv[opt],
                           "sync")) {
                return false;
            }
        } else if(boost::iequals(m_argv[opt], "--sync-property") ||
                  boost::iequals(m_argv[opt], "-y")) {
                opt++;
                if (!parseProp(m_validSyncProps, m_syncProps,
                               m_argv[opt - 1], opt == m_argc ? NULL : m_argv[opt])) {
                    return false;
                }
        } else if(boost::iequals(m_argv[opt], "--source-property") ||
                  boost::iequals(m_argv[opt], "-z")) {
            opt++;
            if (!parseProp(m_validSourceProps, m_sourceProps,
                           m_argv[opt - 1], opt == m_argc ? NULL : m_argv[opt])) {
                return false;
            }
        } else if(boost::iequals(m_argv[opt], "--properties") ||
                  boost::iequals(m_argv[opt], "-r")) {
            opt++;
            /* TODO */
        } else if(boost::iequals(m_argv[opt], "--template") ||
                  boost::iequals(m_argv[opt], "-l")) {
            opt++;
            if (opt >= m_argc) {
                usage(true, string("missing parameter for ") + cmdOpt(m_argv[opt - 1]));
                return false;
            }
            m_template = m_argv[opt];
            if (m_template == "?") {
                dumpServers("Available configuration templates:",
                            EvolutionSyncConfig::getServerTemplates());
                m_dontrun = true;
            }
        } else if(boost::iequals(m_argv[opt], "--print-servers")) {
            m_printServers = true;
        } else if(boost::iequals(m_argv[opt], "--print-config") ||
                  boost::iequals(m_argv[opt], "-p")) {
            m_printConfig = true;
        } else if(boost::iequals(m_argv[opt], "--configure") ||
                  boost::iequals(m_argv[opt], "-c")) {
            m_configure = true;
        } else if(boost::iequals(m_argv[opt], "--migrate")) {
            m_migrate = true;
        } else if(boost::iequals(m_argv[opt], "--status") ||
                  boost::iequals(m_argv[opt], "-t")) {
            m_status = true;
        } else if(boost::iequals(m_argv[opt], "--quiet") ||
                  boost::iequals(m_argv[opt], "-q")) {
            m_quiet = true;
        } else if(boost::iequals(m_argv[opt], "--help") ||
                  boost::iequals(m_argv[opt], "-h")) {
            m_usage = true;
        } else if(boost::iequals(m_argv[opt], "--version")) {
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
    } else if (m_printServers || m_server == "?") {
        dumpServers("Configured servers:",
                    EvolutionSyncConfig::getServers());
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
                ConstSyncSourceNodes nodes = config->getSyncSourceNodes(*it);
                boost::shared_ptr<FilterConfigNode> sourceProps(new FilterConfigNode(boost::shared_ptr<const ConfigNode>(nodes.m_configNode)));
                sourceProps->setFilter(m_sourceProps);
                dumpProperties(*sourceProps, EvolutionSyncSourceConfig::getRegistry());
            }
        }
    } else if (m_server == "" && m_argc > 1) {
        // Options given, but no server - not sure what the user wanted?!
        usage(true, "server name missing");
        return false;
    } else if (m_configure || m_migrate) {
        // Both config changes and migration are implemented as copying from
        // another config (template resp. old one). Migration also moves
        // the old config.
        boost::shared_ptr<EvolutionSyncConfig> from;
        if (m_migrate) {
            from.reset(new EvolutionSyncConfig(m_server));
            if (!from->exists()) {
                cerr << "ERROR: server '" << m_server << "' has not been configured yet." << endl;
                return false;
            }

            int counter = 0;
            string oldRoot = from->getRootPath();
            string suffix;
            while (true) {
                string newname;
                ostringstream newsuffix;
                newsuffix << ".old";
                if (counter) {
                    newsuffix << "." << counter;
                }
                suffix = newsuffix.str();
                newname = oldRoot + suffix;
                if (!rename(oldRoot.c_str(),
                            newname.c_str())) {
                    break;
                } else if (errno != EEXIST && errno != ENOTEMPTY) {
                    m_err << "ERROR: renaming " << oldRoot << " to " <<
                        newname << ": " << strerror(errno) << endl;
                    return false;
                }
                counter++;
            }

            from.reset(new EvolutionSyncConfig(m_server + suffix));
        } else {
            from.reset(new EvolutionSyncConfig(m_server));
            if (!from->exists()) {
                // creating from scratch, look for template
                string configTemplate = m_template.empty() ? m_server : m_template;
                from = EvolutionSyncConfig::createServerTemplate(configTemplate);
                if (!from.get()) {
                    cerr << "ERROR: no configuration template for '" << configTemplate << "' available." << endl;
                    dumpServers("Available configuration templates:",
                                EvolutionSyncConfig::getServerTemplates());
                    return false;
                }
            }
        }

        // apply config changes on-the-fly
        from->setConfigFilter(true, m_syncProps);
        from->setConfigFilter(false, m_sourceProps);

        // write into the requested configuration, creating it if necessary
        boost::shared_ptr<EvolutionSyncConfig> to(new EvolutionSyncConfig(m_server));
        to->copy(*from, (m_migrate || m_sources.empty()) ? NULL : &m_sources);

        // done, now write it
        to->flush();
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

void SyncEvolutionCmdline::dumpServers(const string &preamble,
                                       const EvolutionSyncConfig::ServerList &servers)
{
    m_out << preamble << endl;
    for (EvolutionSyncConfig::ServerList::const_iterator it = servers.begin();
         it != servers.end();
         ++it) {
        m_out << "   "  << it->first << " = " << it->second << endl;
    }
    if (!servers.size()) {
        m_out << "   none" << endl;
    }
}

void SyncEvolutionCmdline::dumpProperties(const ConfigNode &configuredProps,
                                          const ConfigPropertyRegistry &allProps)
{
    for (ConfigPropertyRegistry::const_iterator it = allProps.begin();
         it != allProps.end();
         ++it) {
        if ((*it)->isHidden()) {
            continue;
        }
        if (!m_quiet) {
            string comment = (*it)->getComment();
            if (!comment.empty()) {
                m_out << endl;
                dumpComment(m_out, "# ", comment);
            }
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
    ostream &out(error.empty() ? m_out : m_err);

    out << m_argv[0] << endl;
    out << m_argv[0] << " [<options>] <server> [<source> ...]" << endl;
    out << m_argv[0] << " --help|-h" << endl;
    out << m_argv[0] << " --version" << endl;
    if (full) {
        out << endl <<
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
        out << endl << "ERROR: " << error << endl;
    }
    if (param != "") {
        out << "INFO: use '" << param << (param[param.size() - 1] == '=' ? "" : " ") <<
            "?' to get a list of valid parameters" << endl;
    }
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

    while (lit != string_split_iterator()) {
        res << "> " << *rit << endl;
        ++rit;
    }

    return res.str();
}

# define CPPUNIT_ASSERT_EQUAL_DIFF( expected, actual )      \
    do { \
        if (!CPPUNIT_NS::assertion_traits<string>::equal(expected,actual)) {     \
            CPPUNIT_NS::Message cpputMsg_(string("expected:\n") +       \
                                          expected);                    \
            cpputMsg_.addDetail(string("actual:\n") +                   \
                                actual);                                \
            cpputMsg_.addDetail(string("diff:\n") +                     \
                                diffStrings(expected, actual));         \
            CPPUNIT_NS::Asserter::fail( cpputMsg_,                      \
                                        CPPUNIT_SOURCELINE() );         \
        } \
    } while ( false )


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
class SyncEvolutionCmdlineTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(SyncEvolutionCmdlineTest);
    CPPUNIT_TEST(testFramework);
    CPPUNIT_TEST(testSetupScheduleWorld);
    CPPUNIT_TEST(testSetupFunambol);
    CPPUNIT_TEST(testSetupSynthesis);
    CPPUNIT_TEST(testTemplate);
    
    CPPUNIT_TEST_SUITE_END();
    
public:
    SyncEvolutionCmdlineTest() :
        m_testDir("SyncEvolutionCmdlineTest"),
        m_scheduleWorldConfig(".internal.ini:serverNonce = \n"
                              ".internal.ini:clientNonce = \n"
                              ".internal.ini:devInfoHash = \n"
                              "config.ini:syncURL = http://sync.scheduleworld.com\n"
                              "config.ini:deviceId = \n"
                              "config.ini:username = your SyncML server account name\n"
                              "config.ini:password = your SyncML server password\n"
                              "config.ini:logdir = \n"
                              "config.ini:loglevel = 0\n"
                              "config.ini:maxlogdirs = 0\n"
                              "config.ini:useProxy = F\n"
                              "config.ini:proxyHost = \n"
                              "config.ini:proxyUsername = \n"
                              "config.ini:proxyPassword = \n"
                              "config.ini:clientAuthType = md5\n"
                              "config.ini:maxMsgSize = 8192\n"
                              "config.ini:maxObjSize = 500000\n"
                              "config.ini:loSupport = T\n"
                              "config.ini:enableCompression = F\n"
                              "addressbook/.internal.ini:last = \n"
                              "addressbook/config.ini:sync = two-way\n"
                              "addressbook/config.ini:type = addressbook\n"
                              "addressbook/config.ini:evolutionsource = \n"
                              "addressbook/config.ini:uri = card3\n"
                              "addressbook/config.ini:evolutionuser = \n"
                              "addressbook/config.ini:evolutionpassword = \n"
                              "addressbook/config.ini:encoding = \n"
                              "calendar/.internal.ini:last = \n"
                              "calendar/config.ini:sync = two-way\n"
                              "calendar/config.ini:type = calendar\n"
                              "calendar/config.ini:evolutionsource = \n"
                              "calendar/config.ini:uri = event2\n"
                              "calendar/config.ini:evolutionuser = \n"
                              "calendar/config.ini:evolutionpassword = \n"
                              "calendar/config.ini:encoding = \n"
                              "memo/.internal.ini:last = \n"
                              "memo/config.ini:sync = two-way\n"
                              "memo/config.ini:type = memo\n"
                              "memo/config.ini:evolutionsource = \n"
                              "memo/config.ini:uri = note\n"
                              "memo/config.ini:evolutionuser = \n"
                              "memo/config.ini:evolutionpassword = \n"
                              "memo/config.ini:encoding = \n"
                              "todo/.internal.ini:last = \n"
                              "todo/config.ini:sync = two-way\n"
                              "todo/config.ini:type = todo\n"
                              "todo/config.ini:evolutionsource = \n"
                              "todo/config.ini:uri = task2\n"
                              "todo/config.ini:evolutionuser = \n"
                              "todo/config.ini:evolutionpassword = \n"
                              "todo/config.ini:encoding = \n")
    {}

protected:

    /** verify that createFiles/scanFiles themselves work */
    void testFramework() {
        const string root(m_testDir);
        const string content("baz:line\n"
                             "caz/subdir:booh\n"
                             "foo:bar1\n"
                             "foo:bar2\n");
        createFiles(root, content);
        string res = scanFiles(root);
        CPPUNIT_ASSERT_EQUAL_DIFF(content, res);
    }

    /** create new configurations */
    void testSetupScheduleWorld() {
        string root;
        ScopedEnvChange xdg("XDG_CONFIG_HOME", m_testDir);
        ScopedEnvChange home("HOME", m_testDir);

        root = m_testDir;
        root += "/syncevolution/scheduleworld";
        rm_r(root);
        TestCmdline cmdline("--configure",
                            "--template", "scheduleworld",
                            "scheduleworld",
                            NULL);
        cmdline.doit();
        string res = scanFiles(root);
        CPPUNIT_ASSERT_EQUAL_DIFF(string(m_scheduleWorldConfig), res);
    }
    void testSetupFunambol() {
        string root;
        ScopedEnvChange xdg("XDG_CONFIG_HOME", m_testDir);
        ScopedEnvChange home("HOME", m_testDir);

        root = m_testDir;
        root += "/syncevolution/funambol";
        rm_r(root);
        TestCmdline cmdline("--configure",
                            "--template", "funambol",
                            "funambol",
                            NULL);
        cmdline.doit();
        string res = scanFiles(root);
        string expected = m_scheduleWorldConfig;
        boost::replace_first(expected,
                             "syncURL = http://sync.scheduleworld.com",
                             "syncURL = http://my.funambol.com");

        boost::replace_first(expected,
                             "addressbook/config.ini:uri = card3",
                             "addressbook/config.ini:uri = card");
        boost::replace_first(expected,
                             "addressbook/config.ini:type = addressbook",
                             "addressbook/config.ini:type = addressbook:text/x-vcard");

        boost::replace_first(expected,
                             "calendar/config.ini:uri = event2",
                             "calendar/config.ini:uri = event");
        boost::replace_first(expected,
                             "calendar/config.ini:sync = two-way",
                             "calendar/config.ini:sync = disabled");

        boost::replace_first(expected,
                             "memo/config.ini:sync = two-way",
                             "memo/config.ini:sync = disabled");

        boost::replace_first(expected,
                             "todo/config.ini:uri = task2",
                             "todo/config.ini:uri = task");
        boost::replace_first(expected,
                             "todo/config.ini:sync = two-way",
                             "todo/config.ini:sync = disabled");
        CPPUNIT_ASSERT_EQUAL_DIFF(expected, res);
    }

    void testSetupSynthesis() {
        string root;
        ScopedEnvChange xdg("XDG_CONFIG_HOME", m_testDir);
        ScopedEnvChange home("HOME", m_testDir);

        root = m_testDir;
        root += "/syncevolution/synthesis";
        rm_r(root);
        TestCmdline cmdline("--configure",
                            "--template", "synthesis",
                            "synthesis",
                            NULL);
        cmdline.doit();
        string res = scanFiles(root);
        string expected = m_scheduleWorldConfig;
        boost::replace_first(expected,
                             "syncURL = http://sync.scheduleworld.com",
                             "syncURL = http://www.synthesis.ch/sync");

        boost::replace_first(expected,
                             "addressbook/config.ini:uri = card3",
                             "addressbook/config.ini:uri = contacts");

        boost::replace_first(expected,
                             "calendar/config.ini:uri = event2",
                             "calendar/config.ini:uri = events");
        boost::replace_first(expected,
                             "calendar/config.ini:sync = two-way",
                             "calendar/config.ini:sync = disabled");

        boost::replace_first(expected,
                             "memo/config.ini:uri = note",
                             "memo/config.ini:uri = notes");

        boost::replace_first(expected,
                             "todo/config.ini:uri = task2",
                             "todo/config.ini:uri = tasks");
        boost::replace_first(expected,
                             "todo/config.ini:sync = two-way",
                             "todo/config.ini:sync = disabled");
        CPPUNIT_ASSERT_EQUAL_DIFF(expected, res);
    }

    void testTemplate() {
        TestCmdline failure("--template", NULL);
        CPPUNIT_ASSERT(!failure.m_cmdline->parse());
        CPPUNIT_ASSERT_EQUAL_DIFF("", failure.m_out.str());
        CPPUNIT_ASSERT(boost::ends_with(failure.m_err.str(), "ERROR: missing parameter for '--template'\n"));

        TestCmdline help("--template", "?", NULL);
        help.doit();
        CPPUNIT_ASSERT_EQUAL_DIFF("Available configuration templates:\n"
                                  "   funambol = http://my.funambol.com\n"
                                  "   scheduleworld = http://sync.scheduleworld.com\n"
                                  "   synthesis = http://www.synthesis.ch\n",
                                  help.m_out.str());
        CPPUNIT_ASSERT_EQUAL_DIFF("", help.m_err.str());
    }


    const string m_testDir;
    const string m_scheduleWorldConfig;
        

private:

    /**
     * vararg constructor with NULL termination,
     * out and error stream into stringstream members
     */
    class TestCmdline {
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

            m_argv.reset(new const char *[m_argvstr.size() + 1]);
            m_argv[0] = "client-test";
            for (size_t index = 0;
                 index < m_argvstr.size();
                 ++index) {
                m_argv[index + 1] = m_argvstr[index].c_str();
            }

            m_cmdline.set(new SyncEvolutionCmdline(m_argvstr.size() + 1, m_argv.get(), m_out, m_err), "cmdline");
        }

        void doit() {
            bool success;
            success = m_cmdline->parse() &&
                m_cmdline->run();
            if (m_err.str().size()) {
                cerr << endl << m_err.str();
            }
            CPPUNIT_ASSERT(success);
        }

        ostringstream m_out, m_err;
        cxxptr<SyncEvolutionCmdline> m_cmdline;

    private:
        vector<string> m_argvstr;
        boost::scoped_array<const char *> m_argv;
    };

    /** temporarily set env variable, restore old value on destruction */
    class ScopedEnvChange {
    public:
        ScopedEnvChange(const string &var, const string &value) :
            m_var(var)
        {
            const char *oldval = getenv(var.c_str());
            if (oldval) {
                m_oldvalset = true;
                m_oldval = oldval;
            } else {
                m_oldvalset = false;
            }
            setenv(var.c_str(), value.c_str(), 1);
        }
        ~ScopedEnvChange()
        {
            if (m_oldvalset) {
                setenv(m_var.c_str(), m_oldval.c_str(), 1);
            } else {
                unsetenv(m_var.c_str());
            } 
        }
    private:
        string m_var, m_oldval;
        bool m_oldvalset;
    };
            

    /** create directory hierarchy, overwriting previous content */
    void createFiles(const string &root, const string &content) {
        rm_r(root);

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
                out.open(fullpath.c_str());
                outname = newname;
            }
            out << line << endl;
            start = end + 1;
        }
    }

    /** turn directory hierarchy into string */
    string scanFiles(const string &root, bool onlyProps = true) {
        ostringstream out;

        scanFiles(root, "", out, onlyProps);
        return out.str();
    }
    void scanFiles(const string &root, const string &dir, ostringstream &out, bool onlyProps) {
        string newroot = root;
        newroot += "/";
        newroot += dir;
        ReadDir readDir(newroot);
        sort(readDir.begin(), readDir.end());

        for (ReadDir::const_iterator it = readDir.begin();
             it != readDir.end();
             ++it) {
            if (isDir(newroot + "/" + *it)) {
                scanFiles(newroot, *it, out, onlyProps);
            } else {
                ifstream in;
                in.exceptions(ios_base::badbit /* failbit must not trigger exception because is set when reaching eof ?! */);
                in.open((newroot + "/" + *it).c_str());
                string line;
                while (!in.eof()) {
                    getline(in, line);
                    if ((line.size() || !in.eof()) && 
                        (!onlyProps ||
                         line.size() && line[0] != '#')) {
                        if (dir.size()) {
                            out << dir << "/";
                        }
                        out << *it << ":";
                        out << line << '\n';
                    }
                }
            }
        }
    }
};

SYNCEVOLUTION_TEST_SUITE_REGISTRATION(SyncEvolutionCmdlineTest);

#endif // ENABLE_UNIT_TESTS
