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
#ifndef INCL_SYNC_EVOLUTION_CONFIG
# define INCL_SYNC_EVOLUTION_CONFIG

#include "FilterConfigNode.h"

#include "spds/AbstractSyncConfig.h"
#include "spds/AbstractSyncSourceConfig.h"
#include <boost/shared_ptr.hpp>
#include <list>
#include <string>
#include <sstream>
using namespace std;

class EvolutionSyncSourceConfig;
class PersistentEvolutionSyncSourceConfig;
class ConfigTree;
struct SyncSourceNodes;

/**
 * A property has a name and a comment. Derived classes might have
 * additional code to read and write the property from/to a
 * ConfigNode. They might also one or more  of the properties
 * on the fly, therefore the virtual get methods which return a
 * string value and not just a reference.
 *
 * A default value is returned if the ConfigNode doesn't have
 * a value set (= empty string). Invalid values in the configuration
 * trigger an exception. Setting invalid values does not because
 * it is not known where the value comes from - the caller should
 * check it himself.
 */
class ConfigProperty {
 public:
    ConfigProperty(const string &name, const string &comment, const string &def = string("")) :
    m_name(name),
        m_comment(comment),
        m_defValue(def),
        m_hidden(false)
        {}
    
    virtual string getName() const { return m_name; }
    virtual string getComment() const { return m_comment; }
    virtual string getDefValue() const { return m_defValue; }

    /**
     * Check whether the given value is okay.
     * If not, then set an error string (one line, no punctuation).
     *
     * @return true if value is okay
     */
    virtual bool checkValue(const string &value, string &error) const { return true; }

    /** split \n separated comment into lines without \n, appending them to commentLines */
    static void splitComment(const string &comment, list<string> &commentLines);

    bool isHidden() const { return m_hidden; }
    void setHidden(bool hidden) { m_hidden = hidden; }

    /** set value unconditionally, even if it is not valid */
    void setProperty(ConfigNode &node, const string &value) const { node.setProperty(getName(), value, getComment()); }
    void setProperty(FilterConfigNode &node, const string &value, bool temporarily = false) const {
        if (temporarily) {
            node.addFilter(m_name, value);
        } else {
            node.setProperty(m_name, value, getComment());
        }
    }
    virtual string getProperty(const ConfigNode &node) const {
        string name = getName();
        string value = node.readProperty(name);
        if (value.size()) {
            string error;
            if (!checkValue(value, error)) {
                throwValueError(node, name, value, error);
            }
            return value;
        } else {
            return getDefValue();
        }
    }

 protected:
    void throwValueError(const ConfigNode &node, const string &name, const string &value, const string &error) const;

 private:
    bool m_hidden;
    const string m_name, m_comment, m_defValue;
};

template<class T> class InitList : public list<T> {
 public:
    InitList() {}
    InitList(const T &initialValue) {
        push_back(initialValue);
    }
    InitList &operator + (const T &rhs) {
        push_back(rhs);
        return *this;
    }
    InitList &operator += (const T &rhs) {
        push_back(rhs);
        return *this;
    }
};
typedef InitList<string> Aliases;
typedef InitList<Aliases> Values;


/**
 * A string property which maps multiple different possible value
 * strings to one generic value, ignoring the case. Values not listed
 * are passed through unchanged. The first value in the list of
 * aliases is the generic one.
 *
 * The addition operator is defined for the aliases so that they
 * can be constructed more easily.
 */
class StringConfigProperty : public ConfigProperty {
 public:
    StringConfigProperty(const string &name, const string &comment,
                         const string &def = string(""),
                         const Values &values = Values()) :
    ConfigProperty(name, comment, def),
        m_values(values)
        {}

    /**
     * @return false if aliases are defined and the string is not one of them
     */
    bool normalizeValue(string &res) const {
        Values values = getValues();
        for (Values::const_iterator value = values.begin();
             value != values.end();
             ++value) {
            for (Aliases::const_iterator alias = value->begin();
                 alias != value->end();
                 ++alias) {
                if (!strcasecmp(res.c_str(), alias->c_str())) {
                    res = *value->begin();
                    return true;
                }
            }
        }
        return values.empty();
    }

    /**
     * This implementation accepts all values if no aliases
     * are given, otherwise the value must be part of the aliases.
     */
    virtual bool checkValue(const string &propValue, string &error) const {
        Values values = getValues();
        if (values.empty()) {
            return true;
        }

        ostringstream err;
        err << "not one of the valid values (";
        for (Values::const_iterator value = values.begin();
             value != values.end();
             ++value) {
            if (value != values.begin()) {
                err << ", ";
            }
            for (Aliases::const_iterator alias = value->begin();
                 alias != value->end();
                 ++alias) {
                if (alias != value->begin()) {
                    err << " = ";
                }
                if (alias->empty()) {
                    err << "\"\"";
                } else {
                    err << *alias;
                }
                
                if (!strcasecmp(propValue.c_str(), alias->c_str())) {
                    return true;
                }
            }
        }
        err << ")";
        error = err.str();
        return false;
    }

    virtual string getProperty(const ConfigNode &node) const {
        string res = ConfigProperty::getProperty(node);
        normalizeValue(res);
        return res;
    }

 protected:
    virtual Values getValues() const { return m_values; }

 private:
    const Values m_values;
};


/**
 * Instead of reading and writing strings, this class interprets the content
 * as a specific type.
 */
template<class T> class TypedConfigProperty : public ConfigProperty {
 public:
    TypedConfigProperty(const string &name, const string &comment, const string &defValue = string("0")) :
    ConfigProperty(name, comment, defValue)
        {}

    /**
     * This implementation accepts all values that can be converted
     * to the required type.
     */
    virtual bool checkValue(const string &value, string &error) const {
        istringstream in(value);
        T res;
        if (in >> res) {
            return true;
        } else {
            error = "cannot parse value";
        }
    }

    void setProperty(ConfigNode &node, const T &value) const {
        ostringstream out;

        out << value;
        node.setProperty(getName(), out.str(), getComment());
    }
    void setProperty(FilterConfigNode &node, const T &value, bool temporarily = false) const {
        ostringstream out;

        out << value;
        if (temporarily) {
            node.addFilter(getName(), out.str());
        } else {
            node.setProperty(getName(), out.str(), getComment());
        }
    }

    T getProperty(ConfigNode &node) {
        string name = getName();
        string value = node.readProperty(name);
        istringstream in(value);
        T res;
        if (value.empty()) {
            istringstream defStream(getDefValue());
            defStream >> res;
            return res;
        } else {
            if (!(in >> res)) {
                throwValueError(node, name, value, "cannot parse value");
            }
            return res;
        }
    }
};

typedef TypedConfigProperty<int> IntConfigProperty;
typedef TypedConfigProperty<unsigned int> UIntConfigProperty;
typedef TypedConfigProperty<long> LongConfigProperty;
typedef TypedConfigProperty<unsigned long> ULongConfigProperty;


/**
 * Instead of reading and writing strings, this class interprets the content
 * as boolean with T/F or 1/0 (default format).
 */
class BoolConfigProperty : public StringConfigProperty {
 public:
    BoolConfigProperty(const string &name, const string &comment, const string &defValue = string("F")) :
    StringConfigProperty(name, comment, defValue,
                         Values() + (Aliases("1") + "T" + "TRUE") + (Aliases("0") + "F" + "FALSE"))
        {}

    void setProperty(ConfigNode &node, bool value) {
        StringConfigProperty::setProperty(node, value ? "1" : "0");
    }
    void setProperty(FilterConfigNode &node, bool value, bool temporarily = false) {
        StringConfigProperty::setProperty(node, value ? "1" : "0", temporarily);
    }
    int getProperty(ConfigNode &node) {
        string res = ConfigProperty::getProperty(node);

        return !strcasecmp(res.c_str(), "T") ||
            !strcasecmp(res.c_str(), "TRUE") ||
            atoi(res.c_str()) != 0;
    }
};

/**
 * A registry for all properties which might be saved in the same ConfigNode.
 * Currently the same as a simple list. Someone else owns the instances.
 */
class ConfigPropertyRegistry : public list<const ConfigProperty *> {
 public:
    /** case-insensitive search for property */
    const ConfigProperty *find(const string &propName) const {
        for (const_iterator it = begin();
             it != end();
             ++it) {
            if (!strcasecmp((*it)->getName().c_str(), propName.c_str())) {
                return *it;
            }
        }
        return NULL;
    }
};

/**
 * Store the current string value of a property in a cache
 * and return the "const char *" pointer that is expected by
 * the client library.
 */
class ConfigStringCache {
 public:
    const char *getProperty(const ConfigNode &node, const ConfigProperty &prop) {
        string value = prop.getProperty(node);
        pair< map<string, string>::iterator, bool > res = m_cache.insert(pair<string,string>(prop.getName(), value));
        if (!res.second) {
            res.first->second = value;
        }
        return res.first->second.c_str();
    }

 private:
    map<string, string> m_cache;
};

/**
 * This class implements the client library configuration interface
 * by mapping values to properties to entries in a ConfigTree. The
 * mapping is either the traditional one used by SyncEvolution <= 0.7
 * and client library <= 6.5 or the new layout introduced with
 * SyncEvolution >= 0.8. If for a given server name the old config
 * exists, then it is used. Otherwise the new layout is used.
 *
 * This class can be instantiated on its own and then provides access
 * to properties actually stored in files. EvolutionSyncClient
 * inherits from this class so that a derived client has the chance to
 * override every single property (although it doesn't have to).
 * Likewise EvolutionSyncSource is derived from
 * EvolutionSyncSourceConfig.
 *
 * Properties can be set permanently (this changes the underlying
 * ConfigNode) and temporarily (this modifies the FilterConfigNode
 * which wraps the ConfigNode).
 *
 * The old layout is:
 * - $HOME/.sync4j/evolution/<server>/spds/syncml/config.txt
 * -- spds/sources/<source>/config.txt
 * ---                      changes_<changeid>/config.txt
 *
 * The new layout is:
 * - ${XDG_CONFIG:-${HOME}/.config}/syncevolution/foo - base directory for server foo
 * -- config.ini - constant per-server settings
 * -- .internal.ini - read/write server properties - hidden from users because of the leading dot
 * -- sources/bar - base directory for source bar
 * --- config.ini - constant per-source settings
 * --- .internal.ini - read/write source properties
 * --- .changes_<changeid>.ini - change tracking node (content under control of sync source)
 *
 * Because this class needs to handle different file layouts it always
 * uses a FileConfigTree instance. Other implementations would be
 * possible.
 */
class EvolutionSyncConfig : public AbstractSyncConfig {
 public:
    /**
     * Opens the configuration for a specific server.
     * Will succeed even if config does not yet exist.
     */
    EvolutionSyncConfig(const string &server);

    typedef list< pair<string, string> > ServerList;

    /**
     * returns list of servers in either the old (.sync4j) or
     * new config directory (.config), given as server name
     * and absolute root of config
     */
    static ServerList getServers();

    /** true if the main configuration file already exists */
    bool exists() const;

    /** write changes */
    void flush();

    /**
     * A list of all properties. Can be extended by derived clients.
     */
    static ConfigPropertyRegistry &getRegistry();

    /**
     * Replaces the property filter of either the sync properties or
     * all sources. This can be used to e.g. temporarily override
     * the active sync mode.
     */
    void setConfigFilter(bool sync, const FilterConfigNode::ConfigFilter &filter) {
        if (sync) {
            m_configNode->setFilter(filter);
        } else {
            m_sourceFilter = filter;
        }
    }

    /**
     * Read-write access to all configurable properties of the server,
     * including any config filter set earlier.
     */
    virtual boost::shared_ptr<FilterConfigNode> getProperties() { return m_configNode; }

    /**
     * Returns a wrapper around all properties of the given source
     * which are saved in the config tree. Note that this is different
     * from the set of sync source configs used by the SyncManager:
     * the SyncManger uses the AbstractSyncSourceConfig. In
     * SyncEvolution those are implemented by the
     * EvolutionSyncSource's actually instantiated by
     * EvolutionSyncClient. Those are complete whereas
     * PersistentEvolutionSyncSourceConfig only provides access to a
     * subset of the properties.
     *
     * Can be called for sources which do not exist yet.
     */
    virtual boost::shared_ptr<PersistentEvolutionSyncSourceConfig> getSyncSourceConfig(const string &name);

    /**
     * Returns list of all configured (not active!) sync sources.
     */
    virtual list<string> getSyncSources();

    /**
     * Creates config nodes for a certain node. The nodes are not
     * yet created in the backend if they do not yet exist.
     *
     * @param name       the name of the sync source
     * @param trackName  additional part of the tracking node name (used for unit testing)
     */
    SyncSourceNodes getSyncSourceNodes(const string &name,
                                       const string &trackName = "");

    /**
     * create a new sync configuration for the current server name
     *
     * @name serverTemplate     use default settings for this server (e.g. "funambol", "scheduleworld")
     */
    void setDefaults(const string &serverTemplate);

    /**
     * create a new sync source configuration
     */
    void setSourceDefaults(const string &name);

    /**
     * @defgroup SyncEvolutionSettings
     *
     * See etc/syncml-config.txt and the property definitions in
     * SyncEvolutionConfig.cpp for the user-visible explanations of
     * these settings.
     *
     * @{
     */

    virtual const char *getLogDir() const;
    virtual void setLogDir(const string &value, bool temporarily = false);

    virtual int getMaxLogDirs() const;
    virtual void setMaxLogDirs(int value, bool temporarily = false);

    virtual int getLogLevel() const;
    virtual void setLogLevel(int value, bool temporarily = false);

    /**@}*/

    /**
     * @defgroup AbstractSyncConfig
     *
     * These settings are required by the Funambol C++ client library.
     * Some of them are hard-coded in this class. A derived class could
     * make them configurable again, should that be desired.
     *
     * @{
     */

    /**
     * @defgroup ActiveSyncSources
     *
     * This group of calls grants access to all active sources. In
     * SyncEvolution the EvolutionSyncClient class decides which
     * sources are active and thus fully configured and reimplements
     * these calls.
     *
     * @{
     */
    virtual AbstractSyncSourceConfig* getAbstractSyncSourceConfig(const char* name) const { return NULL; }
    virtual AbstractSyncSourceConfig* getAbstractSyncSourceConfig(unsigned int i) const { return NULL; }
    virtual unsigned int getAbstractSyncSourceConfigsCount() const { return 0; }
    /**@}*/

    virtual const char*  getUsername() const;
    virtual void setUsername(const string &value, bool temporarily = false);
    virtual const char*  getPassword() const;
    virtual void setPassword(const string &value, bool temporarily = false);
    virtual bool getUseProxy() const;
    virtual void setUseProxy(bool value, bool temporarily = false);
    virtual const char*  getProxyHost() const;
    virtual void setProxyHost(const string &value, bool temporarily = false);
    virtual int getProxyPort() const { return 0; }
    virtual const char* getProxyUsername() const;
    virtual void setProxyUsername(const string &value, bool temporarily = false);
    virtual const char* getProxyPassword() const;
    virtual void setProxyPassword(const string &value, bool temporarily = false);
    virtual const char*  getSyncURL() const;
    virtual void setSyncURL(const string &value, bool temporarily = false);
    virtual const char*  getClientAuthType() const;
    virtual void setClientAuthType(const string &value, bool temporarily = false);
    virtual bool getLoSupport() const;
    virtual void setLoSupport(bool value, bool temporarily = false);
    virtual unsigned long getMaxMsgSize() const;
    virtual void setMaxMsgSize(unsigned long value, bool temporarily = false);
    virtual unsigned int getMaxObjSize() const;
    virtual void setMaxObjSize(unsigned int value, bool temporarily = false);
    virtual unsigned long getReadBufferSize() const { return 0; }
    virtual bool  getCompression() const;
    virtual void setCompression(bool value, bool temporarily = false);
    virtual unsigned int getResponseTimeout() const { return 0; }
    virtual const char*  getDevID() const;
    virtual void setDevID(const string &value, bool temporarily = false);

    virtual bool getServerAuthRequired() const { return false; }
    virtual const char*  getServerAuthType() const { return ""; }
    virtual const char*  getServerPWD() const { return ""; }
    virtual const char*  getServerID() const { return ""; }

    virtual const char*  getUserAgent() const { return "SyncEvolution"; }
    virtual const char*  getVerDTD() const { return "1.1"; }
    virtual const char*  getMan() const { return "Patrick Ohly"; }
    virtual const char*  getMod() const { return "SyncEvolution"; }
    virtual const char*  getOem() const { return "Open Source"; }
    virtual const char*  getFwv() const { return ""; }
    virtual const char*  getHwv() const { return ""; }
    virtual const char*  getDsV() const { return ""; }
    virtual const char*  getSwv() const;
    virtual const char*  getDevType() const;

    virtual bool getUtc() const { return true; }
    virtual bool getNocSupport() const { return false; }

    virtual const char*  getServerNonce() const;
    virtual void setServerNonce(const char *value);
    virtual const char*  getClientNonce() const;
    virtual void setClientNonce(const char *value);
    virtual const char*  getDevInfHash() const;
    virtual void setDevInfHash(const char *value);

    /**@}*/

private:
    string m_server;
    bool m_oldLayout;

    /** holds all config nodes relative to the root that we found */
    boost::shared_ptr<ConfigTree> m_tree;

    /** access to global sync properties */
    boost::shared_ptr<FilterConfigNode> m_configNode;
    boost::shared_ptr<ConfigNode> m_hiddenNode;

    /** temporary overrides for sync or sync source settings */
    FilterConfigNode::ConfigFilter m_sourceFilter;

    mutable ConfigStringCache m_stringCache;

    static string getHome() {
        const char *homestr = getenv("HOME");
        return homestr ? homestr : ".";
    }
    
    static string getOldRoot() {
        return getHome() + "/.sync4j/evolution";
    }

    static string getNewRoot() {
        const char *xdg_root_str = getenv("XDG_CONFIG_HOME");
        return xdg_root_str ? xdg_root_str :
            getHome() + "/.config/syncevolution";
    }
};

/**
 * This set of config nodes is to be used by EvolutionSyncSourceConfig
 * to accesss properties.
 */
struct SyncSourceNodes {
    /**
     * @param configNode    node for user-visible properties
     * @param hiddenNode    node for internal properties (may be the same as
     *                      configNode in old config layouts!)
     * @param trackingNode  node for tracking changes (always different than the
     *                      other two nodes)
     */
    SyncSourceNodes(const boost::shared_ptr<FilterConfigNode> &configNode,
                    const boost::shared_ptr<ConfigNode> &hiddenNode,
                    const boost::shared_ptr<ConfigNode> &trackingNode) : 
    m_configNode(configNode),
        m_hiddenNode(hiddenNode),
        m_trackingNode(trackingNode)
    {}

    const boost::shared_ptr<FilterConfigNode> m_configNode;
    const boost::shared_ptr<ConfigNode> m_hiddenNode;
    const boost::shared_ptr<ConfigNode> m_trackingNode;
};
    


/**
 * This class maps per-source properties to ConfigNode properties.
 * Some properties are not configurable and have to be provided
 * by derived classes.
 */
class EvolutionSyncSourceConfig : public AbstractSyncSourceConfig {
 public:
    EvolutionSyncSourceConfig(const string &name, const SyncSourceNodes &nodes);

    static ConfigPropertyRegistry &getRegistry();
    virtual boost::shared_ptr<FilterConfigNode> getProperties() { return m_nodes.m_configNode; }
    bool exists() const { return m_nodes.m_configNode->exists(); }

    /**
     * @defgroup EvolutionSyncSourceConfigExtensions
     *
     * @{
     */
    virtual const char *getUser() const;
    virtual void setUser(const string &value, bool temporarily = false);

    const char *getPassword() const;
    virtual void setPassword(const string &value, bool temporarily = false);

    virtual const char *getDatabaseID() const;
    virtual void setDatabaseID(const string &value, bool temporarily = false);
    /**@}*/

    /**
     * Returns the data source type configured as part of the given
     * configuration; different EvolutionSyncSources then check whether
     * they support that type. This call has to work before instantiating
     * a source and thus gets passed a node to read from.
     *
     * @return the pair of <backend> and the (possibly empty)
     *         <format> specified in the "type" property; see
     *         sourcePropSourceType in SyncEvolutionConfig.cpp
     *         for details
     */
    static pair<string, string> getSourceType(const SyncSourceNodes &nodes);
    static string getSourceTypeString(const SyncSourceNodes &nodes);
    virtual pair<string, string> getSourceType() const;

    /** set the source type in <backend>[:format] style */
    virtual void setSourceType(const string &value, bool temporarily = false);


    /**@}*/

    /**
     * @defgroup AbstractSyncSourceConfigAPI_not_yet_implemented
     *
     * These calls have to be implemented by EvolutionSyncSource
     * instances. Some sources support more than one type. The
     * configuration then selects the preferred format in
     * the getSourceType() string.
     * 
     * @{
     */

    /**
     * Returns the preferred mime type of the items handled by the sync source.
     * Example: "text/x-vcard"
     */
    virtual const char *getMimeType() const = 0;

    /**
     * Returns the version of the mime type used by client.
     * Example: "2.1"
     */
    virtual const char *getMimeVersion() const = 0;

    /**
     * A string representing the source types (with versions) supported by the SyncSource.
     * The string must be formatted as a sequence of "type:version" separated by commas ','.
     * For example: "text/x-vcard:2.1,text/vcard:3.0".
     * The version can be left empty, for example: "text/x-s4j-sifc:".
     * Supported types will be sent as part of the DevInf.
     */
    virtual const char* getSupportedTypes() const = 0;

    /**@}*/

    /**
     * @defgroup AbstractSyncSourceConfigAPI_implemented
     * @{
     */
    virtual const char *getType() const { return getMimeType(); }
    virtual const char *getVersion() const { return getMimeVersion(); }
    virtual const char*  getName() const { return m_name.c_str(); }
    /**@}*/

    /**
     * Returns the SyncSource URI: used in SyncML to address the data
     * on the server.
     *
     * Each URI has to be unique during a sync session, i.e.
     * two different sync sources cannot access the same data at
     * the same time.
     */
    virtual const char*  getURI() const;
    virtual void setURI(const string &value, bool temporarily = false);

    /**
     * Returns a comma separated list of the possible syncModes for the
     * SyncSource. Sync modes can be one of
     * - slow
     * - two-way
     * - one-way-from-server
     * - one-way-from-client
     * - refresh-from-server
     * - refresh-from-client
     * - one-way-from-server
     * - one-way-from-client
     * - addrchange (Funambol extension)
     *
     * This is hard-coded in SyncEvolution because changing it
     * wouldn't have any effect (IMHO).
     */
    virtual const char*  getSyncModes() const { return "slow,two-way,one-way-from-server,one-way-from-client,refresh-from-server,refresh-from-client"; }

    /**
     * Gets the default syncMode as one of the strings listed in setSyncModes.
     */
    virtual const char*  getSync() const;
    virtual void setSync(const string &value, bool temporarily = false);
    
    /**
     * Specifies how the content of an outgoing item should be
     * encoded by the client library if the sync source does not
     * set an encoding on the item that it created. Valid values
     * are listed in SyncItem::encodings.
     */
    virtual const char*  getEncoding() const;
    virtual void setEncoding(const string &value, bool temporarily = false);

    /**
     * Sets the last sync timestamp. Called by the sync engine at
     * the end of a sync. The client must save that modified
     * value; it is needed to decide during the next sync whether
     * an incremental sync is possible.
     *
     * SyncEvolution will reset this value when a SyncSource fails
     * and thus force a slow sync during the next sync.
     *
     * @param timestamp the last sync timestamp
     */
    virtual unsigned long getLast() const;
    virtual void setLast(unsigned long timestamp);

    /**
     * "des" enables an encryption mode which only the Funambol server
     * understands. Not configurable in SyncEvolution unless a derived
     * SyncSource decides otherwise.
     */
    virtual const char* getEncryption() const { return ""; }

    /**
     * Returns an array of CtCap with all the capabilities for this
     * source.  The capabilities specify which parts of e.g. a vCard
     * the sync source supports. Not specifying this in detail by
     * returning an empty array implies that it supports all aspects.
     * This is the default implementation of this call.
     *
     * @return an ArrayList of CTCap
     */
    virtual const ArrayList& getCtCaps() const { static const ArrayList dummy; return dummy; }

    /**@}*/

 private:
    string m_name;
    SyncSourceNodes m_nodes;
    mutable ConfigStringCache m_stringCache;
};

/**
 * Adds dummy implementations of the missing calls to
 * EvolutionSyncSourceConfig so that the other properties can be read.
 */
class PersistentEvolutionSyncSourceConfig : public EvolutionSyncSourceConfig {
 public:
    PersistentEvolutionSyncSourceConfig(const string &name, const SyncSourceNodes &nodes) :
    EvolutionSyncSourceConfig(name, nodes) {}

    virtual const char* getMimeType() const { return ""; }
    virtual const char* getMimeVersion() const { return ""; }
    virtual const char* getSupportedTypes() const { return ""; }
};

#endif
