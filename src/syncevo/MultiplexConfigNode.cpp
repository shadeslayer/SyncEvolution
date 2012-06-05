/*
 * Copyright (C) 2009 Intel Corporation
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

#include <syncevo/MultiplexConfigNode.h>
#include <boost/algorithm/string/predicate.hpp>

SE_BEGIN_CXX

FilterConfigNode *
MultiplexConfigNode::getNode(const std::string &property,
                             const ConfigProperty **found) const
{
    BOOST_FOREACH(const ConfigProperty *prop, m_registry) {
        bool match = false;
        BOOST_FOREACH(const std::string &name, prop->getNames()) {
            if (name == property) {
                match = true;
                break;
            }
        }
        if (match) {
            if (found) {
                *found = prop;
            }

            FilterConfigNode *node = 
                m_nodes[prop->isHidden()][prop->getSharing()].get();

            return node;
        }
    }

    return NULL;
}

void MultiplexConfigNode::addFilter(const std::string &property,
                                    const InitStateString &value)
{
    FilterConfigNode::addFilter(property, value);
    for (int i = 0; i < 2; i++) {
        for (int e = 0; e < 3; e++) {
            if (m_nodes[i][e]) {
                m_nodes[i][e]->addFilter(property, value);
            }
        }
    }
}

void MultiplexConfigNode::setFilter(const ConfigFilter &filter)
{
    FilterConfigNode::setFilter(filter);
    for (int i = 0; i < 2; i++) {
        for (int e = 0; e < 3; e++) {
            if (m_nodes[i][e]) {
                m_nodes[i][e]->setFilter(filter);
            }
        }
    }
}

void MultiplexConfigNode::flush()
{
    for (int i = 0; i < 2; i++) {
        for (int e = 0; e < 3; e++) {
            if (m_nodes[i][e]) {
                m_nodes[i][e]->flush();
            }
        }
    }
}

InitStateString MultiplexConfigNode::readProperty(const std::string &property) const
{
    FilterConfigNode *node = getNode(property);
    if (node) {
        return node->readProperty(property);
    } else {
        return InitStateString();
    }
}

void MultiplexConfigNode::writeProperty(const std::string &property,
                                        const InitStateString &value,
                                        const std::string &comment)
{
    const ConfigProperty *prop;
    FilterConfigNode *node = getNode(property, &prop);
    if (node) {
        node->writeProperty(property, value, comment);
    } else {
        SE_THROW(property + ": not supported by configuration multiplexer");
    }
}

void MultiplexConfigNode::readProperties(PropsType &props) const
{
    for (int i = 0; i < 2; i++) {
        for (int e = 0; e < 3; e++) {
            if (m_nodes[i][e]) {
                m_nodes[i][e]->readProperties(props);
            }
        }
    }
}

void MultiplexConfigNode::removeProperty(const std::string &property)
{
#if 1
    SE_THROW(property + ": removing via configuration multiplexer not supported");
#else
    for (int i = 0; i < 2; i++) {
        for (int e = 0; e < 3; e++) {
            if (m_nodes[i][e]) {
                m_nodes[i][e]->removeProperty(property);
            }
        }
    }
#endif
}

void MultiplexConfigNode::clear()
{
#if 1
    SE_THROW("configuration multiplexer cannot be cleared");
#else
    for (int i = 0; i < 2; i++) {
        for (int e = 0; e < 3; e++) {
            if (m_nodes[i][e]) {
                m_nodes[i][e]->clear();
            }
        }
    }
#endif
}

bool MultiplexConfigNode::exists() const
{
    for (int i = 0; i < 2; i++) {
        for (int e = 0; e < 3; e++) {
            if (m_nodes[i][e] &&
                m_nodes[i][e]->exists()) {
                return true;
            }
        }
    }
    return false;
}

SE_END_CXX
