/*
 * Copyright (C) 2008-2009 Patrick Ohly <patrick.ohly@gmx.de>
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

#ifndef INCL_EVOLUTION_SAFE_CONFIG_NODE
# define INCL_EVOLUTION_SAFE_CONFIG_NODE

#include <syncevo/ConfigNode.h>
#include <boost/shared_ptr.hpp>

#include <map>
#include <utility>
#include <vector>
#include <string>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

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

    /**
     * chooses which characters are accepted by underlying node:
     * in strict mode, only alphanumeric and -_ are supported;
     * in non-strict mode, only line breaks, = and spaces at start and end are escaped
     */
    void setMode(bool strict) { m_strictMode = strict; }
    bool getMode() { return m_strictMode; }

    virtual std::string getName() const { return m_readOnlyNode->getName(); }

    /* keep underlying methods visible; our own setProperty() would hide them */
    using ConfigNode::setProperty;

    /* ConfigNode API */
    virtual void flush();
    virtual InitStateString readProperty(const std::string &property) const;
    virtual void writeProperty(const std::string &property,
                               const InitStateString &value,
                               const std::string &comment = "");
    virtual void readProperties(ConfigProps &props) const;
    virtual void removeProperty(const std::string &property);
    virtual bool exists() const { return m_readOnlyNode->exists(); }
    virtual bool isReadOnly() const { return !m_node || m_readOnlyNode->isReadOnly(); }
    virtual void clear() { m_node->clear(); }

 private:
    boost::shared_ptr<ConfigNode> m_node;
    boost::shared_ptr<const ConfigNode> m_readOnlyNode;
    bool m_strictMode;

    /**
     * turn str into something which can be used as key or value in ConfigNode
     */
    std::string escape(const std::string &str) const {
        return StringEscape::escape(str, '!', m_strictMode ? StringEscape::STRICT : StringEscape::INI_VALUE);
    }

    static std::string unescape(const std::string &str) {
        return StringEscape::unescape(str, '!');
    }
};


SE_END_CXX
#endif
