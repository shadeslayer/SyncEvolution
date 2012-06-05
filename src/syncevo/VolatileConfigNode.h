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

#ifndef INCL_EVOLUTION_VOLATILE_CONFIG_NODE
# define INCL_EVOLUTION_VOLATILE_CONFIG_NODE

#include <syncevo/FilterConfigNode.h>
#include <syncevo/IniConfigNode.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

/**
 * This class can store properties while in memory, but will never
 * save them persistently. Implemented by instantiating an IniHashConfigNode
 * (because order of entries doesn't matter)
 * with invalid path and never calling its flush() method.
 */
class VolatileConfigNode : public FilterConfigNode {
 public:
 VolatileConfigNode() :
    FilterConfigNode(boost::shared_ptr<ConfigNode>(new IniHashConfigNode("/dev/null", "dummy.ini", true)))
        {}

    virtual std::string getName() const { return "intermediate configuration"; }
    virtual void flush() {}
};


SE_END_CXX
#endif
