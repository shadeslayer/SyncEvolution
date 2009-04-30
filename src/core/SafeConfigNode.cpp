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

#include "SafeConfigNode.h"
#include "EvolutionSyncClient.h"

#include <boost/foreach.hpp>

SafeConfigNode::SafeConfigNode(const boost::shared_ptr<ConfigNode> &node) :
    m_node(node),
    m_readOnlyNode(node)
{
}

SafeConfigNode::SafeConfigNode(const boost::shared_ptr<const ConfigNode> &node) :
    m_readOnlyNode(node)
{
}

string SafeConfigNode::readProperty(const string &property) const
{
    return unescape(m_readOnlyNode->readProperty(escape(property)));
}

void SafeConfigNode::setProperty(const string &property,
                                 const string &value,
                                 const string &comment,
                                 const string *defValue)
{
    m_node->setProperty(escape(property),
                        escape(value),
                        comment,
                        defValue);
}

void SafeConfigNode::readProperties(map<string, string> &props) const
{
    map<string, string> original;
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
        EvolutionSyncClient::throwError(getName() + ": read-only, flushing not allowed");
    }
    m_node->flush();
}

string SafeConfigNode::escape(const string &str)
{
    string res;
    char buffer[4];
    res.reserve(str.size() * 3);

    BOOST_FOREACH(char c, str) {
        if(isalnum(c) ||
           c == '-' ||
           c == '_') {
            res += c;
        } else {
            sprintf(buffer, "!%02x",
                    (unsigned int)(unsigned char)c);
            res += buffer;
        }
    }

    return res;
}

string SafeConfigNode::unescape(const string &str)
{
    string res;
    size_t curr;

    res.reserve(str.size());

    curr = 0;
    while (curr < str.size()) {
        if (str[curr] == '!') {
            string hex = str.substr(curr + 1, 2);
            res += (char)strtol(hex.c_str(), NULL, 16);
            curr += 3;
        } else {
            res += str[curr];
            curr++;
        }
    }

    return res;
}
