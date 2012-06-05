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

#include <syncevo/SafeConfigNode.h>
#include <syncevo/SyncContext.h>

#include <boost/foreach.hpp>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

SafeConfigNode::SafeConfigNode(const boost::shared_ptr<ConfigNode> &node) :
    m_node(node),
    m_readOnlyNode(node),
    m_strictMode(true)
{
}

SafeConfigNode::SafeConfigNode(const boost::shared_ptr<const ConfigNode> &node) :
    m_readOnlyNode(node),
    m_strictMode(true)
{
}

InitStateString SafeConfigNode::readProperty(const string &property) const
{
    InitStateString res = m_readOnlyNode->readProperty(escape(property));
    return InitStateString(unescape(res.get()), res.wasSet());
}

void SafeConfigNode::writeProperty(const string &property,
                                   const InitStateString &value,
                                   const string &comment)
{
    m_node->writeProperty(escape(property),
                          InitStateString(escape(value.get()), value.wasSet()),
                          comment);
}

void SafeConfigNode::readProperties(ConfigProps &props) const
{
    ConfigProps original;
    m_readOnlyNode->readProperties(original);

    BOOST_FOREACH(const StringPair &prop, original) {
        string key = unescape(prop.first);
        string value = unescape(prop.second);

        props[key] = value;
    }
}

void SafeConfigNode::removeProperty(const string &property)
{
    m_node->removeProperty(escape(property));
}

void SafeConfigNode::flush()
{
    if (!m_node.get()) {
        SyncContext::throwError(getName() + ": read-only, flushing not allowed");
    }
    m_node->flush();
}

SE_END_CXX
