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

#ifndef INCL_SYNCEVO_DEVNULL_CONFIG_NODE
# define INCL_SYNCEVO_DEVNULL_CONFIG_NODE

#include <syncevo/ConfigNode.h>
#include <syncevo/util.h>
SE_BEGIN_CXX

/**
 * A read-only node which raises an exception when someone tries to
 * write into it.
 */
class DevNullConfigNode : public ConfigNode {
    string m_name;

 public:
    DevNullConfigNode(const string &name) : m_name() {}

    virtual string getName() const { return m_name; }
    virtual void flush() {}
    virtual InitStateString readProperty(const string &property) const { return ""; }
    virtual void writeProperty(const string &property,
                               const InitStateString &value,
                               const string &comment = string(""))
    {
        SE_THROW(m_name + ": virtual read-only configuration node, cannot write property " +
                 property + " = " + value);
    }
    virtual void readProperties(PropsType &props) const {}
    virtual void writeProperties(const PropsType &props)
    {
        if (!props.empty()) {
            SE_THROW(m_name + ": virtual read-only configuration node, cannot write properties");
        }
    }
    virtual void removeProperty(const string &property) {}
    virtual void clear() {}
    virtual bool exists() const { return false; }
    virtual bool isReadOnly() const { return true; }
};


SE_END_CXX
#endif // SYNCEVO_DEVNULL_CONFIG_NODE
