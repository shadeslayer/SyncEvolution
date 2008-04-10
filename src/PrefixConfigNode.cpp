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

#include "PrefixConfigNode.h"
#include "EvolutionSyncClient.h"

#include <boost/foreach.hpp>
#include <boost/algorithm/string/predicate.hpp>

PrefixConfigNode::PrefixConfigNode(const string prefix,
                                   const boost::shared_ptr<ConfigNode> &node) :
    m_prefix(prefix),
    m_node(node),
    m_readOnlyNode(node)
{
}

PrefixConfigNode::PrefixConfigNode(const string prefix,
                                   const boost::shared_ptr<const ConfigNode> &node) :
    m_prefix(prefix),
    m_readOnlyNode(node)
{
}

string PrefixConfigNode::readProperty(const string &property) const
{
    return m_readOnlyNode->readProperty(m_prefix + property);
}

void PrefixConfigNode::setProperty(const string &property,
                                 const string &value,
                                 const string &comment,
                                 const string *defValue)
{
    m_node->setProperty(m_prefix + property,
                        value,
                        comment,
                        defValue);
}

void PrefixConfigNode::readProperties(map<string, string> &props) const
{
    map<string, string> original;
    m_readOnlyNode->readProperties(original);

    for(map<string, string>::iterator it = original.begin();
        it != original.end();
        ++it) {
        string key = it->first;
        string value = it->second;

        if (boost::starts_with(key, m_prefix)) {
            props.insert(make_pair(key.substr(m_prefix.size()), value));
        }
    }
}

void PrefixConfigNode::removeProperty(const string &property)
{
    m_node->removeProperty(m_prefix + property);
}

void PrefixConfigNode::flush()
{
    if (!m_node.get()) {
        EvolutionSyncClient::throwError(getName() + ": read-only, flushing not allowed");
    }
    m_node->flush();
}
