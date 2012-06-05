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

#ifndef INCL_EVOLUTION_PREFIX_CONFIG_NODE
# define INCL_EVOLUTION_PREFIX_CONFIG_NODE

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
    PrefixConfigNode(const std::string prefix,
                     const boost::shared_ptr<ConfigNode> &node);

    /** read-only access to underlying node */
    PrefixConfigNode(const std::string prefix,
                     const boost::shared_ptr<const ConfigNode> &node);

    virtual std::string getName() const { return m_readOnlyNode->getName(); }

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
    virtual void clear();

 private:
    std::string m_prefix;
    boost::shared_ptr<ConfigNode> m_node;
    boost::shared_ptr<const ConfigNode> m_readOnlyNode;
};


SE_END_CXX
#endif
