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

#include <syncevo/SyncConfig.h>
#include <syncevo/SyncSource.h>
#include <syncevo/SyncContext.h>
#include <syncevo/FileConfigTree.h>
#include <syncevo/VolatileConfigTree.h>
#include <syncevo/VolatileConfigNode.h>
#include <syncevo/DevNullConfigNode.h>
#include <syncevo/MultiplexConfigNode.h>
#include <syncevo/SingleFileConfigTree.h>
#include <syncevo/lcs.h>
#include <test.h>
#include <synthesis/timeutil.h>

#include <boost/foreach.hpp>
#include <iterator>
#include <algorithm>
#include <functional>
#include <queue>

#include <unistd.h>
#include "config.h"

#include <syncevo/declarations.h>
SE_BEGIN_CXX

const char *const SourceAdminDataName = "adminData";

static bool SourcePropSourceTypeIsSet(boost::shared_ptr<SyncSourceConfig> source);
static bool SourcePropURIIsSet(boost::shared_ptr<SyncSourceConfig> source);
static bool SourcePropSyncIsSet(boost::shared_ptr<SyncSourceConfig> source);

void ConfigProperty::splitComment(const string &comment, list<string> &commentLines)
{
    size_t start = 0;

    while (true) {
        size_t end = comment.find('\n', start);
        if (end == comment.npos) {
            commentLines.push_back(comment.substr(start));
            break;
        } else {
            commentLines.push_back(comment.substr(start, end - start));
            start = end + 1;
        }
    }
}

void ConfigProperty::throwValueError(const ConfigNode &node, const string &name, const string &value, const string &error) const
{
    SyncContext::throwError(node.getName() + ": " + name + " = " + value + ": " + error);
}

string SyncConfig::normalizeConfigString(const string &config)
{
    string normal = config;
    boost::to_lower(normal);
    BOOST_FOREACH(char &character, normal) {
        if (!isprint(character) ||
            character == '/' ||
            character == '\\' ||
            character == ':') {
            character = '_';
        }
    }
    if (boost::ends_with(normal, "@default")) {
        normal.resize(normal.size() - strlen("@default"));
    } else if (boost::ends_with(normal, "@")) {
        normal.resize(normal.size() - 1);
    } else {
        size_t at = normal.rfind('@');
        if (at == normal.npos) {
            // No explicit context. Pick the first server which matches
            // when ignoring their context. Peer list is sorted by name,
            // therefore shorter config names (= without context) are
            // found first, as intended.
            BOOST_FOREACH(const StringPair &entry, getConfigs()) {
                string entry_peer, entry_context;
                splitConfigString(entry.first, entry_peer, entry_context);
                if (normal == entry_peer) {
                    // found a matching, existing config, use it
                    normal = entry.first;
                    break;
                }
            }
        }
    }

    if (normal.empty()) {
        // default context is meant with the empty string,
        // better make that explicit
        normal = "@default";
    }

    return normal;
}

bool SyncConfig::splitConfigString(const string &config, string &peer, string &context)
{
    string::size_type at = config.rfind('@');
    if (at != config.npos) {
        peer = config.substr(0, at);
        context = config.substr(at + 1);
        return true;
    } else {
        peer = config;
        context = "default";
        return false;
    }    
}

SyncConfig::SyncConfig() :
    m_layout(HTTP_SERVER_LAYOUT) // use more compact layout with shorter paths and less source nodes
{
    // initialize properties
    SyncConfig::getRegistry();
    SyncSourceConfig::getRegistry();

    m_peerPath =
        m_contextPath = "volatile";
    makeVolatile();
}

void SyncConfig::makeVolatile()
{
    m_tree.reset(new VolatileConfigTree());
    m_peerNode.reset(new VolatileConfigNode());
    m_hiddenPeerNode = m_peerNode;
    m_globalNode = m_peerNode;
    m_contextNode = m_peerNode;
    m_contextHiddenNode = m_peerNode;
    m_props[false] = m_peerNode;
    m_props[true] = m_peerNode;
}

SyncConfig::SyncConfig(const string &peer,
                       boost::shared_ptr<ConfigTree> tree) :
    m_layout(SHARED_LAYOUT)
{
    // initialize properties
    SyncConfig::getRegistry();
    SyncSourceConfig::getRegistry();

    string root;

    m_peer = normalizeConfigString(peer);

    // except for SHARED_LAYOUT (set below),
    // everything is below the directory called like
    // the peer
    m_peerPath =
        m_contextPath = 
        m_peer;

    if (tree.get() != NULL) {
        // existing tree points into simple configuration
        m_tree = tree;
        m_layout = HTTP_SERVER_LAYOUT;
        m_peerPath =
            m_contextPath = "";
    } else {
        // search for configuration in various places...
        root = getOldRoot();
        string path = root + "/" + m_peerPath;
        if (!access((path + "/spds/syncml/config.txt").c_str(), F_OK)) {
            m_layout = SYNC4J_LAYOUT;
        } else {
            root = getNewRoot();
            path = root + "/" + m_peerPath;
            if (!access((path + "/config.ini").c_str(), F_OK) &&
                !access((path + "/sources").c_str(), F_OK) &&
                access((path + "/peers").c_str(), F_OK)) {
                m_layout = HTTP_SERVER_LAYOUT;
            } else {
                // check whether config name specifies a context,
                // otherwise use "default"
                splitConfigString(m_peer, m_peerPath, m_contextPath);
                if (!m_peerPath.empty()) {
                    m_peerPath = m_contextPath + "/peers/" + m_peerPath;
                }
            }
        }
        m_tree.reset(new FileConfigTree(root,
                                        m_peerPath.empty() ? m_contextPath : m_peerPath,
                                        m_layout));
    }

    string path;
    boost::shared_ptr<ConfigNode> node;
    switch (m_layout) {
    case SYNC4J_LAYOUT:
        // all properties reside in the same node
        path = m_peerPath + "/spds/syncml";
        node = m_tree->open(path, ConfigTree::visible);
        m_peerNode.reset(new FilterConfigNode(node));
        m_globalNode =
            m_contextNode = m_peerNode;
        m_hiddenPeerNode =
            m_contextHiddenNode =
            node;
        m_props[false] = m_peerNode;
        m_props[true].reset(new FilterConfigNode(m_hiddenPeerNode));
        break;
    case HTTP_SERVER_LAYOUT: {
        // properties which are normally considered shared are
        // stored in the same nodes as the per-peer properties,
        // except for global ones
        path = "";
        node = m_tree->open(path, ConfigTree::visible);
        m_globalNode.reset(new FilterConfigNode(node));
        path = m_peerPath;      
        node = m_tree->open(path, ConfigTree::visible);
        m_peerNode.reset(new FilterConfigNode(node));
        m_contextNode = m_peerNode;
        m_hiddenPeerNode =
            m_contextHiddenNode =
            m_tree->open(path, ConfigTree::hidden);

        // similar multiplexing as for SHARED_LAYOUT,
        // with two nodes underneath
        boost::shared_ptr<MultiplexConfigNode> mnode;
        mnode.reset(new MultiplexConfigNode(m_peerNode->getName(),
                                            getRegistry(),
                                            false));
        m_props[false] = mnode;
        mnode->setNode(false, ConfigProperty::GLOBAL_SHARING,
                       m_globalNode);
        mnode->setNode(false, ConfigProperty::SOURCE_SET_SHARING,
                       m_peerNode);
        mnode->setNode(false, ConfigProperty::NO_SHARING,
                       m_peerNode);

        // no multiplexing necessary for hidden nodes
        m_props[true].reset(new FilterConfigNode(m_hiddenPeerNode));
        break;
    }
    case SHARED_LAYOUT:
        // really use different nodes for everything
        path = "";
        node = m_tree->open(path, ConfigTree::visible);
        m_globalNode.reset(new FilterConfigNode(node));

        path = m_peerPath;
        if (path.empty()) {
            node.reset(new DevNullConfigNode(m_contextPath + " without peer config"));
        } else {
            node = m_tree->open(path, ConfigTree::visible);
        }
        m_peerNode.reset(new FilterConfigNode(node));
        if (path.empty()) {
            m_hiddenPeerNode = m_peerNode;
        } else {
            m_hiddenPeerNode = m_tree->open(path, ConfigTree::hidden);
        }

        path = m_contextPath;
        node = m_tree->open(path, ConfigTree::visible);
        m_contextNode.reset(new FilterConfigNode(node));
        m_contextHiddenNode = m_tree->open(path, ConfigTree::hidden);

        // Instantiate multiplexer with the most specific node name in
        // the set, the peer node's name. This is slightly inaccurate:
        // error messages generated for this node in will reference
        // the wrong config.ini file for shared properties. But
        // there no shared properties which can trigger such an error
        // at the moment, so this is good enough for now (MB#8037).
        boost::shared_ptr<MultiplexConfigNode> mnode;
        mnode.reset(new MultiplexConfigNode(m_peerNode->getName(),
                                            getRegistry(),
                                            false));
        mnode->setHavePeerNodes(!m_peerPath.empty());
        m_props[false] = mnode;
        mnode->setNode(false, ConfigProperty::GLOBAL_SHARING,
                       m_globalNode);
        mnode->setNode(false, ConfigProperty::SOURCE_SET_SHARING,
                       m_contextNode);
        mnode->setNode(false, ConfigProperty::NO_SHARING,
                       m_peerNode);

        mnode.reset(new MultiplexConfigNode(m_hiddenPeerNode->getName(),
                                            getRegistry(),
                                            true));
        mnode->setHavePeerNodes(!m_peerPath.empty());
        m_props[true] = mnode;
        mnode->setNode(true, ConfigProperty::SOURCE_SET_SHARING,
                       m_contextHiddenNode);
        mnode->setNode(true, ConfigProperty::NO_SHARING,
                       m_hiddenPeerNode);
        break;
    }
}

string SyncConfig::getRootPath() const
{
    return m_tree->getRootPath();
}

void SyncConfig::addPeers(const string &root,
                          const std::string &configname,
                          SyncConfig::ConfigList &res) {
    FileConfigTree tree(root, "", SyncConfig::HTTP_SERVER_LAYOUT);
    list<string> servers = tree.getChildren("");
    BOOST_FOREACH(const string &server, servers) {
        // sanity check: only list server directories which actually
        // contain a configuration. To distinguish between a context
        // (~/.config/syncevolution/default) and an HTTP server config
        // (~/.config/syncevolution/scheduleworld), we check for the
        // "peer" subdirectory that is only in the former.
        //
        // Contexts which don't have a peer are therefore incorrectly
        // listed as a peer. Short of adding a special hidden file
        // this can't be fixed. This is probably overkill and thus not
        // done yet.
        string peerPath = server + "/peers";
        if (!access((root + "/" + peerPath).c_str(), F_OK)) {
            // not a real HTTP server, search for peers
            BOOST_FOREACH(const string &peer, tree.getChildren(peerPath)) {
                res.push_back(pair<string, string> (normalizeConfigString(peer + "@" + server),
                                                  root + "/" + peerPath + "/" + peer));
            }
        } else if (!access((root + "/" + server + "/" + configname).c_str(), F_OK)) {
            res.push_back(pair<string, string> (server, root + "/" + server));
        }
    }
}

SyncConfig::ConfigList SyncConfig::getConfigs()
{
    ConfigList res;

    addPeers(getOldRoot(), "config.txt", res);
    addPeers(getNewRoot(), "config.ini", res);

    // sort the list; better than returning it in random order
    res.sort();

    return res;
}

/* Get a list of all templates, both for any phones listed in @peers*/
SyncConfig::TemplateList SyncConfig::getPeerTemplates(const DeviceList &peers)
{
    TemplateList result1, result2;
    result1 = matchPeerTemplates (peers);
    result2 = getBuiltInTemplates();

    result1.insert (result1.end(), result2.begin(), result2.end());
    return result1;
}


SyncConfig::TemplateList SyncConfig::getBuiltInTemplates()
{
    class TmpList : public TemplateList {
        public:
            void addDefaultTemplate(const string &server, const string &url) {
                BOOST_FOREACH(const boost::shared_ptr<TemplateDescription> entry, static_cast<TemplateList &>(*this)) {
                    if (boost::iequals(entry->m_templateId, server)) {
                        //already present 
                        return;
                    }
                }
                push_back (boost::shared_ptr<TemplateDescription> (new TemplateDescription(server, url)));
            }
    } result;

    // builtin templates if not present
    result.addDefaultTemplate("Funambol", "http://my.funambol.com");
    result.addDefaultTemplate("ScheduleWorld", "http://www.scheduleworld.com");
    result.addDefaultTemplate("Synthesis", "http://www.synthesis.ch");
    result.addDefaultTemplate("Memotoo", "http://www.memotoo.com");
    result.addDefaultTemplate("Google", "http://m.google.com/sync");
    result.addDefaultTemplate("Mobical", "http://www.mobical.net");
    result.addDefaultTemplate("Oracle", "http://www.oracle.com/technology/products/beehive/index.html");
    result.addDefaultTemplate("Goosync", "http://www.goosync.com/");
    result.addDefaultTemplate("SyncEvolution", "http://www.syncevolution.org");
    result.addDefaultTemplate("Ovi", "http://www.ovi.com");

    result.sort (TemplateDescription::compare_op);
    return result;
}

static string SyncEvolutionTemplateDir()
{
    string templateDir(TEMPLATE_DIR);
    const char *envvar = getenv("SYNCEVOLUTION_TEMPLATE_DIR");
    if (envvar) {
        templateDir = envvar;
    }
    return templateDir;
}

SyncConfig::TemplateList SyncConfig::matchPeerTemplates(const DeviceList &peers, bool fuzzyMatch)
{
    TemplateList result;
    // match against all possible templates without any assumption on directory
    // layout, the match is entirely based on the metadata template.ini
    string templateDir(SyncEvolutionTemplateDir());
    std::queue <std::string, std::list<std::string> > directories;

    directories.push(templateDir);
    templateDir = SubstEnvironment("${XDG_CONFIG_HOME}/syncevolution-templates");
    directories.push(templateDir);
    while (!directories.empty()) {
        string sDir = directories.front();
        directories.pop();
        if (isDir(sDir)) {
            // check all sub directories
            ReadDir dir(sDir);
            BOOST_FOREACH(const string &entry, dir) {
                directories.push(sDir + "/" + entry);
            }
        } else {
            TemplateConfig templateConf (sDir);
            if (boost::ends_with(sDir, "~") ||
                boost::starts_with(sDir, ".") ||
                !templateConf.isTemplateConfig()) {
                // ignore temporary files and files which do
                // not contain a valid template
                continue;
            }
            BOOST_FOREACH (const DeviceList::value_type &entry, peers){
                int rank = templateConf.metaMatch (entry.m_fingerprint, entry.m_matchMode);
                if (fuzzyMatch){
                    if (rank > TemplateConfig::NO_MATCH) {
                        result.push_back (boost::shared_ptr<TemplateDescription>(
                                    new TemplateDescription(templateConf.getTemplateId(),
                                                            templateConf.getDescription(),
                                                            rank,
                                                            entry.m_deviceId,
                                                            entry.m_fingerprint,
                                                            sDir,
                                                            templateConf.getFingerprint(),
                                                            templateConf.getTemplateName()
                                                            )
                                    ));
                    }
                } else if (rank == TemplateConfig::BEST_MATCH){
                    result.push_back (boost::shared_ptr<TemplateDescription>(
                                new TemplateDescription(templateConf.getTemplateId(),
                                                        templateConf.getDescription(),
                                                        rank,
                                                        entry.m_deviceId,
                                                        entry.m_fingerprint,
                                                        sDir, 
                                                        templateConf.getFingerprint(),
                                                        templateConf.getTemplateName())
                                ));
                    break;
                }
            }
        }
    }

    result.sort (TemplateDescription::compare_op);
    return result;
}


boost::shared_ptr<SyncConfig> SyncConfig::createPeerTemplate(const string &server)
{
    if (server.empty()) {
        // Empty template name => no such template. This check is
        // necessary because otherwise we end up with SyncConfig(""),
        // which is a configuration where peer-specific properties
        // cannot be set, triggering an errror in config->setDevID().
        return boost::shared_ptr<SyncConfig>();
    }

    // case insensitive search for read-only file template config
    string templateConfig(SyncEvolutionTemplateDir());

    // before starting another fuzzy match process, first try to load the
    // template directly taking the parameter as the path
    bool fromDisk = false;
    if (TemplateConfig::isTemplateConfig(server)) {
        templateConfig = server;
        fromDisk = true;
    } else {
        SyncConfig::DeviceList devices;
        devices.push_back (DeviceDescription("", server, MATCH_ALL));
        templateConfig = "";
        TemplateList templates = matchPeerTemplates (devices, false);
        if (!templates.empty()) {
            templateConfig = templates.front()->m_path;
        }
        if (templateConfig.empty()) {
            // not found, avoid reading current directory by using one which doesn't exist
            templateConfig = "/dev/null";
        } else {
            fromDisk = true;
        }
    }
    
    boost::shared_ptr<ConfigTree> tree(new SingleFileConfigTree(templateConfig));
    boost::shared_ptr<SyncConfig> config(new SyncConfig(server, tree));
    boost::shared_ptr<PersistentSyncSourceConfig> source;

    config->setDefaults(false);
    config->setDevID(string("syncevolution-") + UUID());

    // create sync source configs and set non-default values
    config->setSourceDefaults("addressbook", false);
    config->setSourceDefaults("calendar", false);
    config->setSourceDefaults("todo", false);
    config->setSourceDefaults("memo", false);

    source = config->getSyncSourceConfig("addressbook");
    if (!SourcePropSourceTypeIsSet(source)) {
        source->setSourceType("addressbook");
    }
    if (!SourcePropURIIsSet(source)) {
        source->setURI("card");
    }
    if (!SourcePropSyncIsSet(source)) {
        source->setSync("two-way");
    }

    source = config->getSyncSourceConfig("calendar");
    if (!SourcePropSourceTypeIsSet(source)) {
        source->setSourceType("calendar");
    }
    if (!SourcePropURIIsSet(source)) {
        source->setURI("event");
    }
    if (!SourcePropSyncIsSet(source)) {
        source->setSync("two-way");
    }

    source = config->getSyncSourceConfig("todo");
    if (!SourcePropSourceTypeIsSet(source)) {
        source->setSourceType("todo");
    }
    if (!SourcePropURIIsSet(source)) {
        source->setURI("task");
    }
    if (!SourcePropSyncIsSet(source)) {
        source->setSync("two-way");
    }

    source = config->getSyncSourceConfig("memo");
    if (!SourcePropSourceTypeIsSet(source)) {
        source->setSourceType("memo");
    }
    if (!SourcePropURIIsSet(source)) {
        source->setURI("note");
    }
    if (!SourcePropSyncIsSet(source)) {
        source->setSync("two-way");
    }

    if (fromDisk) {
        // check for icon
        if (config->getIconURI().empty()) {
            string dirname, filename;
            splitPath(templateConfig, dirname, filename);
            ReadDir dir(getDirname(dirname));

            // remove last suffix, regardless what it is
            size_t pos = filename.rfind('.');
            if (pos != filename.npos) {
                filename.resize(pos);
            }
            filename += "-icon";

            BOOST_FOREACH(const string &entry, dir) {
                if (boost::istarts_with(entry, filename)) {
                    config->setIconURI("file://" + dirname + "/" + entry);
                    break;
                }
            }
        }

        // leave the source configs alone and return the config as it is:
        // in order to have sources configured as part of the template,
        // the template must have entries for all sources under "sources"
        return config;
    }

    if (boost::iequals(server, "scheduleworld") ||
        boost::iequals(server, "default")) {
        config->setSyncURL("http://sync.scheduleworld.com/funambol/ds");
        config->setWebURL("http://www.scheduleworld.com");
        // ScheduleWorld was shut down end of November 2010.
        // Completely removing all traces of it from SyncEvolution
        // source code is too intrusive for the time being, so
        // just disable it in the UI.
        // config->setConsumerReady(false);
        source = config->getSyncSourceConfig("addressbook");
        source->setURI("card3");
        source->setSourceType("addressbook:text/vcard");
        source = config->getSyncSourceConfig("calendar");
        source->setURI("cal2");
        source = config->getSyncSourceConfig("todo");
        source->setURI("task2");
        source = config->getSyncSourceConfig("memo");
        source->setURI("note");
    } else if (boost::iequals(server, "funambol")) {
        config->setSyncURL("http://my.funambol.com/sync");
        config->setWebURL("http://my.funambol.com");
        config->setWBXML(false);
        config->setRetryInterval(0);
        config->setConsumerReady(true);
        source = config->getSyncSourceConfig("calendar");
        source->setSync("two-way");
        source->setURI("event");
        source->setSourceType("calendar:text/calendar!");
        source = config->getSyncSourceConfig("todo");
        source->setSync("two-way");
        source->setURI("task");
        source->setSourceType("todo:text/calendar!");
    } else if (boost::iequals(server, "synthesis")) {
        config->setSyncURL("http://www.synthesis.ch/sync");
        config->setWebURL("http://www.synthesis.ch");
        source = config->getSyncSourceConfig("addressbook");
        source->setURI("contacts");
        source = config->getSyncSourceConfig("calendar");
        source->setURI("events");
        source->setSync("disabled");
        source = config->getSyncSourceConfig("todo");
        source->setURI("tasks");
        source->setSync("disabled");
        source = config->getSyncSourceConfig("memo");
        source->setURI("notes");
    } else if (boost::iequals(server, "memotoo")) {
        config->setSyncURL("http://sync.memotoo.com/syncML");
        config->setWebURL("http://www.memotoo.com");
        config->setConsumerReady(true);
        source = config->getSyncSourceConfig("addressbook");
        source->setURI("con");
        source = config->getSyncSourceConfig("calendar");
        source->setURI("cal");
        source = config->getSyncSourceConfig("todo");
        source->setURI("task");
        source = config->getSyncSourceConfig("memo");
        source->setURI("note");
    } else if (boost::iequals(server, "google")) {
        config->setSyncURL("https://m.google.com/syncml");
        config->setWebURL("http://m.google.com/sync");
        config->setClientAuthType("syncml:auth-basic");
        config->setWBXML(true);
        config->setConsumerReady(true);
#ifndef ENABLE_SSL_CERTIFICATE_CHECK
        // temporarily (?) disabled certificate checking because
        // libsoup/gnutls do not accept the Verisign certificate
        // (GNOME Bugzilla #589323)
        config->setSSLVerifyServer(false);
        config->setSSLVerifyHost(false);
#endif
        source = config->getSyncSourceConfig("addressbook");
        source->setURI("contacts");
        source->setSourceType("addressbook:text/x-vcard");
        /* Google support only addressbook sync via syncml */
        source = config->getSyncSourceConfig("calendar");
        source->setSync("none");
        source->setURI("");
        source = config->getSyncSourceConfig("todo");
        source->setSync("none");
        source->setURI("");
        source = config->getSyncSourceConfig("memo");
        source->setSync("none");
        source->setURI("");
    } else if (boost::iequals(server, "mobical")) {
        config->setSyncURL("http://www.mobical.net/sync/server");
        config->setWebURL("http://www.mobical.net");
        config->setConsumerReady(true);
        source = config->getSyncSourceConfig("addressbook");
        source->setURI("con");
        source = config->getSyncSourceConfig("calendar");
        source->setURI("cal");
        source = config->getSyncSourceConfig("todo");
        source->setURI("task");
        source = config->getSyncSourceConfig("memo");
        source->setURI("pnote");
    } else if (boost::iequals(server, "oracle")) {
        config->setSyncURL("https://your.company/mobilesync/server");
        config->setWebURL("http://www.oracle.com/technology/products/beehive/");
        source = config->getSyncSourceConfig("addressbook");
        source->setURI("./contacts");
        source = config->getSyncSourceConfig("calendar");
        source->setURI("./calendar/events");
        source = config->getSyncSourceConfig("todo");
        source->setURI("./calendar/tasks");
        source = config->getSyncSourceConfig("memo");
        source->setURI("./notes");
    } else if (boost::iequals(server, "Ovi")) {
        config->setSyncURL("https://sync.ovi.com/services/syncml");
        config->setWebURL("http://www.ovi.com");
#ifndef ENABLE_SSL_CERTIFICATE_CHECK
        // temporarily (?) disabled certificate checking because
        // libsoup/gnutls do not accept the Verisign certificate
        // (GNOME Bugzilla #589323)
        config->setSSLVerifyServer(false);
        config->setSSLVerifyHost(false);
#endif
        //prefer vcard 3.0
        source = config->getSyncSourceConfig("addressbook");
        source->setSourceType("addressbook:text/vcard");
        source->setURI("./Contact/Unfiled");
        source = config->getSyncSourceConfig("calendar");
        source->setSync("none");
        source->setURI("");
        //prefer vcalendar 1.0
        source->setSourceType("calendar:text/x-vcalendar");
        source = config->getSyncSourceConfig("todo");
        source->setSync("none");
        source->setURI("");
        //prefer vcalendar 1.0
        source->setSourceType("todo:text/x-vcalendar");
        //A virtual datastore combining calendar and todo
        source = config->getSyncSourceConfig("calendar+todo");
        source->setURI("./EventTask/Tasks");
        source->setSourceType("virtual:text/x-vcalendar");
        source->setDatabaseID("calendar,todo");
        source->setSync("two-way");
        //Memo is disabled
        source = config->getSyncSourceConfig("memo");
        source->setSync("none");
        source->setURI("");
    } else if (boost::iequals(server, "goosync")) {
        config->setSyncURL("http://sync2.goosync.com/");
        config->setWebURL("http://www.goosync.com/");
        source = config->getSyncSourceConfig("addressbook");
        source->setURI("contacts");
        source = config->getSyncSourceConfig("calendar");
        source->setURI("calendar");
        source = config->getSyncSourceConfig("todo");
        source->setURI("tasks");
        source = config->getSyncSourceConfig("memo");
        source->setURI("");
    } else if (boost::iequals(server, "syncevolution")) {
        config->setSyncURL("http://yourserver:port");
        config->setWebURL("http://www.syncevolution.org");
        config->setConsumerReady(false);
        source = config->getSyncSourceConfig("addressbook");
        source->setURI("addressbook");
        source = config->getSyncSourceConfig("calendar");
        source->setURI("calendar");
        source = config->getSyncSourceConfig("todo");
        source->setURI("todo");
        source = config->getSyncSourceConfig("memo");
        source->setURI("memo");
    } else {
        config.reset();
    }

    return config;
}

bool SyncConfig::exists() const
{
    return m_peerPath.empty() ?
        m_contextNode->exists() :
        m_peerNode->exists();
}

void SyncConfig::preFlush(ConfigUserInterface &ui)
{
    /* Iterator over all sync global and source properties 
     * one by one and check whether they need to save password */

    /* save password in the global config node */
    ConfigPropertyRegistry& registry = getRegistry();
    BOOST_FOREACH(const ConfigProperty *prop, registry) {
        prop->savePassword(ui, m_peer, *getProperties());
    }

    /** grep each source and save their password */
    list<string> configuredSources = getSyncSources();
    BOOST_FOREACH(const string &sourceName, configuredSources) {
        //boost::shared_ptr<SyncSourceConfig> sc = getSyncSourceConfig(sourceName);
        ConfigPropertyRegistry& registry = SyncSourceConfig::getRegistry();
        SyncSourceNodes sourceNodes = getSyncSourceNodes(sourceName);

        BOOST_FOREACH(const ConfigProperty *prop, registry) {
            prop->savePassword(ui, m_peer, *getProperties(),
                               sourceName, sourceNodes.getProperties());
        }
    }
}

void SyncConfig::flush()
{
    m_tree->flush();
}

void SyncConfig::remove()
{
    boost::shared_ptr<ConfigTree> tree = m_tree;

    // stop using the config nodes, they might get removed now
    makeVolatile();

    tree->remove(m_peerPath.empty() ?
                 m_contextPath :
                 m_peerPath);
}

boost::shared_ptr<PersistentSyncSourceConfig> SyncConfig::getSyncSourceConfig(const string &name)
{
    SyncSourceNodes nodes = getSyncSourceNodes(name);
    return boost::shared_ptr<PersistentSyncSourceConfig>(new PersistentSyncSourceConfig(name, nodes));
}

list<string> SyncConfig::getSyncSources() const
{
    // Return *all* sources configured in this context,
    // not just those configured for the peer. This
    // is necessary so that sources created for some other peer
    // show up for the current one, to prevent overwriting
    // existing properties unintentionally.
    // Returned sources are an union of:
    // 1. contextpath/sources
    // 2. peers/[one-peer]/sources
    // 3. sources in source filter
    list<string> sources;
    if (m_layout == SHARED_LAYOUT) {
        // get sources in context
        sources = m_tree->getChildren(m_contextPath + "/sources");
        list<string> peerSources;
        // get sources from peer if it's not empty
        if (!m_peerPath.empty()) {
            peerSources = m_tree->getChildren(m_peerPath + "/sources");
        }
        // union sources in specific peer
        BOOST_FOREACH(const string &peerSource, peerSources) {
            list<string>::iterator it = std::find(sources.begin(), sources.end(), peerSource);
            // not found
            if ( it == sources.end()) {
                sources.push_back(peerSource); 
            }
        }
    } else {
        // get sources from peer
        sources = m_tree->getChildren(m_peerPath +
                                      (m_layout == SYNC4J_LAYOUT ? 
                                       "/spds/sources" :
                                       "/sources"));
    }
    // get sources from filter and union them into returned sources
    BOOST_FOREACH(const SourceFilters_t::value_type &value, m_sourceFilters) {
        list<string>::iterator it = std::find(sources.begin(), sources.end(), value.first);
        if ( it == sources.end()) {
            sources.push_back(value.first); 
        }
    }

    return sources;
}

SyncSourceNodes SyncConfig::getSyncSourceNodes(const string &name,
                                               const string &changeId)
{
    /** shared source properties */
    boost::shared_ptr<FilterConfigNode> sharedNode;
    /** per-peer source properties */
    boost::shared_ptr<FilterConfigNode> peerNode;
    /** per-peer internal properties and meta data */
    boost::shared_ptr<ConfigNode> hiddenPeerNode,
        serverNode,
        trackingNode;
    string cacheDir;

    // store configs lower case even if the UI uses mixed case
    string lower = name;
    boost::to_lower(lower);

    boost::shared_ptr<ConfigNode> node;
    string sharedPath, peerPath;
    switch (m_layout) {
    case SYNC4J_LAYOUT:
        peerPath = m_peerPath + "/spds/sources/" + lower;
        break;
    case HTTP_SERVER_LAYOUT:
        peerPath = m_peerPath + "/sources/" + lower;
        break;
    case SHARED_LAYOUT:
        if (!m_peerPath.empty()) {
            peerPath = m_peerPath + "/sources/" + lower;
        }
        sharedPath = m_contextPath + string("/sources/") + lower;
        break;
    }

    if (peerPath.empty()) {
        node.reset(new DevNullConfigNode(m_contextPath + " without peer configuration"));
        peerNode.reset(new FilterConfigNode(node));
        hiddenPeerNode =
            trackingNode =
            serverNode = node;
    } else {
        // Here we assume that m_tree is a FileConfigTree. Otherwise getRootPath()
        // will not point into a normal file system.
        cacheDir = m_tree->getRootPath() + "/" + peerPath + "/.cache";

        node = m_tree->open(peerPath, ConfigTree::visible);
        peerNode.reset(new FilterConfigNode(node, m_sourceFilter));
        SourceFilters_t::const_iterator filter = m_sourceFilters.find(name);
        if (filter != m_sourceFilters.end()) {
            peerNode =
                boost::shared_ptr<FilterConfigNode>(new FilterConfigNode(boost::shared_ptr<ConfigNode>(peerNode), filter->second));
        }
        hiddenPeerNode = m_tree->open(peerPath, ConfigTree::hidden);
        trackingNode = m_tree->open(peerPath, ConfigTree::other, changeId);
        serverNode = m_tree->open(peerPath, ConfigTree::server, changeId);
    }

    if (sharedPath.empty()) {
        sharedNode = peerNode;
    } else {
        node = m_tree->open(sharedPath, ConfigTree::visible);
        sharedNode.reset(new FilterConfigNode(node, m_sourceFilter));
        SourceFilters_t::const_iterator filter = m_sourceFilters.find(name);
        if (filter != m_sourceFilters.end()) {
            sharedNode =
                boost::shared_ptr<FilterConfigNode>(new FilterConfigNode(boost::shared_ptr<ConfigNode>(sharedNode), filter->second));
        }
    }

    return SyncSourceNodes(!peerPath.empty(), sharedNode, peerNode, hiddenPeerNode, trackingNode, serverNode, cacheDir);
}

ConstSyncSourceNodes SyncConfig::getSyncSourceNodes(const string &name,
                                                    const string &changeId) const
{
    return const_cast<SyncConfig *>(this)->getSyncSourceNodes(name, changeId);
}

SyncSourceNodes SyncConfig::getSyncSourceNodesNoTracking(const string &name)
{
    SyncSourceNodes nodes = getSyncSourceNodes(name);
    boost::shared_ptr<ConfigNode> dummy(new VolatileConfigNode());
    return SyncSourceNodes(nodes.m_havePeerNode,
                           nodes.m_sharedNode,
                           nodes.m_peerNode,
                           nodes.m_hiddenPeerNode,
                           dummy,
                           nodes.m_serverNode,
                           nodes.m_cacheDir);
}

static ConfigProperty syncPropSyncURL("syncURL",
                                      "Identifies how to contact the peer,\n"
                                      "best explained with some examples:\n"
                                      "HTTP(S) SyncML servers:\n"
                                      "  http://my.funambol.com/sync\n"
                                      "  http://sync.scheduleworld.com/funambol/ds\n"
                                      "  https://m.google.com/syncml\n"
                                      "OBEX over Bluetooth uses the MAC address, with\n"
                                      "the channel chosen automatically:\n"
                                      "  obex-bt://00:0A:94:03:F3:7E\n"
                                      "If the automatism fails, the channel can also be specified:\n"
                                      "  obex-bt://00:0A:94:03:F3:7E+16\n"
                                      "For peers contacting us via Bluetooth, the MAC address is\n"
                                      "used to identify it before the sync starts. Multiple\n"
                                      "urls can be specified in one syncURL property:\n"
                                      "  obex-bt://00:0A:94:03:F3:7E obex-bt://00:01:02:03:04:05\n"
                                      "In the future this might be used to contact the peer\n"
                                      "via one of several transports; right now, only the first\n"
                                      "one is tried." // MB #9446
                                      );

static ConfigProperty syncPropDevID("deviceId",
                                    "The SyncML server gets this string and will use it to keep track of\n"
                                    "changes that still need to be synchronized with this particular\n"
                                    "client; it must be set to something unique (like the pseudo-random\n"
                                    "string created automatically for new configurations) among all clients\n"
                                    "accessing the same server.\n"
                                    "myFUNAMBOL also requires that the string starts with sc-pim-");
static ConfigProperty syncPropUsername("username",
                                       "user name used for authorization with the SyncML server",
                                       "");
static PasswordConfigProperty syncPropPassword("password",
                                               "password used for authorization with the SyncML server;\n"
                                               "in addition to specifying it directly as plain text, it can\n"
                                               "also be read from the standard input or from an environment\n"
                                               "variable of your choice:\n"
                                               "  plain text: password = <insert your password here>\n"
                                               "         ask: password = -\n"
                                               "env variable: password = ${<name of environment variable>}\n",
                                               "",
                                               "SyncML server");
static BoolConfigProperty syncPropPreventSlowSync("preventSlowSync",
                                                  "During a slow sync, the SyncML server must match all items\n"
                                                  "of the client with its own items and detect which ones it\n"
                                                  "already has based on properties of the items. This is slow\n"
                                                  "(client must send all its data) and can lead to duplicates\n"
                                                  "(when the server fails to match correctly).\n"
                                                  "It is therefore sometimes desirable to wipe out data on one\n"
                                                  "side with a refresh-from-client/server sync instead of doing\n"
                                                  "a slow sync.\n"
                                                  "When this option is enabled, slow syncs that could cause problems\n"
                                                  "are not allowed to proceed. Instead, the affected sources are\n"
                                                  "skipped, allowing the user to choose a suitable sync mode in\n"
                                                  "the next run (slow sync selected explicitly, refresh sync).\n"
                                                  "The following situations are handled:\n"
                                                  "- running as client with no local data => unproblematic,\n"
                                                  "  slow sync is allowed to proceed automatically\n"
                                                  "- running as client with local data => client has no\n"
                                                  "  information about server, so slow sync might be problematic\n"
                                                  "  and is prevented\n"
                                                  "- client has data, server asks for slow sync because all its data\n"
                                                  "  was deleted (done by Memotoo and Mobical, because they treat\n"
                                                  "  this as 'user wants to start from scratch') => the sync would\n"
                                                  "  recreate all the client's data, even if the user really wanted\n"
                                                  "  to have it deleted, therefore slow sync is prevented\n"
                                                  "Slow syncs are not yet detected when running as server.\n",
                                                  "1");
static BoolConfigProperty syncPropUseProxy("useProxy",
                                           "set to T to choose an HTTP proxy explicitly; otherwise the default\n"
                                           "proxy settings of the underlying HTTP transport mechanism are used;\n"
                                           "only relevant when contacting the peer via HTTP");
static ConfigProperty syncPropProxyHost("proxyHost",
                                        "proxy URL (http://<host>:<port>)");
static ConfigProperty syncPropProxyUsername("proxyUsername",
                                            "authentication for proxy: username");
static ProxyPasswordConfigProperty syncPropProxyPassword("proxyPassword",
                                                         "proxy password, can be specified in different ways,\n"
                                                         "see SyncML server password for details\n",
                                                         "",
                                                         "proxy");
static StringConfigProperty syncPropClientAuthType("clientAuthType",
                                                   "- empty or \"md5\" for secure method (recommended)\n"
                                                   "- \"basic\" for insecure method\n"
                                                   "\n"
                                                   "This setting is only for debugging purpose and only\n"
                                                   "has an effect during the initial sync of a client.\n"
                                                   "Later it remembers the method that was supported by\n"
                                                   "the server and uses that. When acting as server,\n"
                                                   "clients contacting us can use both basic and md5\n"
                                                   "authentication.\n",
                                                   "md5",
                                                   "",
                                                   Values() +
                                                   (Aliases("basic") + "syncml:auth-basic") +
                                                   (Aliases("md5") + "syncml:auth-md5" + ""));
static ULongConfigProperty syncPropMaxMsgSize("maxMsgSize",
                                              "The maximum size of each message can be set (maxMsgSize) and the\n"
                                              "peer can be told to never sent items larger than a certain\n"
                                              "threshold (maxObjSize). Presumably the peer has to truncate or\n"
                                              "skip larger items. Sizes are specified as number of bytes.",
                                              "150000");
static UIntConfigProperty syncPropMaxObjSize("maxObjSize", "", "4000000");

static BoolConfigProperty syncPropCompression("enableCompression", "enable compression of network traffic (not currently supported)");
static BoolConfigProperty syncPropWBXML("enableWBXML",
                                        "use the more compact binary XML (WBXML) for messages between client and server;\n"
                                        "not applicable when the peer is a SyncML client, because then the client\n"
                                        "chooses the encoding",
                                        "TRUE");
static ConfigProperty syncPropLogDir("logdir",
                                     "full path to directory where automatic backups and logs\n"
                                     "are stored for all synchronizations; if unset, then\n"
                                     "\"${XDG_CACHE_HOME}/syncevolution/<server>\" (which\n"
                                     "usually expands to ${HOME}/.cache/...) will be used;\n"
                                     "if \"none\", then no backups of the databases are made and any\n"
                                     "output is printed directly to the screen");
static UIntConfigProperty syncPropMaxLogDirs("maxlogdirs",
                                            "Unless this option is set, SyncEvolution will never delete\n"
                                            "anything in the \"logdir\". If set, the oldest directories and\n"
                                            "all their content will be removed after a successful sync\n"
                                            "to prevent the number of log directories from growing beyond\n"
                                            "the given limit.",
                                            "10");
static UIntConfigProperty syncPropLogLevel("loglevel",
                                          "level of detail for log messages:\n"
                                          "- 0 (or unset) = INFO messages without log file, DEBUG with log file\n"
                                          "- 1 = only ERROR messages\n"
                                          "- 2 = also INFO messages\n"
                                          "- 3 = also DEBUG messages\n"
                                          "> 3 = increasing amounts of debug messages for developers");
static BoolConfigProperty syncPropPrintChanges("printChanges",
                                               "enables or disables the detailed (and sometimes slow) comparison\n"
                                               "of database content before and after a sync session",
                                               "1");
static SecondsConfigProperty syncPropRetryDuration("RetryDuration",
                                          "The total amount of time in seconds in which the client\n"
                                          "tries to get a response from the server.\n"
                                          "During this time, the client will resend messages\n"
                                          "in regular intervals (RetryInterval) if no response\n"
                                          "is received or the message could not be delivered due\n"
                                          "to transport problems. When this time is exceeded\n"
                                          "without a response, the synchronization aborts without\n"
                                          "sending further messages to the server.\n"
                                          "\n"
                                          "When acting as server, this setting controls how long\n"
                                          "a client is allowed to not send a message before the\n"
                                          "synchronization is aborted."
                                          ,"5M");
static SecondsConfigProperty syncPropRetryInterval("RetryInterval",
                                          "The number of seconds between the start of message sending\n"
                                          "and the start of the retransmission. If the interval has\n"
                                          "already passed when a message send returns, the\n"
                                          "message is resent immediately. Resending without\n"
                                          "any delay will never succeed and therefore specifying 0\n"
                                          "disables retries.\n"
                                          "\n"
                                          "Servers cannot resend messages, so this setting has no\n"
                                          "effect in that case."
                                          ,"2M");
static BoolConfigProperty syncPropPeerIsClient("PeerIsClient",
                                          "Indicates whether this configuration is about a\n"
                                          "client peer or server peer.\n",
                                          "0");
static SafeConfigProperty syncPropPeerName("PeerName",
                                           "An arbitrary name for the peer referenced by this config.\n"
                                           "Might be used by a GUI. The command line tool always uses the\n"
                                           "the configuration name.");
static StringConfigProperty syncPropSyncMLVersion("SyncMLVersion",
                                           "On a client, the latest commonly supported SyncML version \n"
                                           "is used when contacting a server. one of '1.0/1.1/1.2' can\n"
                                           "be used to pick a specific version explicitly.\n"
                                           "\n"
                                           "On a server, this option controls what kind of Server Alerted \n"
                                           "Notification is sent to the client to start a synchronization.\n"
                                           "By default, first the format from 1.2 is tried, then in case \n"
                                           "of failure, the older one from 1.1. 1.2/1.1 can be choosen \n"
                                           "explictely which disables the automatism\n",
                                           "",
                                           "",
                                           Values() +
                                           Aliases("") + Aliases("1.0") + Aliases ("1.1") + Aliases ("1.2")
                                           );

static ConfigProperty syncPropRemoteIdentifier("remoteIdentifier",
                                      "the identifier sent to the remote peer for a server initiated sync.\n"
                                      "if not set, deviceId will be used instead\n",
                                      "");
static ConfigProperty syncPropSSLServerCertificates("SSLServerCertificates",
                                                    "A string specifying the location of the certificates\n"
                                                    "used to authenticate the server. When empty, the\n"
                                                    "system's default location will be searched.\n"
                                                    "\n"
                                                    "SSL support when acting as HTTP server is implemented\n"
                                                    "by the HTTP server frontend, not with these properties.",
                                                    SYNCEVOLUTION_SSL_SERVER_CERTIFICATES);
static BoolConfigProperty syncPropSSLVerifyServer("SSLVerifyServer",
                                                  "The client refuses to establish the connection unless\n"
                                                  "the server presents a valid certificate. Disabling this\n"
                                                  "option considerably reduces the security of SSL\n"
                                                  "(man-in-the-middle attacks become possible) and is not\n"
                                                  "recommended.\n",
                                                  "1");
static BoolConfigProperty syncPropSSLVerifyHost("SSLVerifyHost",
                                                "The client refuses to establish the connection unless the\n"
                                                "server's certificate matches its host name. In cases where\n"
                                                "the certificate still seems to be valid it might make sense\n"
                                                "to disable this option and allow such connections.\n",
                                                "1");

static ConfigProperty syncPropWebURL("WebURL",
                                     "The URL of a web page with further information about the server.\n"
                                     "Used only by the GUI."
                                     "");

static ConfigProperty syncPropIconURI("IconURI",
                                      "The URI of an icon representing the server graphically.\n"
                                      "Should be a 48x48 pixmap or a SVG (preferred).\n"
                                      "Used only by the GUI.");

static BoolConfigProperty syncPropConsumerReady("ConsumerReady",
                                                "Set to true in a configuration template to indicate\n"
                                                "that the server works well enough and is available\n"
                                                "for normal users. Used by the GUI to limit the choice\n"
                                                "of configurations offered to users.\n"
                                                "Has no effect in a user's server configuration.\n",
                                                "0");

static ULongConfigProperty syncPropHashCode("HashCode", "used by the SyncML library internally; do not modify");

static ConfigProperty syncPropConfigDate("ConfigDate", "used by the SyncML library internally; do not modify");

static SafeConfigProperty syncPropRemoteDevID("remoteDeviceId",
                                              "SyncML ID of our peer, empty if unknown; must be set only when\n"
                                              "the peer is a SyncML client contacting us via HTTP.\n"
                                              "Clients contacting us via OBEX/Bluetooth can be identified\n"
                                              "either via this remoteDeviceId property or by their MAC\n"
                                              "address, if that was set in the syncURL property.\n"
                                              "\n"
                                              "If this property is empty and the peer synchronizes with\n"
                                              "this configuration chosen by some other means, then its ID\n"
                                              "is recorded here automatically and later used to verify that\n"
                                              "the configuration is not accidentally used by a different\n"
                                              "peer.");

static SafeConfigProperty syncPropNonce("lastNonce",
                                        "MD5 nonce of our peer, empty if not set yet; do not edit, used internally");

// used both as source and sync property, internal in both cases
static SafeConfigProperty syncPropDeviceData("deviceData",
                                             "information about the peer in the format described in the\n"
                                             "Synthesis SDK manual under 'Session_SaveDeviceInfo'");

static SafeConfigProperty syncPropDefaultPeer("defaultPeer",
                                              "the peer which is used by default in some frontends, like the sync-UI");

static StringConfigProperty syncPropAutoSync("autoSync",
                                             "Controls automatic synchronization. Currently,\n"
                                             "automatic synchronization is done by running\n"
                                             "a synchronization at regular intervals. This\n"
                                             "may drain the battery, in particular when\n"
                                             "using Bluetooth!\n"
                                             "Because a peer might be reachable via different\n"
                                             "transports at some point, this option provides\n"
                                             "detailed control over which transports may\n"
                                             "be used for automatic synchronization:\n"
                                             "0 - don't do auto sync\n"
                                             "1 - do automatic sync, using whatever transport\n"
                                             "    is available\n"
                                             "http - only via HTTP transport\n"
                                             "obex-bt - only via Bluetooth transport\n"
                                             "http,obex-bt - pick one of these\n",
                                             "0");

static SecondsConfigProperty syncPropAutoSyncInterval("autoSyncInterval",
                                                      "This is the minimum number of seconds between two\n"
                                                      "synchronizations that has to pass before starting\n"
                                                      "an automatic synchronization. Can be specified using\n"
                                                      "a 1h30m5s format.\n"
                                                      "\n"
                                                      "Before reducing this interval, consider that it will\n"
                                                      "increase resource consumption on the local and remote\n"
                                                      "side. Some SyncML server operators only allow a\n"
                                                      "certain number of sessions per day.\n"
                                                      "The value 0 has the effect of only running automatic\n"
                                                      "synchronization when changes are detected (not\n"
                                                      "implemented yet, therefore it basically disables\n"
                                                      "automatic synchronization).\n",
                                                      "30M");

static SecondsConfigProperty syncPropAutoSyncDelay("autoSyncDelay",
                                                   "An automatic sync will not be started unless the peer\n"
                                                   "has been available for this duration, specified in seconds\n"
                                                   "or 1h30m5s format.\n"
                                                   "\n"
                                                   "This prevents running a sync when network connectivity\n"
                                                   "is unreliable or was recently established for some\n"
                                                   "other purpose. It is also a heuristic that attempts\n"
                                                   "to predict how long connectivity be available in the\n"
                                                   "future, because it should better be available long\n"
                                                   "enough to complete the synchronization.\n",
                                                   "5M");

ConfigPropertyRegistry &SyncConfig::getRegistry()
{
    static ConfigPropertyRegistry registry;
    static bool initialized;

    if (!initialized) {
        registry.push_back(&syncPropSyncURL);
        registry.push_back(&syncPropUsername);
        registry.push_back(&syncPropPassword);
        registry.push_back(&syncPropLogDir);
        registry.push_back(&syncPropLogLevel);
        registry.push_back(&syncPropPrintChanges);
        registry.push_back(&syncPropMaxLogDirs);
        registry.push_back(&syncPropAutoSync);
        registry.push_back(&syncPropAutoSyncInterval);
        registry.push_back(&syncPropAutoSyncDelay);
        registry.push_back(&syncPropPreventSlowSync);
        registry.push_back(&syncPropUseProxy);
        registry.push_back(&syncPropProxyHost);
        registry.push_back(&syncPropProxyUsername);
        registry.push_back(&syncPropProxyPassword);
        registry.push_back(&syncPropClientAuthType);
        registry.push_back(&syncPropRetryDuration);
        registry.push_back(&syncPropRetryInterval);
        registry.push_back(&syncPropRemoteIdentifier);
        registry.push_back(&syncPropPeerIsClient);
        registry.push_back(&syncPropSyncMLVersion);
        registry.push_back(&syncPropPeerName);
        registry.push_back(&syncPropDevID);
        registry.push_back(&syncPropRemoteDevID);
        registry.push_back(&syncPropWBXML);
        registry.push_back(&syncPropMaxMsgSize);
        registry.push_back(&syncPropMaxObjSize);
        registry.push_back(&syncPropCompression);
        registry.push_back(&syncPropSSLServerCertificates);
        registry.push_back(&syncPropSSLVerifyServer);
        registry.push_back(&syncPropSSLVerifyHost);
        registry.push_back(&syncPropWebURL);
        registry.push_back(&syncPropIconURI);
        registry.push_back(&syncPropConsumerReady);
        registry.push_back(&syncPropHashCode);
        registry.push_back(&syncPropConfigDate);
        registry.push_back(&syncPropNonce);
        registry.push_back(&syncPropDeviceData);
        registry.push_back(&syncPropDefaultPeer);

        // obligatory sync properties
        syncPropUsername.setObligatory(true);
        syncPropPassword.setObligatory(true);
        syncPropDevID.setObligatory(true);
        syncPropSyncURL.setObligatory(true);

        // hidden sync properties
        syncPropHashCode.setHidden(true);
        syncPropConfigDate.setHidden(true);
        syncPropNonce.setHidden(true);
        syncPropDeviceData.setHidden(true);

        // global sync properties
        syncPropDefaultPeer.setSharing(ConfigProperty::GLOBAL_SHARING);

        // peer independent sync properties
        syncPropLogDir.setSharing(ConfigProperty::SOURCE_SET_SHARING);
        syncPropMaxLogDirs.setSharing(ConfigProperty::SOURCE_SET_SHARING);
        syncPropDevID.setSharing(ConfigProperty::SOURCE_SET_SHARING);

        initialized = true;
    }

    return registry;
}

const char *SyncConfig::getUsername() const { return m_stringCache.getProperty(*getNode(syncPropUsername), syncPropUsername); }
void SyncConfig::setUsername(const string &value, bool temporarily) { syncPropUsername.setProperty(*getNode(syncPropUsername), value, temporarily); }
const char *SyncConfig::getPassword() const {
    string password = syncPropPassword.getCachedProperty(*getNode(syncPropPassword), m_cachedPassword);
    return m_stringCache.storeString(syncPropPassword.getName(), password);
}
void SyncConfig::checkPassword(ConfigUserInterface &ui) {
    syncPropPassword.checkPassword(ui, m_peer, *getProperties());
}
void SyncConfig::savePassword(ConfigUserInterface &ui) {
    syncPropPassword.savePassword(ui, m_peer, *getProperties());
}

void PasswordConfigProperty::checkPassword(ConfigUserInterface &ui,
                                           const string &serverName,
                                           FilterConfigNode &globalConfigNode,
                                           const string &sourceName,
                                           const boost::shared_ptr<FilterConfigNode> &sourceConfigNode) const
{
    string password, passwordSave;
    /* if no source config node, then it should only be password in the global config node */
    if(sourceConfigNode.get() == NULL) {
        password = getProperty(globalConfigNode);
    } else {
        password = getProperty(*sourceConfigNode);
    }

    string descr = getDescr(serverName,globalConfigNode,sourceName,sourceConfigNode);
    if (password == "-") {
        ConfigPasswordKey key = getPasswordKey(descr,serverName,globalConfigNode,sourceName,sourceConfigNode);
        passwordSave = ui.askPassword(getName(),descr, key);
    } else if(boost::starts_with(password, "${") &&
              boost::ends_with(password, "}")) {
        string envname = password.substr(2, password.size() - 3);
        const char *envval = getenv(envname.c_str());
        if (!envval) {
            SyncContext::throwError(string("the environment variable '") +
                                            envname +
                                            "' for the '" +
                                            descr +
                                            "' password is not set");
        } else {
            passwordSave = envval;
        }
    }
    /* If password is from ui or environment variable, set them in the config node on fly
     * Previous impl use temp string to store them, this is not good for expansion in the backend */
    if(!passwordSave.empty()) {
        if(sourceConfigNode.get() == NULL) {
            globalConfigNode.addFilter(getName(), passwordSave);
        } else {
            sourceConfigNode->addFilter(getName(), passwordSave);
        }
    }
}
void PasswordConfigProperty::savePassword(ConfigUserInterface &ui,
                                          const string &serverName,
                                          FilterConfigNode &globalConfigNode,
                                          const string &sourceName,
                                          const boost::shared_ptr<FilterConfigNode> &sourceConfigNode) const
{
    /** here we don't invoke askPassword for this function has different logic from it */
    string password;
    if(sourceConfigNode.get() == NULL) {
        password = getProperty(globalConfigNode);
    } else {
        password = getProperty(*sourceConfigNode);
    }
    /** if it has been stored or it has no value, do nothing */
    if(password == "-" || password == "") {
        return;
    } else if(boost::starts_with(password, "${") &&
              boost::ends_with(password, "}")) {
        /** we delay this calculation of environment variable for 
         * it might be changed in the sync time. */
        return;
    }
    string descr = getDescr(serverName,globalConfigNode,sourceName,sourceConfigNode);
    ConfigPasswordKey key = getPasswordKey(descr,serverName,globalConfigNode,sourceName,sourceConfigNode);
    if(ui.savePassword(getName(), password, key)) {
        string value = "-";
        if(sourceConfigNode.get() == NULL) {
            setProperty(globalConfigNode, value);
        } else {
            setProperty(*sourceConfigNode,value);
        }
    }
}

string PasswordConfigProperty::getCachedProperty(const ConfigNode &node,
                                                 const string &cachedPassword)
{
    string password;

    if (!cachedPassword.empty()) {
        password = cachedPassword;
    } else {
        password = getProperty(node);
    }
    return password;
}

/**
 * remove some unnecessary parts of server URL.
 * internal use.
 */
static void purifyServer(string &server)
{
    /** here we use server sync url without protocol prefix and
     * user account name as the key in the keyring */
    size_t start = server.find("://");
    /** we don't reserve protocol prefix for it may change*/
    if(start != server.npos) {
        server = server.substr(start + 3);
    }
}

ConfigPasswordKey PasswordConfigProperty::getPasswordKey(const string &descr,
                                                         const string &serverName,
                                                         FilterConfigNode &globalConfigNode,
                                                         const string &sourceName,
                                                         const boost::shared_ptr<FilterConfigNode> &sourceConfigNode) const 
{
    ConfigPasswordKey key;
    key.server = syncPropSyncURL.getProperty(globalConfigNode);
    purifyServer(key.server);
    key.user   = syncPropUsername.getProperty(globalConfigNode);
    return key;
}
void ProxyPasswordConfigProperty::checkPassword(ConfigUserInterface &ui,
                                           const string &serverName,
                                           FilterConfigNode &globalConfigNode,
                                           const string &sourceName,
                                           const boost::shared_ptr<FilterConfigNode> &sourceConfigNode) const
{
    /* if useProxy is set 'true', then check proxypassword */
    if(syncPropUseProxy.getPropertyValue(globalConfigNode)) {
        PasswordConfigProperty::checkPassword(ui, serverName, globalConfigNode, sourceName, sourceConfigNode);
    }
}

ConfigPasswordKey ProxyPasswordConfigProperty::getPasswordKey(const string &descr,
                                                              const string &serverName,
                                                              FilterConfigNode &globalConfigNode,
                                                              const string &sourceName,
                                                              const boost::shared_ptr<FilterConfigNode> &sourceConfigNode) const
{
    ConfigPasswordKey key;
    key.server = syncPropProxyHost.getProperty(globalConfigNode);
    key.user   = syncPropProxyUsername.getProperty(globalConfigNode);
    return key;
}

void SyncConfig::setPassword(const string &value, bool temporarily) { m_cachedPassword = ""; syncPropPassword.setProperty(*getNode(syncPropPassword), value, temporarily); }

bool SyncConfig::getPreventSlowSync() const { return syncPropPreventSlowSync.getPropertyValue(*getNode(syncPropPreventSlowSync)); }
void SyncConfig::setPreventSlowSync(bool value, bool temporarily) { syncPropPreventSlowSync.setProperty(*getNode(syncPropPreventSlowSync), value, temporarily); }

static const char *ProxyString = "http_proxy";

/* Reads http_proxy from environment, if not available returns configured value */
bool SyncConfig::getUseProxy() const {
    char *proxy = getenv(ProxyString);
    if (!proxy ) {
        return syncPropUseProxy.getPropertyValue(*getNode(syncPropUseProxy));
    } else if (strlen(proxy)>0) {
        return true;
    } else {
        return false;
    }
}

void SyncConfig::setUseProxy(bool value, bool temporarily) { syncPropUseProxy.setProperty(*getNode(syncPropUseProxy), value, temporarily); }

/* If http_proxy set in the environment returns it, otherwise configured value */
const char *SyncConfig::getProxyHost() const {
    char *proxy = getenv(ProxyString);
    if (!proxy) {
        return m_stringCache.getProperty(*getNode(syncPropUseProxy),syncPropProxyHost); 
    } else {
        return m_stringCache.storeString(syncPropProxyHost.getName(), proxy);
    }
}

void SyncConfig::setProxyHost(const string &value, bool temporarily) { syncPropProxyHost.setProperty(*getNode(syncPropProxyHost), value, temporarily); }

const char *SyncConfig::getProxyUsername() const { return m_stringCache.getProperty(*getNode(syncPropProxyUsername), syncPropProxyUsername); }
void SyncConfig::setProxyUsername(const string &value, bool temporarily) { syncPropProxyUsername.setProperty(*getNode(syncPropProxyUsername), value, temporarily); }

const char *SyncConfig::getProxyPassword() const {
    string password = syncPropProxyPassword.getCachedProperty(*getNode(syncPropProxyPassword), m_cachedProxyPassword);
    return m_stringCache.storeString(syncPropProxyPassword.getName(), password);
}
void SyncConfig::checkProxyPassword(ConfigUserInterface &ui) {
    syncPropProxyPassword.checkPassword(ui, m_peer, *getNode(syncPropProxyPassword), "", boost::shared_ptr<FilterConfigNode>());
}
void SyncConfig::saveProxyPassword(ConfigUserInterface &ui) {
    syncPropProxyPassword.savePassword(ui, m_peer, *getNode(syncPropProxyPassword), "", boost::shared_ptr<FilterConfigNode>());
}
void SyncConfig::setProxyPassword(const string &value, bool temporarily) { m_cachedProxyPassword = ""; syncPropProxyPassword.setProperty(*getNode(syncPropProxyPassword), value, temporarily); }
vector<string> SyncConfig::getSyncURL() const { 
    string s = m_stringCache.getProperty(*getNode(syncPropSyncURL), syncPropSyncURL);
    vector<string> urls;
    if (!s.empty()) {
        // workaround for g++ 4.3/4.4:
        // http://stackoverflow.com/questions/1168525/c-gcc4-4-warning-array-subscript-is-above-array-bounds
        static const string sep(" \t");
        boost::split(urls, s, boost::is_any_of(sep));
    }
    return urls;
}
void SyncConfig::setSyncURL(const string &value, bool temporarily) { syncPropSyncURL.setProperty(*getNode(syncPropSyncURL), value, temporarily); }
void SyncConfig::setSyncURL(const vector<string> &value, bool temporarily) { 
    stringstream urls;
    BOOST_FOREACH (string url, value) {
        urls<<url<<" ";
    }
    return setSyncURL (urls.str(), temporarily);
}
const char *SyncConfig::getClientAuthType() const { return m_stringCache.getProperty(*getNode(syncPropClientAuthType), syncPropClientAuthType); }
void SyncConfig::setClientAuthType(const string &value, bool temporarily) { syncPropClientAuthType.setProperty(*getNode(syncPropClientAuthType), value, temporarily); }
unsigned long  SyncConfig::getMaxMsgSize() const { return syncPropMaxMsgSize.getPropertyValue(*getNode(syncPropMaxMsgSize)); }
void SyncConfig::setMaxMsgSize(unsigned long value, bool temporarily) { syncPropMaxMsgSize.setProperty(*getNode(syncPropMaxMsgSize), value, temporarily); }
unsigned int  SyncConfig::getMaxObjSize() const { return syncPropMaxObjSize.getPropertyValue(*getNode(syncPropMaxObjSize)); }
void SyncConfig::setMaxObjSize(unsigned int value, bool temporarily) { syncPropMaxObjSize.setProperty(*getNode(syncPropMaxObjSize), value, temporarily); }
bool SyncConfig::getCompression() const { return syncPropCompression.getPropertyValue(*getNode(syncPropCompression)); }
void SyncConfig::setCompression(bool value, bool temporarily) { syncPropCompression.setProperty(*getNode(syncPropCompression), value, temporarily); }
const char *SyncConfig::getDevID() const { return m_stringCache.getProperty(*getNode(syncPropDevID), syncPropDevID); }
void SyncConfig::setDevID(const string &value, bool temporarily) { syncPropDevID.setProperty(*getNode(syncPropDevID), value, temporarily); }
bool SyncConfig::getWBXML() const { return syncPropWBXML.getPropertyValue(*getNode(syncPropWBXML)); }
void SyncConfig::setWBXML(bool value, bool temporarily) { syncPropWBXML.setProperty(*getNode(syncPropWBXML), value, temporarily); }
const char *SyncConfig::getLogDir() const { return m_stringCache.getProperty(*getNode(syncPropLogDir), syncPropLogDir); }
void SyncConfig::setLogDir(const string &value, bool temporarily) { syncPropLogDir.setProperty(*getNode(syncPropLogDir), value, temporarily); }
int SyncConfig::getMaxLogDirs() const { return syncPropMaxLogDirs.getPropertyValue(*getNode(syncPropMaxLogDirs)); }
void SyncConfig::setMaxLogDirs(int value, bool temporarily) { syncPropMaxLogDirs.setProperty(*getNode(syncPropMaxLogDirs), value, temporarily); }
int SyncConfig::getLogLevel() const { return syncPropLogLevel.getPropertyValue(*getNode(syncPropLogLevel)); }
void SyncConfig::setLogLevel(int value, bool temporarily) { syncPropLogLevel.setProperty(*getNode(syncPropLogLevel), value, temporarily); }
int SyncConfig::getRetryDuration() const {return syncPropRetryDuration.getPropertyValue(*getNode(syncPropRetryDuration));}
void SyncConfig::setRetryDuration(int value, bool temporarily) { syncPropRetryDuration.setProperty(*getNode(syncPropRetryDuration), value, temporarily); }
int SyncConfig::getRetryInterval() const { return syncPropRetryInterval.getPropertyValue(*getNode(syncPropRetryInterval)); }
void SyncConfig::setRetryInterval(int value, bool temporarily) { return syncPropRetryInterval.setProperty(*getNode(syncPropRetryInterval),value,temporarily); }

/* used by Server Alerted Sync */
const char* SyncConfig::getRemoteIdentifier() const {return m_stringCache.getProperty (*getNode(syncPropRemoteIdentifier), syncPropRemoteIdentifier);}
void SyncConfig::setRemoteIdentifier (const string &value, bool temporarily) { return syncPropRemoteIdentifier.setProperty (*getNode(syncPropRemoteIdentifier), value, temporarily); }

bool SyncConfig::getPeerIsClient() const { return syncPropPeerIsClient.getPropertyValue(*getNode(syncPropPeerIsClient)); }
void SyncConfig::setPeerIsClient(bool value, bool temporarily) { syncPropPeerIsClient.setProperty(*getNode(syncPropPeerIsClient), value, temporarily); }

const char* SyncConfig::getSyncMLVersion() const { return m_stringCache.getProperty(*getNode(syncPropSyncMLVersion), syncPropSyncMLVersion); }
void SyncConfig::setSyncMLVersion(const string &value, bool temporarily) { syncPropSyncMLVersion.setProperty(*getNode(syncPropSyncMLVersion), value, temporarily); }

string SyncConfig::getPeerName() const { return syncPropPeerName.getProperty(*getNode(syncPropPeerName)); }
void SyncConfig::setPeerName(const string &name) { syncPropPeerName.setProperty(*getNode(syncPropPeerName), name); }

bool SyncConfig::getPrintChanges() const { return syncPropPrintChanges.getPropertyValue(*getNode(syncPropPrintChanges)); }
void SyncConfig::setPrintChanges(bool value, bool temporarily) { syncPropPrintChanges.setProperty(*getNode(syncPropPrintChanges), value, temporarily); }
std::string SyncConfig::getWebURL() const { return syncPropWebURL.getProperty(*getNode(syncPropWebURL)); }
void SyncConfig::setWebURL(const std::string &url, bool temporarily) { syncPropWebURL.setProperty(*getNode(syncPropWebURL), url, temporarily); }
std::string SyncConfig::getIconURI() const { return syncPropIconURI.getProperty(*getNode(syncPropIconURI)); }
bool SyncConfig::getConsumerReady() const { return syncPropConsumerReady.getPropertyValue(*getNode(syncPropConsumerReady)); }
void SyncConfig::setConsumerReady(bool ready) { return syncPropConsumerReady.setProperty(*getNode(syncPropConsumerReady), ready); }
void SyncConfig::setIconURI(const std::string &uri, bool temporarily) { syncPropIconURI.setProperty(*getNode(syncPropIconURI), uri, temporarily); }
unsigned long SyncConfig::getHashCode() const { return syncPropHashCode.getPropertyValue(*getNode(syncPropHashCode)); }
void SyncConfig::setHashCode(unsigned long code) { syncPropHashCode.setProperty(*getNode(syncPropHashCode), code); }
std::string SyncConfig::getConfigDate() const { return syncPropConfigDate.getProperty(*getNode(syncPropConfigDate)); }
void SyncConfig::setConfigDate() { 
    /* Set current timestamp as configdate */
    char buffer[17]; 
    time_t ts = time(NULL);
    strftime(buffer, sizeof(buffer), "%Y%m%dT%H%M%SZ", gmtime(&ts));
    const std::string date(buffer);
    syncPropConfigDate.setProperty(*getNode(syncPropConfigDate), date);
}

const char* SyncConfig::getSSLServerCertificates() const { return m_stringCache.getProperty(*getNode(syncPropSSLServerCertificates), syncPropSSLServerCertificates); }
void SyncConfig::setSSLServerCertificates(const string &value, bool temporarily) { syncPropSSLServerCertificates.setProperty(*getNode(syncPropSSLServerCertificates), value, temporarily); }
bool SyncConfig::getSSLVerifyServer() const { return syncPropSSLVerifyServer.getPropertyValue(*getNode(syncPropSSLVerifyServer)); }
void SyncConfig::setSSLVerifyServer(bool value, bool temporarily) { syncPropSSLVerifyServer.setProperty(*getNode(syncPropSSLVerifyServer), value, temporarily); }
bool SyncConfig::getSSLVerifyHost() const { return syncPropSSLVerifyHost.getPropertyValue(*getNode(syncPropSSLVerifyHost)); }
void SyncConfig::setSSLVerifyHost(bool value, bool temporarily) { syncPropSSLVerifyHost.setProperty(*getNode(syncPropSSLVerifyHost), value, temporarily); }
string SyncConfig::getRemoteDevID() const { return syncPropRemoteDevID.getProperty(*getNode(syncPropRemoteDevID)); }
void SyncConfig::setRemoteDevID(const string &value) { syncPropRemoteDevID.setProperty(*getNode(syncPropRemoteDevID), value); }
string SyncConfig::getNonce() const { return syncPropNonce.getProperty(*getNode(syncPropNonce)); }
void SyncConfig::setNonce(const string &value) { syncPropNonce.setProperty(*getNode(syncPropNonce), value); }
string SyncConfig::getDeviceData() const { return syncPropDeviceData.getProperty(*getNode(syncPropDeviceData)); }
void SyncConfig::setDeviceData(const string &value) { syncPropDeviceData.setProperty(*getNode(syncPropDeviceData), value); }
string SyncConfig::getDefaultPeer() const { return syncPropDefaultPeer.getProperty(*getNode(syncPropDefaultPeer)); }
void SyncConfig::setDefaultPeer(const string &value) { syncPropDefaultPeer.setProperty(*getNode(syncPropDefaultPeer), value); }

string SyncConfig::getAutoSync() const { return syncPropAutoSync.getProperty(*getNode(syncPropAutoSync)); }
void SyncConfig::setAutoSync(const string &value, bool temporarily) { syncPropAutoSync.setProperty(*getNode(syncPropAutoSync), value, temporarily); }
unsigned int SyncConfig::getAutoSyncInterval() const { return syncPropAutoSyncInterval.getPropertyValue(*getNode(syncPropAutoSyncInterval)); }
void SyncConfig::setAutoSyncInterval(unsigned int value, bool temporarily) { syncPropAutoSyncInterval.setProperty(*getNode(syncPropAutoSyncInterval), value, temporarily); }
unsigned int SyncConfig::getAutoSyncDelay() const { return syncPropAutoSyncDelay.getPropertyValue(*getNode(syncPropAutoSyncDelay)); }
void SyncConfig::setAutoSyncDelay(unsigned int value, bool temporarily) { syncPropAutoSyncDelay.setProperty(*getNode(syncPropAutoSyncDelay), value, temporarily); }

std::string SyncConfig::findSSLServerCertificate()
{
    std::string paths = getSSLServerCertificates();
    std::vector< std::string > files;
    boost::split(files, paths, boost::is_any_of(":"));
    BOOST_FOREACH(std::string file, files) {
        if (!file.empty() && !access(file.c_str(), R_OK)) {
            return file;
        }
    }

    return "";
}

void SyncConfig::setConfigFilter(bool sync,
                                 const std::string &source,
                                 const FilterConfigNode::ConfigFilter &filter)
{
    if (sync) {
        m_peerNode->setFilter(filter);
        if (m_peerNode != m_contextNode) {
            m_contextNode->setFilter(filter);
        }
        if (m_globalNode != m_contextNode) {
            m_globalNode->setFilter(filter);
        }
    } else if (source.empty()) {
        m_sourceFilter = filter;
    } else {
        m_sourceFilters[source] = filter;
    }
}

boost::shared_ptr<FilterConfigNode>
SyncConfig::getNode(const ConfigProperty &prop)
{
    switch (prop.getSharing()) {
    case ConfigProperty::GLOBAL_SHARING:
        if (prop.isHidden()) {
            boost::shared_ptr<FilterConfigNode>(new FilterConfigNode(boost::shared_ptr<ConfigNode>(new DevNullConfigNode("no hidden global properties"))));
        } else {
            return m_globalNode;
        }
        break;
    case ConfigProperty::SOURCE_SET_SHARING:
        if (prop.isHidden()) {
            return boost::shared_ptr<FilterConfigNode>(new FilterConfigNode(m_contextHiddenNode));
        } else {
            return m_contextNode;
        }
        break;
    case ConfigProperty::NO_SHARING:
        if (prop.isHidden()) {
            return boost::shared_ptr<FilterConfigNode>(new FilterConfigNode(m_hiddenPeerNode));
        } else {
            return m_peerNode;
        }
        break;
    }
    // should not be reached
    return boost::shared_ptr<FilterConfigNode>(new FilterConfigNode(boost::shared_ptr<ConfigNode>(new DevNullConfigNode("unknown sharing state of property"))));
}

static void setDefaultProps(const ConfigPropertyRegistry &registry,
                            boost::shared_ptr<FilterConfigNode> node,
                            bool force,
                            bool unshared,
                            bool useObligatory = true)
{
    BOOST_FOREACH(const ConfigProperty *prop, registry) {
        bool isDefault;
        prop->getProperty(*node, &isDefault);
        
        if (!prop->isHidden() &&
            (unshared || prop->getSharing() != ConfigProperty::NO_SHARING) &&
            (force || isDefault)) {
            if (useObligatory) {
                prop->setDefaultProperty(*node, prop->isObligatory());
            } else {
                prop->setDefaultProperty(*node, false);
            }
        }
    }
}

void SyncConfig::setDefaults(bool force)
{
    setDefaultProps(getRegistry(), getProperties(),
                    force,
                    !m_peerPath.empty());
}

void SyncConfig::setSourceDefaults(const string &name, bool force)
{
    SyncSourceNodes nodes = getSyncSourceNodes(name);
    setDefaultProps(SyncSourceConfig::getRegistry(),
                    nodes.getProperties(),
                    force,
                    !m_peerPath.empty());
}

void SyncConfig::removeSyncSource(const string &name)
{
    string lower = name;
    boost::to_lower(lower);
    string pathName;

    if (m_layout == SHARED_LAYOUT) {
        if (m_peerPath.empty()) {
            // removed shared source properties...
            pathName = m_contextPath + "/sources/" + lower;
            m_tree->remove(pathName);
            // ... and the peer-specific ones of *all* peers
            BOOST_FOREACH(const std::string peer,
                          m_tree->getChildren(m_contextPath + "/peers")) {
                m_tree->remove(m_contextPath + "/peers/" + peer + "/sources/" + lower);
            }
        } else {
            // remove only inside the selected peer
            m_tree->remove(m_peerPath + "/sources/" + lower);
        }
    } else {
        // remove the peer-specific ones
        pathName = m_peerPath +
            (m_layout == SYNC4J_LAYOUT ? "spds/sources/" : "sources/") +
            lower;
        m_tree->remove(pathName);
    }
}

void SyncConfig::clearSyncSourceProperties(const string &name)
{
    SyncSourceNodes nodes = getSyncSourceNodes(name);
    setDefaultProps(SyncSourceConfig::getRegistry(),
                    nodes.getProperties(),
                    true,
                    !m_peerPath.empty(),
                    false);
}

void SyncConfig::clearSyncProperties()
{
    setDefaultProps(getRegistry(), getProperties(),
                    true,
                    !m_peerPath.empty(),
                    false);
}

static void copyProperties(const ConfigNode &fromProps,
                           ConfigNode &toProps,
                           bool hidden,
                           bool unshared,
                           const ConfigPropertyRegistry &allProps)
{
    BOOST_FOREACH(const ConfigProperty *prop, allProps) {
        if (prop->isHidden() == hidden &&
            (unshared ||
             prop->getSharing() != ConfigProperty::NO_SHARING ||
             (prop->getFlags() & ConfigProperty::SHARED_AND_UNSHARED))) {
            string name = prop->getName();
            bool isDefault;
            string value = prop->getProperty(fromProps, &isDefault);
            toProps.setProperty(name, value, prop->getComment(),
                                isDefault ? &value : NULL);
        }
    }
}

static void copyProperties(const ConfigNode &fromProps,
                           ConfigNode &toProps)
{
    ConfigProps props;
    fromProps.readProperties(props);
    toProps.writeProperties(props);
}

void SyncConfig::copy(const SyncConfig &other,
                      const set<string> *sourceSet)
{
    for (int i = 0; i < 2; i++ ) {
        boost::shared_ptr<const FilterConfigNode> fromSyncProps(other.getProperties(i));
        boost::shared_ptr<FilterConfigNode> toSyncProps(this->getProperties(i));
        copyProperties(*fromSyncProps,
                       *toSyncProps,
                       i,
                       !m_peerPath.empty(),
                       SyncConfig::getRegistry());
    }

    list<string> sources;
    if (!sourceSet) {
        sources = other.getSyncSources();
    } else {
        BOOST_FOREACH(const string &sourceName, *sourceSet) {
            sources.push_back(sourceName);
        }
    }
    BOOST_FOREACH(const string &sourceName, sources) {
        ConstSyncSourceNodes fromNodes = other.getSyncSourceNodes(sourceName);
        SyncSourceNodes toNodes = this->getSyncSourceNodes(sourceName);

        for (int i = 0; i < 2; i++ ) {
            copyProperties(*fromNodes.getProperties(i),
                           *toNodes.getProperties(i),
                           i,
                           !m_peerPath.empty(),
                           SyncSourceConfig::getRegistry());
        }
        copyProperties(*fromNodes.getTrackingNode(),
                       *toNodes.getTrackingNode());
        copyProperties(*fromNodes.getServerNode(),
                       *toNodes.getServerNode());
    }
}

const char *SyncConfig::getSwv() const { return VERSION; }
const char *SyncConfig::getDevType() const { return DEVICE_TYPE; }

                     
SyncSourceConfig::SyncSourceConfig(const string &name, const SyncSourceNodes &nodes) :
    m_name(name),
    m_nodes(nodes)
{
}

StringConfigProperty SyncSourceConfig::m_sourcePropSync("sync",
                                           "Requests a certain synchronization mode when initiating a sync:\n"
                                           "  two-way             = only send/receive changes since last sync\n"
                                           "  slow                = exchange all items\n"
                                           "  refresh-from-client = discard all remote items and replace with\n"
                                           "                        the items on the client\n"
                                           "  refresh-from-server = discard all local items and replace with\n"
                                           "                        the items on the server\n"
                                           "  one-way-from-client = transmit changes from client\n"
                                           "  one-way-from-server = transmit changes from server\n"
                                           "  disabled (or none)  = synchronization disabled\n"
                                           "When accepting a sync session in a SyncML server (HTTP server), only\n"
                                           "sources with sync != disabled are made available to the client,\n"
                                           "which chooses the final sync mode based on its own configuration.\n"
                                           "When accepting a sync session in a SyncML client (local sync with\n"
                                           "the server contacting SyncEvolution on a device), the sync mode\n"
                                           "specified in the client is typically overriden by the server.\n",
                                           "disabled",
                                           "",
                                           Values() +
                                           (Aliases("two-way")) +
                                           (Aliases("slow")) +
                                           (Aliases("refresh-from-client") + "refresh-client") +
                                           (Aliases("refresh-from-server") + "refresh-server" + "refresh") +
                                           (Aliases("one-way-from-client") + "one-way-client") +
                                           (Aliases("one-way-from-server") + "one-way-server" + "one-way") +
                                           (Aliases("disabled") + "none"));
static bool SourcePropSyncIsSet(boost::shared_ptr<SyncSourceConfig> source)
{
    return source->isSet(SyncSourceConfig::m_sourcePropSync);
}


static class SourceTypeConfigProperty : public StringConfigProperty {
public:
    SourceTypeConfigProperty() :
        StringConfigProperty("type",
                             "Specifies the SyncEvolution backend and thus the\n"
                             "data which is synchronized by this source. Some\n"
                             "backends can exchange data in multiple formats.\n"
                             "Some of them have a default format that is used\n"
                             "automatically unless specified differently.\n"
                             "Sometimes the format must be specified.\n"
                             "\n"
                             "This property can be set for individual peers as\n"
                             "well as for the context. Different peers in the\n"
                             "same context can use different formats, but the\n"
                             "backend must be consistent.\n"
                             "\n"
                             "A special 'virtual' backend combines several other\n"
                             "data sources and presents them as one set of items\n"
                             "to the peer. For example, Nokia phones typically\n"
                             "exchange tasks and events as part of one set of\n"
                             "calendar items.\n"
                             "\n"
                             "Right now such a virtual backend is limited to\n"
                             "combining one calendar source with events and one\n"
                             "task source. They have to be specified in the\n"
                             "'evolutionsource' property, typically like this:\n"
                             "  calendar,todo\n"
                             "\n"
                             "In all cases the format of this configuration is\n"
                             "  <backend>[:format][!]\n"
                             "\n"
                             "Different sources combined in one virtual source must\n"
                             "have a common representation. As with other backends,\n"
                             "the preferred format can be influenced via the 'format'\n"
                             "attribute.\n"
                             "\n"
                             "When there are alternative formats for the same data,\n"
                             "each side offers all that it supports and marks one as\n"
                             "preferred. The other side then picks the format that it\n"
                             "uses for sending data. Some peers get confused by this or\n"
                             "pick the less suitable format. In this case the trailing\n"
                             "exclamation mark can be used to configure exactly one format.\n"
                             "\n"
                             "Here are some valid examples:\n"
                             "  contacts - synchronize address book with default vCard 2.1 format\n"
                             "  contacts:text/vcard - address book with vCard 3.0 format\n"
                             "  calendar - synchronize events in iCalendar 2.0 format\n"
                             "  calendar:text/x-vcalendar - prefer legacy vCalendar 1.0 format\n"
                             "  calendar:text/calendar! - allow only iCalendar 2.0\n"
                             "  virtual:text/x-vcalendar - a virtual backend using vCalendar 1.0 format\n"
                             "\n"
                             "Errors while starting to sync and parsing and/or storing\n"
                             "items on either client or server can be caused by a mismatch between\n"
                             "type and uri.\n"
                             "\n"
                             "Here's the full list of potentially supported backends,\n"
                             "valid <backend> values for each of them, and possible\n"
                             "formats. Note that SyncEvolution installations usually\n"
                             "support only a subset of the backends; that's why e.g.\n"
                             "\"addressbook\" is unambiguous although there are multiple\n"
                             "address book backends.\n",
                             "select backend",
                             "",
                             Values() +
                             (Aliases("virtual")) +
                             (Aliases("calendar") + "events") +
                             (Aliases("calendar:text/calendar") + "text/calendar") +
                             (Aliases("calendar:text/x-vcalendar") + "text/x-vcalendar") +
                             (Aliases("addressbook") + "contacts") +
                             (Aliases("addressbook:text/x-vcard") + "text/x-vcard") +
                             (Aliases("addressbook:text/vcard") + "text/vcard") +
                             (Aliases("todo") + "tasks" + "text/x-todo") +
                             (Aliases("memo") + "memos" + "notes" + "text/plain") +
                             (Aliases("memo:text/calendar") + "text/x-journal"))
    {}

    virtual string getComment() const {
        stringstream enabled, disabled;
        stringstream res;

        SourceRegistry &registry(SyncSource::getSourceRegistry());
        BOOST_FOREACH(const RegisterSyncSource *sourceInfos, registry) {
            const string &comment = sourceInfos->m_typeDescr;
            stringstream *curr = sourceInfos->m_enabled ? &enabled : &disabled;
            *curr << comment;
            if (comment.size() && comment[comment.size() - 1] != '\n') {
                *curr << '\n';
            }
        }

        res << StringConfigProperty::getComment();
        if (enabled.str().size()) {
            res << "\nCurrently active:\n" << enabled.str();
        }
        if (disabled.str().size()) {
            res << "\nCurrently inactive:\n" << disabled.str();
        }

        return boost::trim_right_copy(res.str());
    }

    virtual Values getValues() const {
        Values res(StringConfigProperty::getValues());

        const SourceRegistry &registry(SyncSource::getSourceRegistry());
        BOOST_FOREACH(const RegisterSyncSource *sourceInfos, registry) {
            copy(sourceInfos->m_typeValues.begin(),
                 sourceInfos->m_typeValues.end(),
                 back_inserter(res));
        }

        return res;
    }

    /** relax string checking: only the part before a colon has to match one of the aliases */
    virtual bool checkValue(const string &value, string &error) const {
        size_t colon = value.find(':');
        if (colon != value.npos) {
            string backend = value.substr(0, colon);
            return StringConfigProperty::checkValue(backend, error);
        } else {
            return StringConfigProperty::checkValue(value, error);
        }
    }
} sourcePropSourceType;
static bool SourcePropSourceTypeIsSet(boost::shared_ptr<SyncSourceConfig> source)
{
    return source->isSet(sourcePropSourceType);
}

static ConfigProperty sourcePropDatabaseID("evolutionsource",
                                           "Picks one of backend data sources:\n"
                                           "enter either the name or the full URL.\n"
                                           "Most backends have a default data source,\n"
                                           "like for example the system address book.\n"
                                           "Not setting this property selects that default\n"
                                           "data source.\n"
                                           "If the backend is a virtual data source,\n"
                                           "this field must contain comma seperated list of\n"
                                           "sub datasources actually used to store data.\n"
                                           "If your sub datastore has a comma in name, you\n"
                                           "must prevent taht comma from being mistaken as the\n"
                                           "separator by preceding it with a backslash, like this:\n"
                                           "  evolutionsource=Source1PartA\\,PartB,Source2\\\\Backslash\n"
                                           "\n"
                                           "To get a full list of available data sources,\n"
                                           "run syncevolution without parameters. The name\n"
                                           "is printed in front of the colon, followed by\n"
                                           "the URL. Usually the name is unique and can be\n"
                                           "used to reference the data source. The default\n"
                                           "data source is marked with <default> after the\n"
                                           "URL, if there is a default.\n");
static ConfigProperty sourcePropURI("uri",
                                    "this is appended to the server's URL to identify the\n"
                                    "server's database");
static bool SourcePropURIIsSet(boost::shared_ptr<SyncSourceConfig> source)
{
    return source->isSet(sourcePropURI);
}

static ConfigProperty sourcePropUser("evolutionuser",
                                     "authentication for backend data source; password can be specified\n"
                                     "in multiple ways, see SyncML server password for details\n"
                                     "\n"
                                     "Warning: setting evolutionuser/password in cases where it is not\n"
                                     "needed, as for example with local Evolution calendars and addressbooks,\n"
                                     "can cause the Evolution backend to hang.");
static EvolutionPasswordConfigProperty sourcePropPassword("evolutionpassword", "","", "backend");

static ConfigProperty sourcePropAdminData(SourceAdminDataName,
                                          "used by the Synthesis library internally; do not modify");

static IntConfigProperty sourcePropSynthesisID("synthesisID", "unique integer ID, necessary for libsynthesis", "0");

ConfigPropertyRegistry &SyncSourceConfig::getRegistry()
{
    static ConfigPropertyRegistry registry;
    static bool initialized;

    if (!initialized) {
        registry.push_back(&SyncSourceConfig::m_sourcePropSync);
        registry.push_back(&sourcePropSourceType);
        registry.push_back(&sourcePropDatabaseID);
        registry.push_back(&sourcePropURI);
        registry.push_back(&sourcePropUser);
        registry.push_back(&sourcePropPassword);
        registry.push_back(&sourcePropAdminData);
        registry.push_back(&sourcePropSynthesisID);

        // obligatory source properties
        SyncSourceConfig::m_sourcePropSync.setObligatory(true);

        // hidden source properties - only possible for
        // non-shared properties (other hidden nodes don't
        // exist at the moment)
        sourcePropAdminData.setHidden(true);
        sourcePropSynthesisID.setHidden(true);

        // No global source properties. Does not make sense
        // conceptually.

        // peer independent source properties
        sourcePropDatabaseID.setSharing(ConfigProperty::SOURCE_SET_SHARING);
        sourcePropUser.setSharing(ConfigProperty::SOURCE_SET_SHARING);
        sourcePropPassword.setSharing(ConfigProperty::SOURCE_SET_SHARING);

        // Save "type" also in the shared nodes, so that the backend
        // can be selected independently from a specific peer.
        sourcePropSourceType.setFlags(ConfigProperty::SHARED_AND_UNSHARED);

        initialized = true;
    }

    return registry;
}

SyncSourceNodes::SyncSourceNodes(bool havePeerNode,
                                 const boost::shared_ptr<FilterConfigNode> &sharedNode,
                                 const boost::shared_ptr<FilterConfigNode> &peerNode,
                                 const boost::shared_ptr<ConfigNode> &hiddenPeerNode,
                                 const boost::shared_ptr<ConfigNode> &trackingNode,
                                 const boost::shared_ptr<ConfigNode> &serverNode,
                                 const string &cacheDir) :
    m_havePeerNode(havePeerNode),
    m_sharedNode(sharedNode),
    m_peerNode(peerNode),
    m_hiddenPeerNode(hiddenPeerNode),
    m_trackingNode(trackingNode),
    m_serverNode(serverNode),
    m_cacheDir(cacheDir)
{
    boost::shared_ptr<MultiplexConfigNode> mnode;
    mnode.reset(new MultiplexConfigNode(m_peerNode->getName(),
                                        SyncSourceConfig::getRegistry(),
                                        false));
    mnode->setHavePeerNodes(havePeerNode);
    m_props[false] = mnode;
    mnode->setNode(false, ConfigProperty::SOURCE_SET_SHARING,
                   m_sharedNode);
    mnode->setNode(false, ConfigProperty::NO_SHARING,
                   m_peerNode);
    // no multiplexing necessary for hidden peer properties yet
    m_props[true].reset(new FilterConfigNode(m_hiddenPeerNode));
}


boost::shared_ptr<FilterConfigNode>
SyncSourceNodes::getNode(const ConfigProperty &prop) const
{
    switch (prop.getSharing()) {
    case ConfigProperty::GLOBAL_SHARING:
        return boost::shared_ptr<FilterConfigNode>(new FilterConfigNode(boost::shared_ptr<ConfigNode>(new DevNullConfigNode("no global source properties"))));
        break;
    case ConfigProperty::SOURCE_SET_SHARING:
        if (prop.isHidden()) {
            return boost::shared_ptr<FilterConfigNode>(new FilterConfigNode(boost::shared_ptr<ConfigNode>(new DevNullConfigNode("no hidden source set properties"))));
        } else {
            return m_sharedNode;
        }
        break;
    case ConfigProperty::NO_SHARING:
        if ((prop.getFlags() & ConfigProperty::SHARED_AND_UNSHARED) &&
            !m_havePeerNode &&
            !prop.isHidden()) {
            // special case for "sync": use shared node because
            // peer node does not exist
            return m_sharedNode;
        } else {
            if (prop.isHidden()) {
                return boost::shared_ptr<FilterConfigNode>(new FilterConfigNode(m_hiddenPeerNode));
            } else {
                return m_peerNode;
            }
        }
    }
    return boost::shared_ptr<FilterConfigNode>();
}

const char *SyncSourceConfig::getDatabaseID() const { return m_stringCache.getProperty(*getNode(sourcePropDatabaseID), sourcePropDatabaseID); }
void SyncSourceConfig::setDatabaseID(const string &value, bool temporarily) { sourcePropDatabaseID.setProperty(*getNode(sourcePropDatabaseID), value, temporarily); }
const char *SyncSourceConfig::getUser() const { return m_stringCache.getProperty(*getNode(sourcePropUser), sourcePropUser); }
void SyncSourceConfig::setUser(const string &value, bool temporarily) { sourcePropUser.setProperty(*getNode(sourcePropUser), value, temporarily); }
const char *SyncSourceConfig::getPassword() const {
    string password = sourcePropPassword.getCachedProperty(*getNode(sourcePropPassword), m_cachedPassword);
    return m_stringCache.storeString(sourcePropPassword.getName(), password);
}
void SyncSourceConfig::checkPassword(ConfigUserInterface &ui, 
                                     const string &serverName, 
                                     FilterConfigNode& globalConfigNode) {
    sourcePropPassword.checkPassword(ui, serverName, globalConfigNode, m_name, getNode(sourcePropPassword));
}
void SyncSourceConfig::savePassword(ConfigUserInterface &ui, 
                                    const string &serverName, 
                                    FilterConfigNode& globalConfigNode) {
    sourcePropPassword.savePassword(ui, serverName, globalConfigNode, m_name, getNode(sourcePropPassword));
}
void SyncSourceConfig::setPassword(const string &value, bool temporarily) { m_cachedPassword = ""; sourcePropPassword.setProperty(*getNode(sourcePropPassword), value, temporarily); }
const char *SyncSourceConfig::getURI() const { return m_stringCache.getProperty(*getNode(sourcePropURI), sourcePropURI); }
void SyncSourceConfig::setURI(const string &value, bool temporarily) { sourcePropURI.setProperty(*getNode(sourcePropURI), value, temporarily); }
const char *SyncSourceConfig::getSync() const { return m_stringCache.getProperty(*getNode(m_sourcePropSync), m_sourcePropSync); }
void SyncSourceConfig::setSync(const string &value, bool temporarily) { m_sourcePropSync.setProperty(*getNode(m_sourcePropSync), value, temporarily); }
string SyncSourceConfig::getSourceTypeString(const SyncSourceNodes &nodes) { return sourcePropSourceType.getProperty(*nodes.getNode(sourcePropSourceType)); }
SourceType SyncSourceConfig::getSourceType(const SyncSourceNodes &nodes) {
    string type = getSourceTypeString(nodes);
    SourceType sourceType;
    size_t colon = type.find(':');
    if (colon != type.npos) {
        string backend = type.substr(0, colon);
        string format = type.substr(colon + 1);
        sourcePropSourceType.normalizeValue(backend);
        size_t formatLen = format.size();
        if(format[formatLen - 1] == '!') {
            sourceType.m_forceFormat = true;
            format = format.substr(0, formatLen - 1);
        }
        sourceType.m_backend = backend;
        sourceType.m_format  = format;
    } else {
        sourceType.m_backend = type;
        sourceType.m_format  = "";
    }
    return sourceType;
}
SourceType SyncSourceConfig::getSourceType() const { return getSourceType(m_nodes); }
void SyncSourceConfig::setSourceType(const string &value, bool temporarily) { sourcePropSourceType.setProperty(*getNode(sourcePropSourceType), value, temporarily); }

const int SyncSourceConfig::getSynthesisID() const { return sourcePropSynthesisID.getPropertyValue(*getNode(sourcePropSynthesisID)); }
void SyncSourceConfig::setSynthesisID(int value, bool temporarily) { sourcePropSynthesisID.setProperty(*getNode(sourcePropSynthesisID), value, temporarily); }

ConfigPasswordKey EvolutionPasswordConfigProperty::getPasswordKey(const string &descr,
                                                                  const string &serverName,
                                                                  FilterConfigNode &globalConfigNode,
                                                                  const string &sourceName,
                                                                  const boost::shared_ptr<FilterConfigNode> &sourceConfigNode) const
{
    ConfigPasswordKey key;
    key.user = sourcePropUser.getProperty(*sourceConfigNode);
    key.object = serverName;
    key.object += " ";
    key.object += sourceName;
    key.object += " backend";
    return key;
}

// Used for built-in templates
SyncConfig::TemplateDescription::TemplateDescription (const std::string &name, const std::string &description)
:   m_templateId (name), m_description (description)
{
    m_rank = TemplateConfig::LEVEL3_MATCH;
    m_fingerprint = "";
    m_path = "";
    m_matchedModel = name;
}

/* Ranking of template description is controled by the rank field, larger the
 * better
 */
bool SyncConfig::TemplateDescription::compare_op (boost::shared_ptr<SyncConfig::TemplateDescription> &left, boost::shared_ptr<SyncConfig::TemplateDescription> &right)
{
    //first sort against the fingerprint string
    if (left->m_fingerprint != right->m_fingerprint) {
        return (left->m_fingerprint < right->m_fingerprint);
    }
    // sort against the rank
    if (right->m_rank != left->m_rank) {
        return (right->m_rank < left->m_rank);
    }
    // sort against the template id
    return (left->m_templateId < right->m_templateId);
}

TemplateConfig::TemplateConfig(const string &path) :
    m_template(new SingleFileConfigTree(path))
{
    boost::shared_ptr<ConfigNode> metaNode = m_template->open("template.ini");
    metaNode->readProperties(m_metaProps);
}

bool TemplateConfig::isTemplateConfig (const string &path) 
{
    SingleFileConfigTree templ(path);
    boost::shared_ptr<ConfigNode> metaNode = templ.open("template.ini");
    if (!metaNode->exists()) {
        return false;
    }
    ConfigProps props;
    metaNode->readProperties(props);
    return !props.empty();
}

bool TemplateConfig::isTemplateConfig() const
{
    return !m_metaProps.empty();
}

int TemplateConfig::serverModeMatch (SyncConfig::MatchMode mode)
{
    boost::shared_ptr<ConfigNode> configNode = m_template->open("config.ini");
    std::string peerIsClient = configNode->readProperty ("peerIsClient");
    
    //not a match if serverMode does not match
    if ((peerIsClient.empty() || peerIsClient == "0") && mode == SyncConfig::MATCH_FOR_SERVER_MODE) {
        return NO_MATCH;
    }
    if (peerIsClient == "1" && mode == SyncConfig::MATCH_FOR_CLIENT_MODE){
        return NO_MATCH;
    }
    return BEST_MATCH;
}

/**
 * The matching is based on Least common string algorithm
 * */
int TemplateConfig::fingerprintMatch (const string &fingerprint)
{
    //if input "", match all
    if (fingerprint.empty()) {
        return LEVEL3_MATCH;
    }

    std::string fingerprintProp = m_metaProps["fingerprint"];
    std::vector <string> subfingerprints = unescapeJoinedString (fingerprintProp, ',');
    std::string input = fingerprint;
    boost::to_lower(input);
    //return the largest match value
    int max = NO_MATCH;
    BOOST_FOREACH (std::string sub, subfingerprints){
        if (boost::iequals (sub, "default")){
            if (LEVEL1_MATCH > max) {
                max = LEVEL1_MATCH;
            }
            continue;
        }

        std::vector< LCS::Entry <char> > result;
        std::string match = sub;
        boost::to_lower(match);
        LCS::lcs(match, input, std::back_inserter(result), LCS::accessor_sequence<std::string>());
        int score = result.size() *2 *BEST_MATCH /(sub.size() + fingerprint.size()) ;
        if (score > max) {
            max = score;
        }
    }
    return max;
}

int TemplateConfig::metaMatch (const std::string &fingerprint, SyncConfig::MatchMode mode)
{
    int serverMatch = serverModeMatch (mode);
    if (serverMatch == NO_MATCH){
        return NO_MATCH;
    }
    int fMatch = fingerprintMatch (fingerprint);
    return (serverMatch *1 + fMatch *3) >>2;
}

string TemplateConfig::getDescription(){
    return m_metaProps["description"];
}

string TemplateConfig::getFingerprint(){
    return m_metaProps["fingerprint"];
}

string TemplateConfig::getTemplateName() {
    return m_metaProps["templateName"];
}

/*
 * A unique identifier for this template, it must be unique and retrieveable.
 * We use the first entry in the "fingerprint" property for cmdline.
 **/
string TemplateConfig::getTemplateId(){
    if (m_id.empty()){
        std::string fingerprintProp = m_metaProps["fingerprint"];
        if (!fingerprintProp.empty()){
            std::vector<std::string> subfingerprints = unescapeJoinedString (fingerprintProp, ',');
            m_id = subfingerprints[0];
        }
    }
    return m_id;
}

bool SecondsConfigProperty::checkValue(const string &value, string &error) const
{
    unsigned int seconds;
    return parseDuration(value, error, seconds);
}

unsigned int SecondsConfigProperty::getPropertyValue(const ConfigNode &node, bool *isDefault) const
{
    string name = getName();
    string value = node.readProperty(name);
    if (value.empty()) {
        if (isDefault) {
            *isDefault = true;
        }
        value = getDefValue();
    }
    string error;
    unsigned int seconds;
    if (!parseDuration(value, error, seconds)) {
        throwValueError(node, name, value, error);
    }
    return seconds;
}

bool SecondsConfigProperty::parseDuration(const string &value, string &error, unsigned int &seconds)
{
    seconds = 0;
    if (value.empty()) {
        // ambiguous - zero seconds?!
        error = "duration expected, empty string not valid";
        return false;
    }

    unsigned int current = 0;
    bool haveDigit = false;
    BOOST_FOREACH(char c, value) {
        if (isdigit(c)) {
            current = current * 10 + (c - '0');
            haveDigit = true;
        } else {
            unsigned int multiplier = 1;
            switch (toupper(c)) {
            case 'Y':
                multiplier = 365 * 24 * 60 * 60;
                break;
            case 'D':
                multiplier = 24 * 60 * 60;
                break;
            case 'H':
                multiplier = 60 * 60;
                break;
            case 'M':
                multiplier = 60;
                break;
            case 'S':
                break;
            case ' ':
            case '\t':
                continue;
            case '+':
                break;
            default:
                error = StringPrintf("invalid character '%c'", c);
                return false;
            }
            if (!haveDigit && c != '+') {
                error = StringPrintf("unit character without preceeding number: %c", c);
                return false;
            }
            seconds += current * multiplier;
            current = 0;
            haveDigit = false;
        }
    }
    seconds += current;
    return true;
}


#ifdef ENABLE_UNIT_TESTS

class SyncConfigTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(SyncConfigTest);
    CPPUNIT_TEST(normalize);
    CPPUNIT_TEST(parseDuration);
    CPPUNIT_TEST_SUITE_END();

private:
    void normalize()
    {
        ScopedEnvChange xdg("XDG_CONFIG_HOME", "/dev/null");
        ScopedEnvChange home("HOME", "/dev/null");

        CPPUNIT_ASSERT_EQUAL(std::string("@default"),
                             SyncConfig::normalizeConfigString(""));
        CPPUNIT_ASSERT_EQUAL(std::string("@default"),
                             SyncConfig::normalizeConfigString("@default"));
        CPPUNIT_ASSERT_EQUAL(std::string("@default"),
                             SyncConfig::normalizeConfigString("@DeFaULT"));
        CPPUNIT_ASSERT_EQUAL(std::string("foobar"),
                             SyncConfig::normalizeConfigString("FooBar"));
        CPPUNIT_ASSERT_EQUAL(std::string("foobar@something"),
                             SyncConfig::normalizeConfigString("FooBar@Something"));
        CPPUNIT_ASSERT_EQUAL(std::string("foo_bar_x_y_z"),
                             SyncConfig::normalizeConfigString("Foo/bar\\x:y:z"));
    }

    void parseDuration()
    {
        string error;
        unsigned int seconds;
        unsigned int expected;

        CPPUNIT_ASSERT(!SecondsConfigProperty::parseDuration("foo", error, seconds));
        CPPUNIT_ASSERT_EQUAL(error, string("invalid character 'f'"));
        CPPUNIT_ASSERT(!SecondsConfigProperty::parseDuration("1g", error, seconds));
        CPPUNIT_ASSERT_EQUAL(error, string("invalid character 'g'"));
        CPPUNIT_ASSERT(!SecondsConfigProperty::parseDuration("", error, seconds));
        CPPUNIT_ASSERT_EQUAL(error, string("duration expected, empty string not valid"));

        expected = 5;
        CPPUNIT_ASSERT(SecondsConfigProperty::parseDuration("5", error, seconds));
        CPPUNIT_ASSERT_EQUAL(expected, seconds);
        CPPUNIT_ASSERT(SecondsConfigProperty::parseDuration("05", error, seconds));
        CPPUNIT_ASSERT_EQUAL(expected, seconds);
        CPPUNIT_ASSERT(SecondsConfigProperty::parseDuration("05s", error, seconds));
        CPPUNIT_ASSERT_EQUAL(expected, seconds);
        CPPUNIT_ASSERT(SecondsConfigProperty::parseDuration("5s", error, seconds));
        CPPUNIT_ASSERT_EQUAL(expected, seconds);

        expected = (((1 * 365 + 2) * 24 + 3) * 60 + 4) * 60 + 5;
        CPPUNIT_ASSERT(SecondsConfigProperty::parseDuration("1y2d3H4M5s", error, seconds));
        CPPUNIT_ASSERT_EQUAL(expected, seconds);
        CPPUNIT_ASSERT(SecondsConfigProperty::parseDuration("5 + 1y+2d + 3 H4M", error, seconds));
        CPPUNIT_ASSERT_EQUAL(expected, seconds);

        CPPUNIT_ASSERT(!SecondsConfigProperty::parseDuration("m", error, seconds));
    }
};

SYNCEVOLUTION_TEST_SUITE_REGISTRATION(SyncConfigTest);

#endif // ENABLE_UNIT_TESTS

SE_END_CXX
