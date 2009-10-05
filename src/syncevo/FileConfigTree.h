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

#include <ConfigTree.h>

#include <string>
#include <map>

#include <syncevo/declarations.h>
SE_BEGIN_CXX
using namespace std;

/**
 * This implementation maps nodes to plain .ini style files below an
 * absolute directory of the filesystem. The caller is responsible for
 * choosing that directory and how hidden and user-visible files are
 * to be named.
 */
class FileConfigTree : public ConfigTree {
 public:
    /**
     * @param root              absolute filesystem path
     * @param oldLayout         use file names as in SyncEvolution <= 0.7
     */
    FileConfigTree(const string &root,
                   bool oldLayout);

    void setReadOnly(bool readonly) { m_readonly = readonly; }
    bool getReadOnly() const { return m_readonly; }

    /* ConfigTree API */
    virtual string getRootPath() const;
    virtual void flush();
    virtual void remove();
    virtual void reset();
    virtual boost::shared_ptr<ConfigNode> open(const string &path,
                                               PropertyType type,
                                               const string &otherId = string(""));
    list<string> getChildren(const string &path);

 private:
    const string m_root;
    const bool m_oldLayout;
    bool m_readonly;

    typedef map< string, boost::shared_ptr<ConfigNode> > NodeCache_t;
    /** cache of all nodes ever accessed */
    NodeCache_t m_nodes;
};


SE_END_CXX
#endif
