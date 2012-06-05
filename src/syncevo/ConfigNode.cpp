/*
 * Copyright (C) 2008-2009 Patrick Ohly <patrick.ohly@gmx.de>
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

#include <syncevo/IniConfigNode.h>
#include <syncevo/SafeConfigNode.h>

#include <syncevo/declarations.h>

using namespace std;

SE_BEGIN_CXX

boost::shared_ptr<ConfigNode> ConfigNode::createFileNode(const string &filename)
{
    string::size_type off = filename.rfind('/');
    boost::shared_ptr<ConfigNode> filenode;
    if (off != filename.npos) {
        filenode.reset(new IniFileConfigNode(filename.substr(0, off),
                                             filename.substr(off + 1),
                                             false));
    } else {
        filenode.reset(new IniFileConfigNode(".", filename, false));
    }
    boost::shared_ptr<SafeConfigNode> savenode(new SafeConfigNode(filenode));
    savenode->setMode(false);
    return savenode;
}

void ConfigNode::writeProperties(const ConfigProps &props)
{
    BOOST_FOREACH(const ConfigProps::value_type &entry, props) {
        setProperty(entry.first, entry.second);
    }
}

SE_END_CXX
