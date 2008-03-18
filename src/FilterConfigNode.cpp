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

#include "FilterConfigNode.h"

FilterConfigNode::FilterConfigNode(const boost::shared_ptr<ConfigNode> &node,
                                   const ConfigFilter &filter) :
    m_node(node),
    m_filter(filter)
{
}

void FilterConfigNode::addFilter(const string &property,
                                 const string &value)
{
    m_filter.set(property, value);
}

void FilterConfigNode::setFilter(const ConfigFilter &filter)
{
    m_filter = filter;
}

string FilterConfigNode::readProperty(const string &property) const
{
    ConfigFilter::const_iterator it = m_filter.find(property);

    if (it != m_filter.end()) {
        return it->second;
    } else {
        return m_node->readProperty(property);
    }
}

void FilterConfigNode::setProperty(const string &property,
                                   const string &value,
                                   const string &comment)
{
    ConfigFilter::iterator it = m_filter.find(property);

    if (it != m_filter.end()) {
        m_filter.erase(it);
    }
    m_node->setProperty(property, value, comment);
}

map<string, string> FilterConfigNode::readProperties()
{
    map<string, string> res = m_node->readProperties();

    for(ConfigFilter::iterator it = m_filter.begin();
        it != m_filter.end();
        it++) {
        res.insert(*it);
    }

    return res;
}

void FilterConfigNode::removeProperty(const string &property)
{
    ConfigFilter::iterator it = m_filter.find(property);

    if (it != m_filter.end()) {
        m_filter.erase(it);
    }
    m_node->removeProperty(property);
}
