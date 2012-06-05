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

#include <syncevo/FilterConfigNode.h>
#include <syncevo/SyncContext.h>

#include <boost/foreach.hpp>
#include <boost/algorithm/string/join.hpp>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

FilterConfigNode::FilterConfigNode(const boost::shared_ptr<ConfigNode> &node,
                                   const ConfigFilter &filter) :
    m_filter(filter),
    m_node(node),
    m_readOnlyNode(node)
{
}

FilterConfigNode::FilterConfigNode(const boost::shared_ptr<const ConfigNode> &node,
                                   const ConfigFilter &filter) :
    m_filter(filter),
    m_readOnlyNode(node)
{
}

void FilterConfigNode::addFilter(const string &property,
                                 const InitStateString &value)
{
    m_filter[property] = value;
}

void FilterConfigNode::setFilter(const ConfigFilter &filter)
{
    m_filter = filter;
}

InitStateString FilterConfigNode::readProperty(const string &property) const
{
    ConfigFilter::const_iterator it = m_filter.find(property);

    if (it != m_filter.end()) {
        return it->second;
    } else {
        return m_readOnlyNode->readProperty(property);
    }
}

void FilterConfigNode::writeProperty(const string &property,
                                     const InitStateString &value,
                                     const string &comment)
{
    ConfigFilter::iterator it = m_filter.find(property);

    if (!m_node.get()) {
        SyncContext::throwError(getName() + ": read-only, setting properties not allowed");
    }

    if (it != m_filter.end()) {
        m_filter.erase(it);
    }
    m_node->writeProperty(property, value, comment);
}

void FilterConfigNode::readProperties(ConfigProps &props) const
{
    m_readOnlyNode->readProperties(props);

    BOOST_FOREACH(const StringPair &filter, m_filter) {
        // overwrite existing values or add new ones
        props[filter.first] = filter.second;
    }
}

void FilterConfigNode::removeProperty(const string &property)
{
    ConfigFilter::iterator it = m_filter.find(property);

    if (!m_node.get()) {
        SyncContext::throwError(getName() + ": read-only, removing properties not allowed");
    }

    if (it != m_filter.end()) {
        m_filter.erase(it);
    }
    m_node->removeProperty(property);
}

void FilterConfigNode::clear()
{
    m_filter.clear();
    m_node->clear();
}

void FilterConfigNode::flush()
{
    if (!m_node.get()) {
        SyncContext::throwError(getName() + ": read-only, flushing not allowed");
    }
    m_node->flush();
}

SE_END_CXX
