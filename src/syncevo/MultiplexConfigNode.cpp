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
MultiplexConfigNode::getNode(const string &property,
                             const ConfigProperty **found) const
{
    BOOST_FOREACH(const ConfigProperty *prop, m_registry) {
        if (boost::iequals(prop->getName(), property)) {
            if (found) {
                *found = prop;
            }

            FilterConfigNode *node = 
                m_nodes[prop->isHidden()][prop->getSharing()].get();

            // special case: fall back to shared node if no unshared
            // node or only dummy one, and property is primarily
            // stored unshared, but also in the shared node
            if ((!node || !m_havePeerNodes) &&
                prop->getSharing() == ConfigProperty::NO_SHARING &&
                (prop->getFlags() & ConfigProperty::SHARED_AND_UNSHARED)) {
                node = m_nodes[prop->isHidden()][ConfigProperty::SOURCE_SET_SHARING].get();
            }

            return node;
        }
    }

    return NULL;
}

void MultiplexConfigNode::addFilter(const string &property,
                                    const string &value)
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

string MultiplexConfigNode::readProperty(const string &property) const
{
    FilterConfigNode *node = getNode(property);
    if (node) {
        return node->readProperty(property);
    } else {
        return "";
    }
}

void MultiplexConfigNode::setProperty(const string &property,
                                      const string &value,
                                      const string &comment,
                                      const string *defValue)
{
    const ConfigProperty *prop;
    FilterConfigNode *node = getNode(property, &prop);
    if (node) {
        node->setProperty(property, value, comment, defValue);
        bool hidden = prop->isHidden();
        if (node == m_nodes[hidden][ConfigProperty::NO_SHARING].get() &&
            (node = m_nodes[hidden][ConfigProperty::SOURCE_SET_SHARING].get()) != NULL &&
            (prop->getFlags() & ConfigProperty::SHARED_AND_UNSHARED)) {
            node->setProperty(property, value, comment, defValue);
        }
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

void MultiplexConfigNode::removeProperty(const string &property)
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
