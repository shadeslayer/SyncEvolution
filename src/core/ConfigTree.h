/*
 * Copyright (C) 2008 Patrick Ohly
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

    /** a string identifying the root of the configuration - exact meaning varies */
    virtual string getRootPath() const = 0;

    /**
     * Selects which node attached to a path name is to be used.
     * This is similar in concept to multiple data forks in a file.
     */
    enum PropertyType {
        visible,   /**< visible configuration properties */
        hidden,    /**< hidden read/write properties */
        other      /**< additional node selected via otherID */
    };

    /**
     * Open the specified node. Opening it multiple
     * times will return the same instance, so the content
     * is always synchronized.
     *
     * @param path      a relative path with / as separator
     * @param type      selects which fork of that path is to be opened
     *                  (visible, hidden, change tracking)
     * @param otherId   an additional string to be attached to the other
     *                  node's name (allows having multiple different such
     *                  nodes); an empty string is allowed
     */
    virtual boost::shared_ptr<ConfigNode> open(const string &path,
                                               PropertyType type,
                                               const string &otherId = string("")) = 0;

    /**
     * returns names of all existing nodes beneath the given path
     */
    virtual list<string> getChildren(const string &path) = 0;
};

#endif
