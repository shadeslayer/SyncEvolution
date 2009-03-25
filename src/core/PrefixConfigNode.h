/*
 * Copyright (C) 2008-2009 Patrick Ohly <patrick.ohly@gmx.de>
 */

#ifndef INCL_EVOLUTION_PREFIX_CONFIG_NODE
# define INCL_EVOLUTION_PREFIX_CONFIG_NODE

#include <ConfigNode.h>
#include <boost/shared_ptr.hpp>

#include <map>
#include <utility>
#include <vector>
#include <string>
using namespace std;

/**
 * This class acts as filter between a real config node and its user:
 * a fixed prefix is added to each key when setting/getting a property.
 * The list of properties only includes the key/value pairs with
 * a matching prefix.
 *
 * The purpose is to have multiple users accessing the same underlying
 * node without running into namespace conflicts.
 */
class PrefixConfigNode : public ConfigNode {
 public:
    /** read-write access to underlying node */
    PrefixConfigNode(const string prefix,
                     const boost::shared_ptr<ConfigNode> &node);

    /** read-only access to underlying node */
    PrefixConfigNode(const string prefix,
                     const boost::shared_ptr<const ConfigNode> &node);

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
    string m_prefix;
    boost::shared_ptr<ConfigNode> m_node;
    boost::shared_ptr<const ConfigNode> m_readOnlyNode;
};

#endif
