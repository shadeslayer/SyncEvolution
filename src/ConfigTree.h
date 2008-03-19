/*
 * Copyright (C) 2008 Patrick Ohly
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef INCL_EVOLUTION_CONFIG_TREE
# define INCL_EVOLUTION_CONFIG_TREE

#include <boost/shared_ptr.hpp>
#include <map>
#include <list>
#include <string>
using namespace std;

class ConfigNode;

/**
 * This class organizes the access to config nodes in a tree. Nodes
 * are identified by a relative path name, using a slash / as
 * separator between levels. Each node can have user-visible and
 * hidden properties. The two sets might be stored in the same
 * ConfigNode, i.e. properties should have unique names per node. For
 * each path there's also a second, separate namespace of key/value
 * pairs. The intented use for that is saving state by sync sources
 * close to, but without interfering with their configuration and the
 * state maintained by the client library itself.
 *
 * A ConfigNode can list all its properties while the tree lists nodes
 * at a specific level and creates nodes.
 *
 * This model is similar to the Funambol C++ DeviceManagementTree.
 * Besides being implemented differently, it also provides additional
 * functionality:
 * - the same node can be opened more than once; in the client library
 *   the content of multiple instances is not synchronized and changes
 *   can get lost
 * - nodes and the whole tree can be explicitly flushed
 * - it distinguishes between user visible configuration options and
 *   hidden read/write properties
 * - temporarily override values without saving them (see FilterConfigNode
 *   decorator)
 * - improved access to properties inside nodes (iterating, deleting)
 */
class ConfigTree {
 public:
    /** frees all resources *without* flushing changed nodes */
    virtual ~ConfigTree() {}

    /** ensure that all changes are saved persistently */
    virtual void flush() = 0;

    /** a string identifying the root of the configuration - exact meaning varies */
    virtual string getRootPath() const = 0;

    /**
     * Open the specified node. Opening it multiple
     * times will return the same instance, so the content
     * is always synchronized.
     *
     * @param path      a relative path with / as separator
     * @param hidden    access the part of the node which is not
     *                  supposed to be edited by the user
     * @param changeId  if set, then create a hidden change tracking node
     */
    virtual boost::shared_ptr<ConfigNode> open(const string &path,
                                               bool hidden,
                                               const string &changeId = string("")) = 0;

    /**
     * returns names of all existing nodes beneath the given path
     */
    virtual list<string> getChildren(const string &path) = 0;
};

#endif
