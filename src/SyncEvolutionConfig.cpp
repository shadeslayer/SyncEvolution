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
#include "EvolutionSyncSource.h"
#include "FileConfigTree.h"

#include <unistd.h>
#include "config.h"

EvolutionSyncConfig::EvolutionSyncConfig(const string &server) :
    m_server(server),
    m_oldLayout(false)
{
    string root;

    const char *homestr = getenv("HOME");
    string home;
    if (homestr) {
        home = homestr;
    }

    // search for configuration in various places...
    string confname;
    root = home + "/.sync4j/evolution/" + server;
    confname = root + "/spds/syncml/config.txt";
    if (!access(confname.c_str(), F_OK)) {
        m_oldLayout = true;
    } else {
        const char *xdg_root_str = getenv("XDG_CONFIG_HOME");
        string xdg_root;
        if (xdg_root_str) {
            xdg_root = xdg_root_str;
        } else {
            xdg_root = home + "/.config";
        }
        root = xdg_root + "/syncevolution/" + server;
    }

    m_tree.reset(new FileConfigTree(root, m_oldLayout));

    string path(m_oldLayout ? "spds/syncml" : "");
    boost::shared_ptr<ConfigNode> node;
    node = m_tree->open(path, false);
    m_configNode.reset(new FilterConfigNode(node, FilterConfigNode::ConfigFilter_t()));
    m_hiddenNode = m_tree->open(path, true);
}

bool EvolutionSyncConfig::exists() const
{
    return m_configNode->exists();
}

void EvolutionSyncConfig::flush()
{
    m_tree->flush();
}

boost::shared_ptr<PersistentEvolutionSyncSourceConfig> EvolutionSyncConfig::getSyncSourceConfig(const string &name)
{
    SyncSourceNodes nodes = getSyncSourceNodes(name);
    return boost::shared_ptr<PersistentEvolutionSyncSourceConfig>(new PersistentEvolutionSyncSourceConfig(name, nodes));
}

list<string> EvolutionSyncConfig::getSyncSources()
{
    return m_tree->getChildren(m_oldLayout ? "spds/sources" : "sources");
}

SyncSourceNodes EvolutionSyncConfig::getSyncSourceNodes(const string &name,
                                                        const string &changeId)
{
    boost::shared_ptr<FilterConfigNode> configNode;
    boost::shared_ptr<ConfigNode> hiddenNode,
        trackingNode;

    boost::shared_ptr<ConfigNode> node;
    string path = string(m_oldLayout ? "spds/sources/" : "sources/") + name;

    node = m_tree->open(path, false);
    configNode.reset(new FilterConfigNode(node, m_sourceFilter));
    hiddenNode = m_tree->open(path, true);
    if (changeId.size()) {
        trackingNode = m_tree->open(path, true, changeId);
    }

    return SyncSourceNodes(configNode, hiddenNode, trackingNode);
}

static ConfigProperty syncPropSyncURL("syncURL",
                                      "the base URL of the SyncML server:\n"
                                      "- Sync4j 2.3\n"
                                      "syncURL = http://localhost:8080/sync4j/sync\n"
                                      "- Funambol >= 3.0\n"
                                      "syncURL = http://localhost:8080/funambol/ds\n"
                                      "- myFUNAMBOL\n"
                                      "syncURL = http://my.funambol.com/sync\n"
                                      "- sync.scheduleworld.com");
static ConfigProperty syncPropDevID("deviceId",
                                    "the SyncML server gets this string and will use it to keep track of\n"
                                    "changes that still need to be synchronized with this particular\n"
                                    "client; it must be set to something unique if SyncEvolution is used\n"
                                    "to synchronize data between different computers");
static ConfigProperty syncPropUsername("username",
                                       "authorization for the SyncML server",
                                       "your SyncML server account name");
static ConfigProperty syncPropPassword("password", "",
                                       "your SyncML server password");
static BoolConfigProperty syncPropUseProxy("useProxy",
                                           "set to T to enable an HTTP proxy");
static ConfigProperty syncPropProxyHost("proxyHost",
                                        "proxy URL (http://<host>:<port>)");
static ConfigProperty syncPropProxyUsername("proxyUsername",
                                            "authentication for proxy");
static ConfigProperty syncPropProxyPassword("proxyPassword", "");
static StringConfigProperty syncPropClientAuthType("clientAuthType",
                                                   "- empty or \"md5\" for secure method (recommended)",
                                                   "- \"basic\" for insecure method\n"
                                                   "md5",
                                                   Values() +
                                                   (Aliases("syncml:auth-basic") + "basic") +
                                                   (Aliases("syncml:auth-md5") + "md5" + ""));
static ULongConfigProperty syncPropMaxMsgSize("maxMsgSize",
                                              "Support for large objects and limiting the message size was added in\n"
                                              "SyncEvolution 0.5, but still disabled in the example configurations\n"
                                              "of that version. Some servers had problems with that configuration,\n"
                                              "so now both features are enabled by default and it is recommended\n"
                                              "to update existing configurations.\n"
                                              "\n"
                                              "The maximum size of each message can be set (maxMsgSize) and the\n"
                                              "server can be told to never sent items larger than a certain\n"
                                              "threshold (maxObjSize). Presumably the server has to truncate or\n"
                                              "skip larger items. Finally the client and server may be given the\n"
                                              "permission to transmit large items in multiple messages (loSupport =\n"
                                              "large object support).",
                                              "8192");
static BoolConfigProperty syncPropLoSupport("loSupport", "", "T");
static UIntConfigProperty syncPropMaxObjSize("maxObjSize", "", "500000");

static BoolConfigProperty syncPropCompression("enableCompression", "enable compression of network traffic (not currently supported)");
static ConfigProperty syncPropServerNonce("serverNonce", "");
static ConfigProperty syncPropClientNonce("clientNonce", "");
static ConfigProperty syncPropDevInfHash("devInfoHash", "");
static ConfigProperty syncPropLogDir("logdir",
                                     "full path to directory where automatic backups and logs\n"
                                     "are stored for all synchronizations; if empty, the temporary\n"
                                     "directory \"$TMPDIR/SyncEvolution-<username>-<server>\" will\n"
                                     "be used to keep the data of just the latest synchronization run;\n"
                                     "if \"none\", then no backups of the databases are made and any\n"
                                     "output is printed directly instead of writing it into a log\n"
                                     "\n"
                                     "When writing into a log nothing will be shown during the\n"
                                     "synchronization. Instead the important messages are extracted\n"
                                     "automatically from the log at the end.");
static IntConfigProperty syncPropMaxLogDirs("maxlogdirs",
                                            "Unless this option is set, SyncEvolution will never delete\n"
                                            "anything in the \"logdir\". If set, the oldest directories and\n"
                                            "all their content will be removed after a successful sync\n"
                                            "to prevent the number of log directories from growing beyond\n"
                                            "the given limit.");
static IntConfigProperty syncPropLogLevel("loglevel",
                                          "level of detail for log messages:\n"
                                          "- 0 (or unset) = INFO messages without log file, DEBUG with log file\n"
                                          "- 1 = only ERROR messages\n"
                                          "- 2 = also INFO messages\n"
                                          "- 3 = also DEBUG messages");

ConfigPropertyRegistry &EvolutionSyncConfig::getRegistry()
{
    static ConfigPropertyRegistry registry;
    static bool initialized;

    if (!initialized) {
        registry.push_back(&syncPropSyncURL);
        registry.push_back(&syncPropDevID);
        registry.push_back(&syncPropUsername);
        registry.push_back(&syncPropPassword);
        registry.push_back(&syncPropLogDir);
        registry.push_back(&syncPropLogLevel);
        registry.push_back(&syncPropMaxLogDirs);
        registry.push_back(&syncPropUseProxy);
        registry.push_back(&syncPropProxyHost);
        registry.push_back(&syncPropProxyUsername);
        registry.push_back(&syncPropProxyPassword);
        registry.push_back(&syncPropClientAuthType);
        registry.push_back(&syncPropMaxMsgSize);
        registry.push_back(&syncPropMaxObjSize);
        registry.push_back(&syncPropLoSupport);
        registry.push_back(&syncPropCompression);

        registry.push_back(&syncPropServerNonce);
        syncPropServerNonce.setHidden(true);
        registry.push_back(&syncPropClientNonce);
        syncPropClientNonce.setHidden(true);
        registry.push_back(&syncPropDevInfHash);
        syncPropDevInfHash.setHidden(true);
        initialized = true;
    }

    return registry;
}

const char *EvolutionSyncConfig::getUsername() const { return m_stringCache.getProperty(*m_configNode, syncPropUsername); }
void EvolutionSyncConfig::setUsername(const string &value, bool temporarily) { syncPropUsername.setProperty(*m_configNode, value, temporarily); }
const char *EvolutionSyncConfig::getPassword() const { return m_stringCache.getProperty(*m_configNode, syncPropPassword); }
void EvolutionSyncConfig::setPassword(const string &value, bool temporarily) { syncPropPassword.setProperty(*m_configNode, value, temporarily); }
bool EvolutionSyncConfig::getUseProxy() const { return syncPropUseProxy.getProperty(*m_configNode); }
void EvolutionSyncConfig::setUseProxy(bool value, bool temporarily) { syncPropUseProxy.setProperty(*m_configNode, value, temporarily); }
const char *EvolutionSyncConfig::getProxyHost() const { return m_stringCache.getProperty(*m_configNode, syncPropProxyHost); }
void EvolutionSyncConfig::setProxyHost(const string &value, bool temporarily) { syncPropProxyHost.setProperty(*m_configNode, value, temporarily); }
const char *EvolutionSyncConfig::getProxyUsername() const { return m_stringCache.getProperty(*m_configNode, syncPropProxyUsername); }
void EvolutionSyncConfig::setProxyUsername(const string &value, bool temporarily) { syncPropProxyUsername.setProperty(*m_configNode, value, temporarily); }
const char *EvolutionSyncConfig::getProxyPassword() const { return m_stringCache.getProperty(*m_configNode, syncPropProxyPassword); }
void EvolutionSyncConfig::setProxyPassword(const string &value, bool temporarily) { syncPropProxyPassword.setProperty(*m_configNode, value, temporarily); }
const char *EvolutionSyncConfig::getSyncURL() const { return m_stringCache.getProperty(*m_configNode, syncPropSyncURL); }
void EvolutionSyncConfig::setSyncURL(const string &value, bool temporarily) { syncPropSyncURL.setProperty(*m_configNode, value, temporarily); }
const char *EvolutionSyncConfig::getClientAuthType() const { return m_stringCache.getProperty(*m_configNode, syncPropClientAuthType); }
void EvolutionSyncConfig::setClientAuthType(const string &value, bool temporarily) { syncPropClientAuthType.setProperty(*m_configNode, value, temporarily); }
bool EvolutionSyncConfig::getLoSupport() const { return syncPropLoSupport.getProperty(*m_configNode); }
void EvolutionSyncConfig::setLoSupport(bool value, bool temporarily) { syncPropLoSupport.setProperty(*m_configNode, value, temporarily); }
unsigned long  EvolutionSyncConfig::getMaxMsgSize() const { return syncPropMaxMsgSize.getProperty(*m_configNode); }
void EvolutionSyncConfig::setMaxMsgSize(unsigned long value, bool temporarily) { syncPropMaxMsgSize.setProperty(*m_configNode, value, temporarily); }
unsigned int  EvolutionSyncConfig::getMaxObjSize() const { return syncPropMaxObjSize.getProperty(*m_configNode); }
void EvolutionSyncConfig::setMaxObjSize(unsigned int value, bool temporarily) { syncPropMaxObjSize.setProperty(*m_configNode, value, temporarily); }
bool EvolutionSyncConfig::getCompression() const { return syncPropCompression.getProperty(*m_configNode); }
void EvolutionSyncConfig::setCompression(bool value, bool temporarily) { syncPropCompression.setProperty(*m_configNode, value, temporarily); }
const char *EvolutionSyncConfig::getDevID() const { return m_stringCache.getProperty(*m_configNode, syncPropDevID); }
void EvolutionSyncConfig::setDevID(const string &value, bool temporarily) { syncPropDevID.setProperty(*m_configNode, value, temporarily); }
const char *EvolutionSyncConfig::getServerNonce() const { return m_stringCache.getProperty(*m_hiddenNode, syncPropServerNonce); }
void EvolutionSyncConfig::setServerNonce(const char *value) { syncPropServerNonce.setProperty(*m_hiddenNode, value); }
const char *EvolutionSyncConfig::getClientNonce() const { return m_stringCache.getProperty(*m_hiddenNode, syncPropClientNonce); }
void EvolutionSyncConfig::setClientNonce(const char *value) { syncPropClientNonce.setProperty(*m_hiddenNode, value); }
const char *EvolutionSyncConfig::getDevInfHash() const { return m_stringCache.getProperty(*m_hiddenNode, syncPropDevInfHash); }
void EvolutionSyncConfig::setDevInfHash(const char *value) { syncPropDevInfHash.setProperty(*m_hiddenNode, value); }
const char *EvolutionSyncConfig::getLogDir() const { return m_stringCache.getProperty(*m_configNode, syncPropLogDir); }
void EvolutionSyncConfig::setLogDir(const string &value, bool temporarily) { syncPropLogDir.setProperty(*m_configNode, value, temporarily); }
int EvolutionSyncConfig::getMaxLogDirs() const { return syncPropMaxLogDirs.getProperty(*m_configNode); }
void EvolutionSyncConfig::setMaxLogDirs(int value, bool temporarily) { syncPropMaxLogDirs.setProperty(*m_configNode, value, temporarily); }
int EvolutionSyncConfig::getLogLevel() const { return syncPropLogLevel.getProperty(*m_configNode); }
void EvolutionSyncConfig::setLogLevel(int value, bool temporarily) { syncPropLogLevel.setProperty(*m_configNode, value, temporarily); }


void EvolutionSyncConfig::setDefaults(const string &serverTemplate)
{
    ConfigPropertyRegistry registry = getRegistry();

    for (ConfigPropertyRegistry::const_iterator it = registry.begin();
         it != registry.end();
         ++it) {
        if (!(*it)->isHidden()) {
            (*it)->setProperty(*m_configNode, (*it)->getDefValue());
        }
    }
    flush();
}

void EvolutionSyncConfig::setSourceDefaults(const string &name)
{
    SyncSourceNodes nodes = getSyncSourceNodes(name);
    ConfigPropertyRegistry registry = EvolutionSyncSourceConfig::getRegistry();

    for (ConfigPropertyRegistry::const_iterator it = registry.begin();
         it != registry.end();
         ++it) {
        if (!(*it)->isHidden()) {
            (*it)->setProperty(*nodes.m_configNode, (*it)->getDefValue());
        }
    }
    nodes.m_configNode->flush();
}

const char *EvolutionSyncConfig::getSwv() const { return VERSION; }
const char *EvolutionSyncConfig::getDevType() const { return DEVICE_TYPE; }

                     
EvolutionSyncSourceConfig::EvolutionSyncSourceConfig(const string &name, const SyncSourceNodes &nodes) :
    m_name(name),
    m_nodes(nodes)
{
}

static ConfigProperty sourcePropSync("sync",
                                     "requests a certain synchronization mode:\n"
                                     "  two-way             = only send/receive changes since last sync\n"
                                     "  slow                = exchange all items\n"
                                     "  refresh-from-client = discard all remote items and replace with\n"
                                     "                        the items on the client\n"
                                     "  refresh-from-server = discard all local items and replace with\n"
                                     "                        the items on the server\n"
                                     "  one-way-from-client = transmit changes from client\n"
                                     "  one-way-from-server = transmit changes from server\n"
                                     "  none                = synchronization disabled",
                                     "two-way");
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
                             "In all cases the format of this configuration is\n"
                             "  <backend>[:format]\n"
                             "\n"
                             "Here are some valid examples:\n"
                             "  contacts - synchronize address book with default vCard 2.1 format\n"
                             "  contacts:text/vcard - address book with vCard 3.0 format\n"
                             "  calendar - synchronize events in iCalendar 2.0 format\n"
                             "\n"
                             "Sending and receiving items in the same format as used by the server for\n"
                             "the uri selected below is essential. Errors while parsing and/or storing\n"
                             "items on either client or server can be caused by a mismatch between\n"
                             "type and uri.\n"
                             "\n"
                             "Here's the full list of potentially supported backends,\n"
                             "valid <backend> values for each of them and possible\n"
                             "formats. Note that SyncEvolution installations usually\n"
                             "support only a subset of the backends; that's why e.g.\n"
                             "\"addressbook\" is usually unambiguous although there are multiple\n"
                             "address book backends.\n",
                             "select backend",
                             Values() +
                             (Aliases("calendar") + "events") +
                             (Aliases("calendar:text/calendar") + "text/calendar") +
                             (Aliases("addressbook") + "contacts") +
                             (Aliases("addressbook:text/x-vcard") + "text/x-vcard") +
                             (Aliases("addressbook:text/vcard") + "text/vcard") +
                             (Aliases("todo") + "tasks" + "text/x-todo") +
                             (Aliases("memos") + "text/plain") +
                             (Aliases("memo:text/calendar") + "text/x-journal"))
    {}

    virtual string getComment() const {
        stringstream enabled, disabled;
        stringstream res;

        SourceRegistry &registry(EvolutionSyncSource::getSourceRegistry());
        for (SourceRegistry::const_iterator it = registry.begin();
             it != registry.end();
             ++it) {
            const string &comment = (*it)->m_typeDescr;
            stringstream *curr = (*it)->m_enabled ? &enabled : &disabled;
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

        return res.str();
    }

    virtual Values getValues() const {
        Values res(StringConfigProperty::getValues());

        const SourceRegistry &registry(EvolutionSyncSource::getSourceRegistry());
        for (SourceRegistry::const_iterator it = registry.begin();
             it != registry.end();
             ++it) {
            for (Values::const_iterator v = (*it)->m_typeValues.begin();
                 v != (*it)->m_typeValues.end();
                 ++v) {
                res.push_back(*v);
            }
        }

        return res;
    }
} sourcePropSourceType;

static ConfigProperty sourcePropDatabaseID("evolutionsource",
                                           "picks one of Evolution's data sources:\n"
                                           "enter either the name or the full URL\n"
                                           "\n"
                                           "To get a full list of available data sources,\n"
                                           "run syncevolution without parameters. The name\n"
                                           "is printed in front of the colon, followed by\n"
                                           "the URL. Usually the name is unique and can be\n"
                                           "used to reference the data source.");
static ConfigProperty sourcePropURI("uri",
                                    "this is appended to the server's URL to identify the\n"
                                    "server's database");
static ConfigProperty sourcePropUser("evolutionuser",
                                     "authentication for Evolution source\n"
                                     "\n"
                                     "Warning: setting evolutionuser/password in cases where it is not\n"
                                     "needed as with local calendars and addressbooks can cause the\n"
                                     "Evolution backend to hang.");
static ConfigProperty sourcePropPassword("evolutionpassword", "");

static ConfigProperty sourcePropEncoding("encoding", "\"b64\" enables base64 encoding of outgoing items (not recommended)");
static ULongConfigProperty sourcePropLast("last",
                                          "used by the SyncML library internally; do not modify");

ConfigPropertyRegistry &EvolutionSyncSourceConfig::getRegistry()
{
    static ConfigPropertyRegistry registry;
    static bool initialized;

    if (!initialized) {
        registry.push_back(&sourcePropSync);
        registry.push_back(&sourcePropSourceType);
        registry.push_back(&sourcePropDatabaseID);
        registry.push_back(&sourcePropURI);
        registry.push_back(&sourcePropUser);
        registry.push_back(&sourcePropPassword);
        registry.push_back(&sourcePropEncoding);
        registry.push_back(&sourcePropLast);
        sourcePropLast.setHidden(true);
        initialized = true;
    }

    return registry;
}

const char *EvolutionSyncSourceConfig::getDatabaseID() const { return m_stringCache.getProperty(*m_nodes.m_configNode, sourcePropDatabaseID); }
void EvolutionSyncSourceConfig::setDatabaseID(const string &value, bool temporarily) { sourcePropDatabaseID.setProperty(*m_nodes.m_configNode, value, temporarily); }
const char *EvolutionSyncSourceConfig::getUser() const { return m_stringCache.getProperty(*m_nodes.m_configNode, sourcePropUser); }
void EvolutionSyncSourceConfig::setUser(const string &value, bool temporarily) { sourcePropUser.setProperty(*m_nodes.m_configNode, value, temporarily); }
const char *EvolutionSyncSourceConfig::getPassword() const { return m_stringCache.getProperty(*m_nodes.m_configNode, sourcePropPassword); }
void EvolutionSyncSourceConfig::setPassword(const string &value, bool temporarily) { sourcePropPassword.setProperty(*m_nodes.m_configNode, value, temporarily); }
const char *EvolutionSyncSourceConfig::getURI() const { return m_stringCache.getProperty(*m_nodes.m_configNode, sourcePropURI); }
void EvolutionSyncSourceConfig::setURI(const string &value, bool temporarily) { sourcePropURI.setProperty(*m_nodes.m_configNode, value, temporarily); }
const char *EvolutionSyncSourceConfig::getSync() const { return m_stringCache.getProperty(*m_nodes.m_configNode, sourcePropSync); }
void EvolutionSyncSourceConfig::setSync(const string &value, bool temporarily) { sourcePropSync.setProperty(*m_nodes.m_configNode, value, temporarily); }
const char *EvolutionSyncSourceConfig::getEncoding() const { return m_stringCache.getProperty(*m_nodes.m_configNode, sourcePropEncoding); }
void EvolutionSyncSourceConfig::setEncoding(const string &value, bool temporarily) { sourcePropEncoding.setProperty(*m_nodes.m_configNode, value, temporarily); }
unsigned long EvolutionSyncSourceConfig::getLast() const { return sourcePropLast.getProperty(*m_nodes.m_hiddenNode); }
void EvolutionSyncSourceConfig::setLast(unsigned long timestamp) { sourcePropLast.setProperty(*m_nodes.m_hiddenNode, timestamp); }
string EvolutionSyncSourceConfig::getSourceTypeString(const SyncSourceNodes &nodes) { return sourcePropSourceType.getProperty(*nodes.m_configNode); }
pair<string, string> EvolutionSyncSourceConfig::getSourceType(const SyncSourceNodes &nodes) {
    string type = getSourceTypeString(nodes);
    size_t colon = type.find(':');
    if (colon != type.npos) {
        string backend = type.substr(0, colon);
        string format = type.substr(colon + 1);
        sourcePropSourceType.normalizeValue(backend);
        return pair<string, string>(backend, format);
    } else {
        return pair<string, string>(type, "");
    }
}
pair<string, string> EvolutionSyncSourceConfig::getSourceType() const { return getSourceType(m_nodes); }
void EvolutionSyncSourceConfig::setSourceType(const string &value, bool temporarily) { sourcePropSourceType.setProperty(*m_nodes.m_configNode, value, temporarily); }
