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

#ifndef INCL_EVOLUTION_FILE_CONFIG_TREE
# define INCL_EVOLUTION_FILE_CONFIG_TREE

#include <syncevo/ConfigTree.h>
#include <syncevo/SyncConfig.h>

#include <string>
#include <map>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

/**
 * This implementation maps nodes to plain .ini style files below an
 * absolute directory of the filesystem. The caller is responsible for
 * choosing that directory and how hidden and user-visible files are
 * to be named.
 */
class FileConfigTree : public ConfigTree {
 public:
    /**
     * @param root              absolute filesystem path for
     *                          .syncj4/evolution or .config/syncevolution
     * @param peer              the relative path to the peer configuration
     * @param layout            determines file names to be used;
     *                          HTTP_SERVER_LAYOUT and SHARED_LAYOUT are the same except
     *                          that SHARED_LAYOUT creates the "peers" directory during
     *                          flushing
     */
    FileConfigTree(const std::string &root,
                   const std::string &peer,
                   SyncConfig::Layout layout);

    void setReadOnly(bool readonly) { m_readonly = readonly; }
    bool getReadOnly() const { return m_readonly; }

    /* ConfigTree API */
    virtual std::string getRootPath() const;
    virtual void flush();
    virtual void reload();
    virtual void remove(const std::string &path);
    virtual void reset();
    virtual boost::shared_ptr<ConfigNode> open(const std::string &path,
                                               PropertyType type,
                                               const std::string &otherId = std::string(""));
    virtual boost::shared_ptr<ConfigNode> add(const std::string &path,
                                              const boost::shared_ptr<ConfigNode> &node);
    std::list<std::string> getChildren(const std::string &path);

 private:
    /**
     * remove all nodes from the node cache which are located at 'fullpath' 
     * or are contained inside it
     */
    void clearNodes(const std::string &fullpath);

 private:
    const std::string m_root;
    const std::string m_peer;
    SyncConfig::Layout m_layout;
    bool m_readonly;

    typedef std::map< std::string, boost::shared_ptr<ConfigNode> > NodeCache_t;
    /** cache of all nodes ever accessed */
    NodeCache_t m_nodes;
};


SE_END_CXX
#endif
