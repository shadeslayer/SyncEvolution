/*
 * Copyright (C) 2008 Patrick Ohly
 */

#ifndef INCL_EVOLUTION_SAFE_CONFIG_NODE
# define INCL_EVOLUTION_SAFE_CONFIG_NODE

#include <ConfigNode.h>
#include <boost/shared_ptr.hpp>

#include <map>
#include <utility>
#include <vector>
#include <string>
using namespace std;

/**
 * This class acts as filter between a real config node and its user:
 * key/value strings which normally wouldn't be valid are escaped
 * before passing them into the underlying node. When reading, they
 * are unescaped again.
 *
 * Unsafe characters are replaced by ! followed by two characters
 * giving the character value in hex notation.
 */
class SafeConfigNode : public ConfigNode {
 public:
    /** read-write access to underlying node */
    SafeConfigNode(const boost::shared_ptr<ConfigNode> &node);

    /** read-only access to underlying node */
    SafeConfigNode(const boost::shared_ptr<const ConfigNode> &node);

    virtual string getName() const { return m_readOnlyNode->getName(); }

    /* ConfigNode API */
    virtual void flush();
    virtual string readProperty(const string &property) const;
    virtual void setProperty(const string &property,
                             const string &value,
                             const string &comment = "",
                             const string *defValue = NULL);
    virtual void readProperties(map<string, string> &props) const;
    virtual void removeProperty(const string &property);
    virtual bool exists() const { return m_readOnlyNode->exists(); }

 private:
    boost::shared_ptr<ConfigNode> m_node;
    boost::shared_ptr<const ConfigNode> m_readOnlyNode;

    /**
     * turn str into something which can be used as key or value in ConfigNode
     */
    static string escape(const string &str);

    /** inverse operation for escape() */
    static string unescape(const string &str);
};

#endif
