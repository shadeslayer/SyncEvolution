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

#include <string.h>
#include <ctype.h>

#include <syncevo/FileConfigTree.h>
#include <syncevo/IniConfigNode.h>
#include <syncevo/util.h>

#include <boost/foreach.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>

#include <syncevo/declarations.h>
using namespace std;
SE_BEGIN_CXX

FileConfigTree::FileConfigTree(const string &root,
                               const string &peer,
                               SyncConfig::Layout layout) :
    m_root(root),
    m_peer(peer),
    m_layout(layout),
    m_readonly(false)
{
}

string FileConfigTree::getRootPath() const
{
    return normalizePath(m_root + "/" + m_peer);
}

void FileConfigTree::flush()
{
    BOOST_FOREACH(const NodeCache_t::value_type &node, m_nodes) {
        node.second->flush();
    }
    if (m_layout == SyncConfig::SHARED_LAYOUT) {
        // ensure that "peers" directory exists for new-style configs,
        // not created by flushing nodes for pure context configs but
        // needed to detect new-syle configs
        mkdir_p(getRootPath() + "/peers");
    }
}

void FileConfigTree::reload()
{
    BOOST_FOREACH(const NodeCache_t::value_type &node, m_nodes) {
        node.second->reload();
    }
}

/**
 * remove config files, backup files of config files (with ~ at
 * the end) and empty directories
 */
static bool rm_filter(const string &path, bool isDir)
{
    if (isDir) {
        // skip non-empty directories
        ReadDir dir(path);
        return dir.begin() == dir.end();
    } else {
        // only delete well-known files
        return boost::ends_with(path, "/config.ini") ||
            boost::ends_with(path, "/config.ini~") ||
            boost::ends_with(path, "/config.txt") ||
            boost::ends_with(path, "/config.txt~") ||
            boost::ends_with(path, "/.other.ini") ||
            boost::ends_with(path, "/.other.ini~") ||
            boost::ends_with(path, "/.server.ini") ||
            boost::ends_with(path, "/.server.ini~") ||
            boost::ends_with(path, "/.internal.ini") ||
            boost::ends_with(path, "/.internal.ini~") ||
            path.find("/.synthesis/") != path.npos;
    }
}

void FileConfigTree::remove(const string &path)
{
    string fullpath = m_root + "/" + path;
    clearNodes(fullpath);
    rm_r(fullpath, rm_filter);
}

void FileConfigTree::reset()
{
    for (NodeCache_t::iterator it = m_nodes.begin();
         it != m_nodes.end();
         ++it) {
        if (it->second.use_count() > 1) {
            // If the use count is larger than 1, then someone besides
            // the cache is referencing the node. We cannot force that
            // other entity to drop the reference, so bail out here.
            SE_THROW(it->second->getName() +
                     ": cannot be removed while in use");
        }
    }
    m_nodes.clear();
}

void FileConfigTree::clearNodes(const string &fullpath) 
{
    NodeCache_t::iterator it;
    it = m_nodes.begin();
    while (it != m_nodes.end()) {
        const string &key = it->first;
        if (boost::starts_with(key, fullpath)){
            /* 'it = m_nodes.erase(it);' doesn't make sense
             * because 'map::erase' returns 'void' in gcc. But other 
             * containers like list, vector could work! :( 
             * Below is STL recommended usage. 
             */
            NodeCache_t::iterator erased = it++;
            if (erased->second.use_count() > 1) {
                // same check as in reset()
                SE_THROW(erased->second->getName() +
                         ": cannot be removed while in use");
            }
            m_nodes.erase(erased);
        } else {
            ++it;
        }
    }
}

boost::shared_ptr<ConfigNode> FileConfigTree::open(const string &path,
                                                   ConfigTree::PropertyType type,
                                                   const string &otherId)
{
    string fullpath;
    string filename;
    
    fullpath = normalizePath(m_root + "/" + path + "/");
    if (type == other) {
        if (m_layout == SyncConfig::SYNC4J_LAYOUT) {
            fullpath += "/changes";
            if (!otherId.empty()) {
                fullpath += "_";
                fullpath += otherId;
            }
            filename = "config.txt";
        } else {
            filename += ".other";
            if (!otherId.empty()) {
                filename += "_";
                filename += otherId;
            }
            filename += ".ini";
        }
    } else {
        filename = type == server ? ".server.ini" :
            m_layout == SyncConfig::SYNC4J_LAYOUT ? "config.txt" :
            type == hidden ? ".internal.ini" :
            "config.ini";
    }

    string fullname = normalizePath(fullpath + "/" + filename);
    NodeCache_t::iterator found = m_nodes.find(fullname);
    if (found != m_nodes.end()) {
        return found->second;
    } else if(type != other && type != server) {
        boost::shared_ptr<ConfigNode> node(new IniFileConfigNode(fullpath, filename, m_readonly));
        return m_nodes[fullname] = node;
    } else {
        boost::shared_ptr<ConfigNode> node(new IniHashConfigNode(fullpath, filename, m_readonly));
        return m_nodes[fullname] = node;
    }
}

boost::shared_ptr<ConfigNode> FileConfigTree::add(const string &path,
                                                  const boost::shared_ptr<ConfigNode> &node)
{
    NodeCache_t::iterator found = m_nodes.find(path);
    if (found != m_nodes.end()) {
        return found->second;
    } else {
        m_nodes[path] = node;
        return node;
    }
}

static inline bool isNode(const string &dir, const string &name) {
    struct stat buf;
    string fullpath = dir + "/" + name;
    return !stat(fullpath.c_str(), &buf) && S_ISDIR(buf.st_mode);
}
 
list<string> FileConfigTree::getChildren(const string &path)
{
    list<string> res;

    string fullpath;
    fullpath = normalizePath(m_root + "/" + path);

    // first look at existing files
    if (!access(fullpath.c_str(), F_OK)) {
        ReadDir dir(fullpath);
        BOOST_FOREACH(const string entry, dir) {
            if (isNode(fullpath, entry)) {
                res.push_back(entry);
            }
        }
    }

    // Now also add those which have been created,
    // but not saved yet. The full path must be
    // <path>/<childname>/<filename>.
    fullpath += "/";
    BOOST_FOREACH(const NodeCache_t::value_type &node, m_nodes) {
        string currpath = node.first;
        if (currpath.size() > fullpath.size() &&
            currpath.substr(0, fullpath.size()) == fullpath) {
            // path prefix matches, now check whether we have
            // a real sibling, i.e. another full path below
            // the prefix
            size_t start = fullpath.size();
            size_t end = currpath.find('/', start);
            if (currpath.npos != end) {
                // Okay, another path separator found.
                // Now make sure we don't have yet another
                // directory level.
                if (currpath.npos == currpath.find('/', end + 1)) {
                    // Insert it if not there yet.
                    string name = currpath.substr(start, end - start);
                    if (res.end() == find(res.begin(), res.end(), name)) {
                        res.push_back(name);
                    }
                }
            }
        }
    }

    return res;
}

SE_END_CXX
