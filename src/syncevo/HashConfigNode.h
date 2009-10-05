/*
 * Copyright (C) 2008-2009 Patrick Ohly <patrick.ohly@gmx.de>
 */

#ifndef INCL_EVOLUTION_HASH_CONFIG_NODE
# define INCL_EVOLUTION_HASH_CONFIG_NODE

#include <syncevo/ConfigNode.h>

#include <string>
#include <map>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

/**
 * Implements a ConfigNode with an in-memory hash table.
 */
class HashConfigNode : public ConfigNode {
    std::map<std::string, std::string> m_props;
    const std::string m_name;

 public:
    /**
     * @param name     a string for debugging and error reporting
     */
    HashConfigNode(const std::string &name = "hash config node") : m_name(name) {}

    /* keep underlying methods visible; our own setProperty() would hide them */
    using ConfigNode::setProperty;

    virtual string getName() const { return m_name; }

    virtual void flush() {}
    virtual string readProperty(const string &property) const {
        std::map<std::string, std::string>::const_iterator it = m_props.find(property);
        if (it == m_props.end()) {
            return "";
        } else {
            return it->second;
        }
    }
    virtual void setProperty(const string &property,
                             const string &value,
                             const string &comment = "",
                             const string *defValue = NULL) { m_props[property] = value; }
    virtual void readProperties(std::map<std::string, std::string> &props) const { props = m_props; }
    virtual void removeProperty(const std::string &property) { m_props.erase(property); }
    virtual bool exists() const { return true; }
};


SE_END_CXX
#endif
