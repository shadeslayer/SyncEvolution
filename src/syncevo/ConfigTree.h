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

#ifndef INCL_EVOLUTION_CONFIG_TREE
# define INCL_EVOLUTION_CONFIG_TREE

#include <boost/shared_ptr.hpp>
#include <map>
#include <list>
#include <string>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

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
 *   hidden read/write properties attached to the same path
 * - in addition to these visible or hidden properties under well-known
 *   names there can be nodes attached to each path which can
 *   be used for arbitrary key/value pairs; different "other" nodes can
 *   be selected via an additional string
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

    /** tell all nodes to reload from background storage, discarding in-memory changes */
    virtual void reload() = 0;

    /**
     * Remove all configuration nodes below and including a certain
     * path and (if based on files) directories created for them, if
     * empty after file removal.
     *
     * The nodes must not be in use for this to work.
     */
    virtual void remove(const std::string &path) = 0;

    /** a string identifying the root of the configuration - exact meaning varies */
    virtual std::string getRootPath() const = 0;

    /**
     * Selects which node attached to a path name is to be used.
     * This is similar in concept to multiple data forks in a file.
     */
    enum PropertyType {
        visible,   /**< visible configuration properties */
        hidden,    /**< hidden read/write properties */
        other,     /**< additional node selected via otherID */
        server,    /**< yet another additional node, similar to other */
    };

    /**
     * Open the specified node. Opening it multiple
     * times will return the same instance, so the content
     * is always synchronized.
     *
     * @param path      a relative path with / as separator
     * @param type      selects which fork of that path is to be opened
     *                  (visible, hidden, change tracking, server)
     * @param otherId   an additional string to be attached to the 'other' or 'server'
     *                  node's name (allows having multiple different such
     *                  nodes); an empty string is allowed
     */
    virtual boost::shared_ptr<ConfigNode> open(const std::string &path,
                                               PropertyType type,
                                               const std::string &otherId = std::string("")) = 0;

    /**
     * Use the specified node, with type determined
     * by caller. The reason for adding the instance is
     * twofold:
     * - ensure that flush() is called on the node
     *   as part of flushing the tree
     * - an existing instance is reused and shared between
     *   different users of the tree
     *
     * @param path       a relative or absolute path, may be outside of normal tree
     * @param node       default instance if not opened before, discarded if a
     *                   node was registered or opened under the given path before
     */
    virtual boost::shared_ptr<ConfigNode> add(const std::string &path,
                                              const boost::shared_ptr<ConfigNode> &node) = 0;

    /**
     * returns names of all existing nodes beneath the given path
     */
    virtual std::list<std::string> getChildren(const std::string &path) = 0;
};


SE_END_CXX
#endif
