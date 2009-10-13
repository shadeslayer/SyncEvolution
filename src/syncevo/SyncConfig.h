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
#ifndef INCL_SYNC_EVOLUTION_CONFIG
# define INCL_SYNC_EVOLUTION_CONFIG

#include <syncevo/FilterConfigNode.h>

#include <boost/shared_ptr.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/foreach.hpp>
#include <list>
#include <string>
#include <sstream>
#include <set>

#include <stdlib.h>
#include <limits.h>
#include <errno.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX
using namespace std;

/**
 * @defgroup ConfigHandling Configuration Handling
 * @{
 */

class SyncSourceConfig;
class PersistentSyncSourceConfig;
class ConfigTree;
class ConfigUserInterface;
struct SyncSourceNodes;
struct ConstSyncSourceNodes;

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
    ConfigProperty(const string &name, const string &comment,
                   const string &def = string(""), const string &descr = string("")) :
        m_obligatory(false),
        m_hidden(false),
        m_name(name),
        m_comment(boost::trim_right_copy(comment)),
            m_defValue(def),
        m_descr(descr)
        {}
    virtual ~ConfigProperty() {}
    
    virtual string getName() const { return m_name; }
    virtual string getComment() const { return m_comment; }
    virtual string getDefValue() const { return m_defValue; }
    virtual string getDescr() const { return m_descr; }

    /**
     * Check whether the given value is okay.
     * If not, then set an error string (one line, no punctuation).
     *
     * @return true if value is okay
     */
    virtual bool checkValue(const string &value, string &error) const { return true; }

    /**
     * Only useful when a config property wants to check itself whether to retrieve password
     * Check the password and cache the result in the filter node on the fly if a property needs.
     * sourceName and sourceConfigNode might be not set by caller. They only affect
     * when checking password for syncsourceconfig 
     * @param ui user interface
     * @param serverName server name
     * @param globalConfigNode the sync global config node for a server
     * @param sourceName the source name used for source config properties
     * @param sourceConfigNode the config node for the source
     */
    virtual void checkPassword(ConfigUserInterface &ui,
                               const string &serverName,
                               FilterConfigNode &globalConfigNode,
                               const string &sourceName = string(),
                               const boost::shared_ptr<FilterConfigNode> &sourceConfigNode = boost::shared_ptr<FilterConfigNode>()) const {}

    /**
     * Try to save password if a config property wants.
     * It firstly check password and then invoke ui's savePassword
     * function to save the password if necessary
     */
    virtual void savePassword(ConfigUserInterface &ui,
                              const string &serverName,
                              FilterConfigNode &globalConfigNode,
                              const string &sourceName = string(),
                              const boost::shared_ptr<FilterConfigNode> &sourceConfigNode = boost::shared_ptr<FilterConfigNode>()) const {}

    /**
     * This is used to generate description dynamically according to the context information
     * Defalut implmenentation is to return value set in the constructor.
     * Derived classes can override this function. Used by 'checkPassword' and 'savePassword'
     * to generate description for user interface.
     */
    virtual const string getDescr(const string &serverName,
                                  FilterConfigNode &globalConfigNode,
                                  const string &sourceName = string(),
                                  const boost::shared_ptr<FilterConfigNode> &sourceConfigNode=boost::shared_ptr<FilterConfigNode>()) const { return m_descr; }


    /** split \n separated comment into lines without \n, appending them to commentLines */
    static void splitComment(const string &comment, list<string> &commentLines);

    bool isHidden() const { return m_hidden; }
    void setHidden(bool hidden) { m_hidden = hidden; }

    bool isObligatory() const { return m_obligatory; }
    void setObligatory(bool obligatory) { m_obligatory = obligatory; }

    /** set value unconditionally, even if it is not valid */
    void setProperty(ConfigNode &node, const string &value) const { node.setProperty(getName(), value, getComment()); }
    void setProperty(FilterConfigNode &node, const string &value, bool temporarily = false) const {
        if (temporarily) {
            node.addFilter(m_name, value);
        } else {
            node.setProperty(m_name, value, getComment());
        }
    }

    /** set default value of a property, marked as default unless forced setting */
    void setDefaultProperty(ConfigNode &node, bool force) const {
        string defValue = getDefValue();
        node.setProperty(m_name, defValue, getComment(), force ? NULL : &defValue);
    }

    /**
     * @retval isDefault    return true if the node had no value set and
     *                      the default was returned instead
     */
    virtual string getProperty(const ConfigNode &node, bool *isDefault = NULL) const {
        string name = getName();
        string value = node.readProperty(name);
        if (!value.empty()) {
            string error;
            if (!checkValue(value, error)) {
                throwValueError(node, name, value, error);
            }
            if (isDefault) {
                *isDefault = false;
            }
            return value;
        } else {
            if (isDefault) {
                *isDefault = true;
            }
            return getDefValue();
        }
    }

    // true if property is set to non-empty value
    virtual bool isSet(const ConfigNode &node) const {
        string name = getName();
        string value = node.readProperty(name);
        return !value.empty();
    }

 protected:
    void throwValueError(const ConfigNode &node, const string &name, const string &value, const string &error) const;

 private:
    bool m_obligatory;
    bool m_hidden;
    const string m_name, m_comment, m_defValue, m_descr;
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
                         const string &descr = string(""),
                         const Values &values = Values()) :
    ConfigProperty(name, comment, def, descr),
        m_values(values)
        {}

    /**
     * @return false if aliases are defined and the string is not one of them
     */
    bool normalizeValue(string &res) const {
        Values values = getValues();
        BOOST_FOREACH(const Values::value_type &value, values) {
            BOOST_FOREACH(const string &alias, value) {
                if (boost::iequals(res, alias)) {
                    res = *value.begin();
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
        bool firstval = true;
        BOOST_FOREACH(const Values::value_type &value, values) {
            if (!firstval) {
                err << ", ";
            } else {
                firstval = false;
            }
            bool firstalias = true;
            BOOST_FOREACH(const string &alias, value) {
                if (!firstalias) {
                    err << " = ";
                } else {
                    firstalias = false;
                }
                if (alias.empty()) {
                    err << "\"\"";
                } else {
                    err << alias;
                }
                
                if (boost::iequals(propValue, alias)) {
                    return true;
                }
            }
        }
        err << ")";
        error = err.str();
        return false;
    }

    virtual string getProperty(const ConfigNode &node, bool *isDefault = NULL) const {
        string res = ConfigProperty::getProperty(node, isDefault);
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
    TypedConfigProperty(const string &name, const string &comment, const string &defValue = string("0"), const string &descr = string("")) :
    ConfigProperty(name, comment, defValue, descr)
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
            return false;
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

    T getProperty(ConfigNode &node, bool *isDefault = NULL) {
        string name = getName();
        string value = node.readProperty(name);
        istringstream in(value);
        T res;
        if (value.empty()) {
            istringstream defStream(getDefValue());
            defStream >> res;
            if (isDefault) {
                *isDefault = true;
            }
            return res;
        } else {
            if (!(in >> res)) {
                throwValueError(node, name, value, "cannot parse value");
            }
            if (isDefault) {
                *isDefault = false;
            }
            return res;
        }
    }
};

/**
 * The "in >> res" check in TypedConfigProperty::checkValue
 * is to loose. For example, the standard library accepts
 * -1 for an unsigned type.
 *
 * This class accepts a function pointer to strtoul() or
 * strtol() and uses that function to do strict value checking.
 *
 * @param T       type to be converted to and from (like int)
 * @param Tmin    minimum value of T for range checking
 * @param Tmax    maximum value of T for range checking
 * @param C       intermediate type for conversion  (like long)
 * @param Cmin    minimum value of C for range checking
 * @param Cmax    maximum value of C for range checking
 * @param strto   conversion function
 */
template <class T, T Tmin, T Tmax,
    class C, C Cmin, C Cmax,
    C (*strto)(const char *, char **, int)>
class ScalarConfigProperty : public TypedConfigProperty<T>
{
 public:
    ScalarConfigProperty(const string &name, const string &comment, const string &defValue = string("0"), const string &descr = string("")) :
    TypedConfigProperty<T>(name, comment, defValue, descr)
        {}

    virtual bool checkValue(const string &value, string &error) const {
        errno = 0;
        const char *nptr = value.c_str();
        char *endptr;
        // use base 10 because not specifying a base 
        // would interpret 077 as octal value, which
        // could be confusing for users
        C val = strto(nptr, &endptr, 10);
        if ((errno == ERANGE && (val == Cmin || val == Cmax))) {
            error = "range error";
            return false;
        }
        if (errno != 0 && val == 0) {
            error = "cannot parse";
            return false;
        }
        if (endptr == nptr) {
            error = "decimal value expected";
            return false;
        }
        while (isspace(*endptr)) {
            endptr++;
        }
        if (*endptr != '\0') {
            error = "unexpected trailing non-whitespace characters: ";
            error += endptr;
            return false;
        }
        if (val > Tmax || val < Tmin) {
            error = "range error";
            return false;
        }
        if (Tmin == 0) {
            // check that we didn't accidentally accept a negative value,
            // strtoul() does that
            const char *start = nptr;
            while (*start && isspace(*start)) {
                start++;
            }
            if (*start == '-') {
                error = "range error";
                return false;
            }
        }

        return true;
    }
};

typedef ScalarConfigProperty<int, INT_MIN, INT_MAX, long, LONG_MIN, LONG_MAX, strtol> IntConfigProperty;
typedef ScalarConfigProperty<unsigned int, 0, UINT_MAX, unsigned long, 0, ULONG_MAX, strtoul> UIntConfigProperty;
typedef ScalarConfigProperty<long, LONG_MIN, LONG_MAX, long, LONG_MIN, LONG_MAX, strtol> LongConfigProperty;
typedef ScalarConfigProperty<unsigned long, 0, ULONG_MAX, unsigned long, 0, ULONG_MAX, strtoul> ULongConfigProperty;

/**
 * This struct wraps keys for storing passwords
 * in configuration system. Some fields might be empty
 * for some passwords. Each field might have different 
 * meaning for each password. Fields using depends on
 * what user actually wants.
 */
struct ConfigPasswordKey {
 public:
    ConfigPasswordKey() : port(0) {}

    /** the user for the password */
    string user;
    /** the server for the password */
    string server;
    /** the domain name */
    string domain;
    /** the remote object */
    string object;
    /** the network protocol */
    string protocol;
    /** the authentication type */
    string authtype;
    /** the network port */
    unsigned int port;
};
/**
 * This interface has to be provided by the user of the config
 * to let the config code interact with the user.
 */
class ConfigUserInterface {
 public:
    virtual ~ConfigUserInterface() {}

    /**
     * A helper function which interactively asks the user for
     * a certain password. May throw errors.
     *
     * @param passwordName the name of the password in the config file, such as 'proxyPassword'
     * @param descr        A simple string explaining what the password is needed for,
     *                     e.g. "SyncML server". This string alone has to be enough
     *                     for the user to know what the password is for, i.e. the
     *                     string has to be unique.
     * @param key          the key used to retrieve password. Using this instead of ConfigNode is
     *                     to make user interface independent on Configuration Tree
     * @return entered password
     */
    virtual string askPassword(const string &passwordName, const string &descr, const ConfigPasswordKey &key) = 0;

    /**
     * A helper function which is used for user interface to save
     * a certain password. Currently possibly syncml server. May
     * throw errors.
     * @param passwordName the name of the password in the config file, such as 'proxyPassword'
     * @param password     password to be saved
     * @param key          the key used to store password
     * @return true if ui saves the password and false if not
     */
    virtual bool savePassword(const string &passwordName, const string &password, const ConfigPasswordKey &key) = 0;
};

class PasswordConfigProperty : public ConfigProperty {
 public:
    PasswordConfigProperty(const string &name, const string &comment, const string &def = string(""),const string &descr = string("")) :
       ConfigProperty(name, comment, def, descr)
           {}

    /**
     * Check the password and cache the result.
     */
    virtual void checkPassword(ConfigUserInterface &ui,
                               const string &serverName,
                               FilterConfigNode &globalConfigNode,
                               const string &sourceName,
                               const boost::shared_ptr<FilterConfigNode> &sourceConfigNode) const;

    /**
     * It firstly check password and then invoke ui's savePassword
     * function to save the password if necessary
     */
    virtual void savePassword(ConfigUserInterface &ui,
                              const string &serverName,
                              FilterConfigNode &globalConfigNode,
                              const string &sourceName,
                              const boost::shared_ptr<FilterConfigNode> &sourceConfigNode) const;

    /**
     * Get password key for storing or retrieving passwords
     * The default implemention is for 'password' in global config.
     * @param descr decription for password
     * @param globalConfigNode the global config node 
     * @param sourceConfigNode the source config node. It might be empty
     */
    virtual ConfigPasswordKey getPasswordKey(const string &descr,
                                             const string &serverName,
                                             FilterConfigNode &globalConfigNode,
                                             const string &sourceName = string(),
                                             const boost::shared_ptr<FilterConfigNode> &sourceConfigNode=boost::shared_ptr<FilterConfigNode>()) const; 

    /**
     * return the cached value if necessary and possible
     */
    virtual string getCachedProperty(ConfigNode &node,
                                     const string &cachedPassword);
};

/**
 * A derived ConfigProperty class for the property "proxyPassword"
 */
class ProxyPasswordConfigProperty : public PasswordConfigProperty {
 public:
    ProxyPasswordConfigProperty(const string &name, const string &comment, const string &def = string(""), const string &descr = string("")) :
        PasswordConfigProperty(name,comment,def,descr)
    {}
    /**
     * re-implement this function for it is necessary to do a check 
     * before retrieving proxy password
     */
    virtual void checkPassword(ConfigUserInterface &ui,
                               const string &serverName,
                               FilterConfigNode &globalConfigNode,
                               const string &sourceName,
                               const boost::shared_ptr<FilterConfigNode> &sourceConfigNode) const;
    virtual ConfigPasswordKey getPasswordKey(const string &descr,
                                             const string &serverName,
                                             FilterConfigNode &globalConfigNode,
                                             const string &sourceName = string(),
                                             const boost::shared_ptr<FilterConfigNode> &sourceConfigNode=boost::shared_ptr<FilterConfigNode>()) const; 
};

/**
 * A derived ConfigProperty class for the property "evolutionpassword"
 */
class EvolutionPasswordConfigProperty : public PasswordConfigProperty {
 public:
    EvolutionPasswordConfigProperty(const string &name, 
                                    const string &comment, 
                                    const string &def = string(""),
                                    const string &descr = string("")): 
                                    PasswordConfigProperty(name,comment,def,descr)
    {}
    virtual ConfigPasswordKey getPasswordKey(const string &descr,
                                             const string &serverName,
                                             FilterConfigNode &globalConfigNode,
                                             const string &sourceName = string(),
                                             const boost::shared_ptr<FilterConfigNode> &sourceConfigNode=boost::shared_ptr<FilterConfigNode>()) const; 
    virtual const string getDescr(const string &serverName,
                                  FilterConfigNode &globalConfigNode,
                                  const string &sourceName,
                                  const boost::shared_ptr<FilterConfigNode> &sourceConfigNode) const {
        string descr = sourceName;
        descr += " ";
        descr += ConfigProperty::getDescr();
        return descr;
    }
};

/**
 * Instead of reading and writing strings, this class interprets the content
 * as boolean with T/F or 1/0 (default format).
 */
class BoolConfigProperty : public StringConfigProperty {
 public:
    BoolConfigProperty(const string &name, const string &comment, const string &defValue = string("F"),const string &descr = string("")) :
    StringConfigProperty(name, comment, defValue,descr,
                         Values() + (Aliases("1") + "T" + "TRUE") + (Aliases("0") + "F" + "FALSE"))
        {}

    void setProperty(ConfigNode &node, bool value) {
        StringConfigProperty::setProperty(node, value ? "1" : "0");
    }
    void setProperty(FilterConfigNode &node, bool value, bool temporarily = false) {
        StringConfigProperty::setProperty(node, value ? "1" : "0", temporarily);
    }
    int getProperty(ConfigNode &node, bool *isDefault = NULL) {
        string res = ConfigProperty::getProperty(node, isDefault);

        return boost::iequals(res, "T") ||
            boost::iequals(res, "TRUE") ||
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
        BOOST_FOREACH(const ConfigProperty *prop, *this) {
            if (boost::iequals(prop->getName(), propName)) {
                return prop;
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
        return storeString(prop.getName(), value);
    }

    const char *storeString(const string &key, const string &value) {
        const string &entry = m_cache[key] = value;
        return entry.c_str();
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
 * to properties actually stored in files. SyncContext
 * inherits from this class so that a derived client has the chance to
 * override every single property (although it doesn't have to).
 * Likewise SyncSource is derived from
 * SyncSourceConfig.
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
class SyncConfig {
 public:
    /**
     * Opens the configuration for a specific server,
     * searching for the config files in the usual
     * places. Will succeed even if config does not
     * yet exist: flushing such a config creates it.
     *
     * @param tree   if non-NULL, then this is used
     *               as configuration tree instead of
     *               searching for it; always uses the
     *               current layout in that tree
     */
    SyncConfig(const string &server,
                        boost::shared_ptr<ConfigTree> tree = boost::shared_ptr<ConfigTree>());

    /**
     * Creates a temporary configuration.
     * Can be copied around, but not flushed.
     */
    SyncConfig();

   /** absolute directory name of the configuration root */
    string getRootPath() const;

    typedef list< std::pair<std::string, std::string> > ServerList;

    /**
     * returns list of servers in either the old (.sync4j) or
     * new config directory (.config), given as server name
     * and absolute root of config
     */
    static ServerList getServers();

    /**
     * returns list of available config templates
     */
    static ServerList getServerTemplates();

    /**
     * Creates a new instance of a configuration template.
     * The result can be modified to set filters, but it
     * cannot be flushed.
     *
     * @return NULL if no such template
     */
    static boost::shared_ptr<SyncConfig> createServerTemplate(const string &server);

    /** true if the main configuration file already exists */
    bool exists() const;

    /**
     * Do something before doing flush to files. This is particularly
     * useful when user interface wants to do preparation jobs, such
     * as savePassword and others.
     */
    virtual void preFlush(ConfigUserInterface &ui); 

    void flush();

    /**
     * Remove the configuration. The config object itself is still
     * valid afterwards, but empty and cannot be flushed.
     *
     * Does *not* remove logs associated with the configuration.
     * For that use the logdir handling in SyncContext
     * before removing the configuration.
     *
     * The config directory is removed if it is empty.
     */
    void remove();

    /**
     * A list of all properties. Can be extended by derived clients.
     */
    static ConfigPropertyRegistry &getRegistry();

    /**
     * Replaces the property filter of either the sync properties or
     * all sources. This can be used to e.g. temporarily override
     * the active sync mode.
     */
    virtual void setConfigFilter(bool sync, const FilterConfigNode::ConfigFilter &filter) {
        if (sync) {
            m_configNode->setFilter(filter);
        } else {
            m_sourceFilter = filter;
        }
    }

    

    /**
     * Read-write access to all configurable properties of the server.
     * The visible properties are passed through the config filter,
     * which can be modified.
     */
    virtual boost::shared_ptr<FilterConfigNode> getProperties(bool hidden = false) {
        if (hidden) {
            return boost::shared_ptr<FilterConfigNode>(new FilterConfigNode(m_hiddenNode));
        } else {
            return m_configNode;
        }
    }
    virtual boost::shared_ptr<const FilterConfigNode> getProperties(bool hidden = false) const { return const_cast<SyncConfig *>(this)->getProperties(hidden); }


    /**
     * Returns a wrapper around all properties of the given source
     * which are saved in the config tree. Note that this is different
     * from the set of sync source configs used by the SyncManager:
     * the SyncManger uses the AbstractSyncSourceConfig. In
     * SyncEvolution those are implemented by the
     * SyncSource's actually instantiated by
     * SyncContext. Those are complete whereas
     * PersistentSyncSourceConfig only provides access to a
     * subset of the properties.
     *
     * Can be called for sources which do not exist yet.
     */
    virtual boost::shared_ptr<PersistentSyncSourceConfig> getSyncSourceConfig(const string &name);
    virtual boost::shared_ptr<const PersistentSyncSourceConfig> getSyncSourceConfig(const string &name) const {
        return const_cast<SyncConfig *>(this)->getSyncSourceConfig(name);
    }

    /**
     * Returns list of all configured (not active!) sync sources.
     */
    virtual list<string> getSyncSources() const;

    /**
     * Creates config nodes for a certain node. The nodes are not
     * yet created in the backend if they do not yet exist.
     *
     * @param name       the name of the sync source
     * @param trackName  additional part of the tracking node name (used for unit testing)
     */
    SyncSourceNodes getSyncSourceNodes(const string &name,
                                       const string &trackName = "");
    ConstSyncSourceNodes getSyncSourceNodes(const string &name,
                                            const string &trackName = "") const;

    /**
     * initialize all properties with their default value
     */
    void setDefaults(bool force = true);

    /**
     * create a new sync source configuration with default values
     */
    void setSourceDefaults(const string &name, bool force = true);

    /**
     * Copy all registered properties (hidden and visible) and the
     * tracking node into the current config. This is done by reading
     * all properties from the source config, which implies the unset
     * properties will be set to their default values.  The current
     * config is not cleared so additional, unregistered properties
     * (should they exist) will continue to exist unchanged.
     *
     * The current config still needs to be flushed to make the
     * changes permanent.
     *
     * @param sourceFilter   if NULL, then copy all sources; if not NULL,
     *                       then only copy sources listed here
     */
    void copy(const SyncConfig &other,
              const set<string> *sourceFilter);

    /**
     * @name Settings specific to SyncEvolution
     *
     * See the property definitions in SyncConfig.cpp
     * for the user-visible explanations of
     * these settings.
     */
    /**@{*/

    virtual const char *getLogDir() const;
    virtual void setLogDir(const string &value, bool temporarily = false);

    virtual int getMaxLogDirs() const;
    virtual void setMaxLogDirs(int value, bool temporarily = false);

    virtual int getLogLevel() const;
    virtual void setLogLevel(int value, bool temporarily = false);

    virtual bool getPrintChanges() const;
    virtual void setPrintChanges(bool value, bool temporarily = false);

    virtual std::string getWebURL() const;
    virtual void setWebURL(const std::string &url, bool temporarily = false);

    virtual std::string getIconURI() const;
    virtual void setIconURI(const std::string &uri, bool temporarily = false);

    /**
     * A property of server template configs. True if the server is
     * ready for use by "normal" users (everyone can get an account
     * and some kind of support, we have tested the server well
     * enough, ...).
     */
    virtual bool getConsumerReady() const;
    virtual void setConsumerReady(bool ready);

    virtual unsigned long getHashCode() const;
    virtual void setHashCode(unsigned long hashCode);

    virtual std::string getConfigDate() const;
    virtual void setConfigDate(); /* set current time always */

    /**@}*/

    /**
     * @name Settings inherited from Funambol
     *
     * These settings are required by the Funambol C++ client library.
     * Some of them are hard-coded in this class. A derived class could
     * make them configurable again, should that be desired.
     */
    /**@{*/

    virtual const char*  getUsername() const;
    virtual void setUsername(const string &value, bool temporarily = false);
    virtual const char*  getPassword() const;
    virtual void setPassword(const string &value, bool temporarily = false);

    /**
     * Look at the password setting and if it requires user interaction,
     * get it from the user. Then store it for later usage in getPassword().
     * Without this call, getPassword() returns the original, unmodified
     * config string.
     */
    virtual void checkPassword(ConfigUserInterface &ui);

    /**
     * Look at the password setting and if it needs special mechanism to
     * save password, this function is used to store specified password
     * in the config tree.
     * @param ui the ui pointer
     */
    virtual void savePassword(ConfigUserInterface &ui); 

    virtual bool getUseProxy() const;
    virtual void setUseProxy(bool value, bool temporarily = false);
    virtual const char*  getProxyHost() const;
    virtual void setProxyHost(const string &value, bool temporarily = false);
    virtual int getProxyPort() const { return 0; }
    virtual const char* getProxyUsername() const;
    virtual void setProxyUsername(const string &value, bool temporarily = false);
    virtual const char* getProxyPassword() const;
    virtual void checkProxyPassword(ConfigUserInterface &ui);
    virtual void saveProxyPassword(ConfigUserInterface &ui);
    virtual void setProxyPassword(const string &value, bool temporarily = false);
    virtual const char*  getSyncURL() const;
    virtual void setSyncURL(const string &value, bool temporarily = false);
    virtual const char*  getClientAuthType() const;
    virtual void setClientAuthType(const string &value, bool temporarily = false);
    virtual unsigned long getMaxMsgSize() const;
    virtual void setMaxMsgSize(unsigned long value, bool temporarily = false);
    virtual unsigned int getMaxObjSize() const;
    virtual void setMaxObjSize(unsigned int value, bool temporarily = false);
    virtual unsigned long getReadBufferSize() const { return 0; }
    virtual const char* getSSLServerCertificates() const;
    virtual void setSSLServerCertificates(const string &value, bool temporarily = false);
    virtual bool getSSLVerifyServer() const;
    virtual void setSSLVerifyServer(bool value, bool temporarily = false);
    virtual bool getSSLVerifyHost() const;
    virtual void setSSLVerifyHost(bool value, bool temporarily = false);
    virtual int getRetryInterval() const;
    virtual void setRetryInterval(int value, bool temporarily = false);
    virtual int getRetryDuration() const;
    virtual void setRetryDuration(int value, bool temporarily = false);
    virtual bool  getCompression() const;
    virtual void setCompression(bool value, bool temporarily = false);
    virtual unsigned int getResponseTimeout() const { return 0; }
    virtual const char*  getDevID() const;
    virtual void setDevID(const string &value, bool temporarily = false);

    /**
     * Specifies whether WBXML is to be used (default).
     * Otherwise XML is used.
     */
    virtual bool getWBXML() const;
    virtual void setWBXML(bool isWBXML, bool temporarily = false);

    virtual const char*  getUserAgent() const { return "SyncEvolution"; }
    virtual const char*  getMan() const { return "Patrick Ohly"; }
    virtual const char*  getMod() const { return "SyncEvolution"; }
    virtual const char*  getOem() const { return "Open Source"; }
    virtual const char*  getHwv() const { return "unknown"; }
    virtual const char*  getSwv() const;
    virtual const char*  getDevType() const;

    FilterConfigNode& getConfigNode() { return *m_configNode; }

    /**@}*/

private:
    string m_server;
    bool m_oldLayout;
    string m_cachedPassword;
    string m_cachedProxyPassword;

    /** holds all config nodes relative to the root that we found */
    boost::shared_ptr<ConfigTree> m_tree;

    /** access to global sync properties */
    boost::shared_ptr<FilterConfigNode> m_configNode;
    boost::shared_ptr<ConfigNode> m_hiddenNode;

    /** temporary overrides for sync or sync source settings */
    FilterConfigNode::ConfigFilter m_sourceFilter;

    mutable ConfigStringCache m_stringCache;

    static string getOldRoot() {
        return getHome() + "/.sync4j/evolution";
    }

    static string getNewRoot() {
        const char *xdg_root_str = getenv("XDG_CONFIG_HOME");
        return xdg_root_str ? string(xdg_root_str) + "/syncevolution" :
            getHome() + "/.config/syncevolution";
    }
};

/**
 * This set of config nodes is to be used by SyncSourceConfig
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

struct ConstSyncSourceNodes {
    ConstSyncSourceNodes(const boost::shared_ptr<const FilterConfigNode> &configNode,
                         const boost::shared_ptr<const ConfigNode> &hiddenNode,
                         const boost::shared_ptr<const ConfigNode> &trackingNode) : 
    m_configNode(configNode),
        m_hiddenNode(hiddenNode),
        m_trackingNode(trackingNode)
    {}

    ConstSyncSourceNodes(const SyncSourceNodes &other) :
    m_configNode(other.m_configNode),
        m_hiddenNode(other.m_hiddenNode),
        m_trackingNode(other.m_trackingNode)
    {}

    const boost::shared_ptr<const FilterConfigNode> m_configNode;
    const boost::shared_ptr<const ConfigNode> m_hiddenNode;
    const boost::shared_ptr<const ConfigNode> m_trackingNode;
};

struct SourceType {
    SourceType():m_forceFormat(false)
    {}
    string m_backend; /**< identifies the SyncEvolution backend (either via a generic term like "addressbook" or a specific one like "Evolution Contacts") */
    string m_format; /**< the format to be used (typically a MIME type) */
    bool   m_forceFormat; /**< force to use the client's preferred format instead giving the engine and server a choice */
};

/**
 * This class maps per-source properties to ConfigNode properties.
 * Some properties are not configurable and have to be provided
 * by derived classes.
 */
class SyncSourceConfig {
 public:
    SyncSourceConfig(const string &name, const SyncSourceNodes &nodes);

    static ConfigPropertyRegistry &getRegistry();

    /** sync mode for sync sources */
    static StringConfigProperty m_sourcePropSync;

    bool exists() const { return m_nodes.m_configNode->exists(); }

    /** checks if a certain property is set to a non-empty value */
    bool isSet(ConfigProperty &prop) {
        return prop.isHidden() ?
            prop.isSet(*m_nodes.m_hiddenNode) :
            prop.isSet(*m_nodes.m_configNode);
    }

    /**
     * @name Settings specific to SyncEvolution SyncSources
     */
    /**@{*/

    virtual const char *getUser() const;
    virtual void setUser(const string &value, bool temporarily = false);

    const char *getPassword() const;
    virtual void setPassword(const string &value, bool temporarily = false);

    /** same as SyncConfig::checkPassword() but with
     * an extra argument globalConfigNode for source config property
     * may need global config node to check password */
    virtual void checkPassword(ConfigUserInterface &ui, const string &serverName, FilterConfigNode& globalConfigNode);

    /** same as SyncConfig::savePassword() */
    virtual void savePassword(ConfigUserInterface &ui, const string &serverName, FilterConfigNode& globalConfigNode);

    virtual const char *getDatabaseID() const;
    virtual void setDatabaseID(const string &value, bool temporarily = false);

    /**
     * Returns the data source type configured as part of the given
     * configuration; different SyncSources then check whether
     * they support that type. This call has to work before instantiating
     * a source and thus gets passed a node to read from.
     *
     * @return the pair of <backend> and the (possibly empty)
     *         <format> specified in the "type" property; see
     *         sourcePropSourceType in SyncConfig.cpp
     *         for details
     */
    static SourceType getSourceType(const SyncSourceNodes &nodes);
    static string getSourceTypeString(const SyncSourceNodes &nodes);
    virtual SourceType getSourceType() const;

    /** set the source type in <backend>[:format] style */
    virtual void setSourceType(const string &value, bool temporarily = false);


    /**@}*/

    /**
     * @name Calls which usually do not have to be implemented by each SyncSource.
     */
    /**@{*/

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
     * Gets the default syncMode.
     *
     * Sync modes can be one of:
     * - disabled
     * - slow
     * - two-way
     * - one-way-from-server
     * - one-way-from-client
     * - refresh-from-server
     * - refresh-from-client
     */
    virtual const char*  getSync() const;
    virtual void setSync(const string &value, bool temporarily = false);
    
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
     * @TODO: per-source capabilities
     */
    // virtual const ArrayList& getCtCaps() const { static const ArrayList dummy; return dummy; }

    /**@}*/

    /**
     * @name Calls implemented by SyncEvolution.
     */
    /**@{*/
    virtual const char*  getName() const { return m_name.c_str(); }

    virtual SyncSourceNodes& getSyncSourceNodes() { return m_nodes; }
    /**@}*/

 private:
    string m_name;
    SyncSourceNodes m_nodes;
    mutable ConfigStringCache m_stringCache;
    string m_cachedPassword;
};

/**
 * Adds dummy implementations of the missing calls to
 * SyncSourceConfig so that the other properties can be read.
 */
class PersistentSyncSourceConfig : public SyncSourceConfig {
 public:
    PersistentSyncSourceConfig(const string &name, const SyncSourceNodes &nodes) :
    SyncSourceConfig(name, nodes) {}

    virtual const char* getMimeType() const { return ""; }
    virtual const char* getMimeVersion() const { return ""; }
    virtual const char* getSupportedTypes() const { return ""; }
};

/**@}*/


SE_END_CXX
#endif
