/*
 * Copyright (C) 2008 Patrick Ohly
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY, TITLE, NONINFRINGEMENT or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307  USA
 */

#include <string.h>
#include <ctype.h>

#include "FileConfigTree.h"
#include "FileConfigNode.h"
#include "SyncEvolutionUtil.h"

#include <boost/foreach.hpp>

#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>

FileConfigTree::FileConfigTree(const string &root,
                               bool oldLayout) :
    m_root(root),
    m_oldLayout(oldLayout)
{
}

string FileConfigTree::getRootPath() const
{
    return normalizePath(m_root);
}

void FileConfigTree::flush()
{
    BOOST_FOREACH(const NodeCache_t::value_type &node, m_nodes) {
        node.second->flush();
    }
}

void FileConfigTree::reset()
{
    m_nodes.clear();
}

boost::shared_ptr<ConfigNode> FileConfigTree::open(const string &path,
                                                   ConfigTree::PropertyType type,
                                                   const string &otherId)
{
    string fullpath;
    string filename;
    
    fullpath = normalizePath(m_root + "/" + path + "/");
    if (type == other) {
        if (m_oldLayout) {
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
        filename = m_oldLayout ? "config.txt" :
            type == hidden ? ".internal.ini" :
            "config.ini";
    }

    string fullname = normalizePath(fullpath + "/" + filename);
    NodeCache_t::iterator found = m_nodes.find(fullname);
    if (found != m_nodes.end()) {
        return found->second;
    } else {
        boost::shared_ptr<ConfigNode> node(new FileConfigNode(fullpath, filename));
        return m_nodes[fullname] = node;
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
