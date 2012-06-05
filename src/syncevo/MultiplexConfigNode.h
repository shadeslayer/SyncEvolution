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
#ifndef INCL_SYNCEVO_MULTIPLEX_CONFIG_NODE
# define INCL_SYNCEVO_MULTIPLEX_CONFIG_NODE

#include <syncevo/declarations.h>
#include <syncevo/ConfigNode.h>
#include <syncevo/SyncConfig.h>
SE_BEGIN_CXX

/**
 * Joins properties from the different nodes that might be used by a
 * SyncConfig or SyncSourceConfig (global/shared/not shared,
 * hidden/user-visible) and presents them as one node. Reading takes
 * the union of all set properties. Writing is directed to the
 * node for which the property was registered.
 */
class MultiplexConfigNode : public FilterConfigNode
{
    const std::string m_name;
    boost::shared_ptr<FilterConfigNode> m_nodes[2][3];
    const ConfigPropertyRegistry &m_registry;
    bool m_havePeerNodes;
    int m_hiddenLower, m_hiddenUpper;

    FilterConfigNode *getNode(const std::string &property,
                              const ConfigProperty **prop = NULL) const;

 public:
    /** join both hidden and user-visible properties */
    MultiplexConfigNode(const std::string &name,
                        const ConfigPropertyRegistry &registry) :
        FilterConfigNode(boost::shared_ptr<ConfigNode>()),
        m_name(name),
        m_registry(registry),
        m_havePeerNodes(true),
        m_hiddenLower(0), m_hiddenUpper(1) {}

    /** only join hidden or user-visible properties */
    MultiplexConfigNode(const std::string &name,
                        const ConfigPropertyRegistry &registry,
                        bool hidden) :
        FilterConfigNode(boost::shared_ptr<ConfigNode>()),
        m_name(name),
        m_registry(registry),
        m_havePeerNodes(true),
        m_hiddenLower(hidden), m_hiddenUpper(hidden) {}

    /** true when peer nodes are used (default), false when they are dummy nodes */
    bool getHavePeerNodes() const { return m_havePeerNodes; }
    void setHavePeerNodes(bool havePeerNodes) { m_havePeerNodes = havePeerNodes; }

    /** configure the nodes to use */
    void setNode(bool hidden, ConfigProperty::Sharing sharing,
                 const boost::shared_ptr<FilterConfigNode> &node) {
        m_nodes[hidden][sharing] = node;
    }
    void setNode(bool hidden, ConfigProperty::Sharing sharing,
                 const boost::shared_ptr<ConfigNode> &node) {
        m_nodes[hidden][sharing].reset(new FilterConfigNode(node));
    }

    virtual void addFilter(const std::string &property,
                           const InitStateString &value);
    virtual void setFilter(const ConfigFilter &filter);

    virtual std::string getName() const { return m_name; }
    virtual void flush();
    virtual InitStateString readProperty(const std::string &property) const;
    virtual void writeProperty(const std::string &property,
                               const InitStateString &value,
                               const std::string &comment = std::string(""));
    virtual void readProperties(PropsType &props) const;

    /*
     * removing or clearing something is not implemented because it is
     * not certain what should be deleted: only properties which are
     * not shared?!
     */
    virtual void removeProperty(const std::string &property);
    virtual void clear();

    /**
     * true if any of the nodes exists
     */
    virtual bool exists() const;
};

SE_END_CXX

#endif // INCL_SYNCEVO_MULTIPLEX_CONFIG_NODE
