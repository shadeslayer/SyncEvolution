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

#ifndef INCL_EVOLUTION_SINGLE_FILE_CONFIG_TREE
# define INCL_EVOLUTION_SINGLE_FILE_CONFIG_TREE

#include <syncevo/ConfigTree.h>
#include <syncevo/DataBlob.h>
#include <syncevo/util.h>

#include <string>
#include <map>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

/**
 * This class handles data blobs which contain multiple .ini files, using
 * the following format:
 * @verbatim
# comment
# ...
=== <first path>/[.internal.ini|config.ini|template.ini|...] ===
<file content>
=== <second file name> ===
...
 * @endverbatim
 *
 * This is based on the assumption that the === ... === file separator
 * is not part of valid .ini file content.
 *
 * Right now, only reading such a single data blob is implemented.
 */
class SingleFileConfigTree : public ConfigTree {
 public:
    /**
     * @param data          access to complete file data
     */
    SingleFileConfigTree(const boost::shared_ptr<DataBlob> &data);
    SingleFileConfigTree(const std::string &fullpath);

    /**
     * same as open(), with full file name (like sources/addressbook/config.ini)
     * instead of path + type
     */
    boost::shared_ptr<ConfigNode> open(const std::string &filename);

    /* ConfigTree API */
    virtual std::string getRootPath() const { return m_data->getName(); }
    virtual void flush();
    virtual void reload();
    virtual void remove(const std::string &path);
    virtual void reset();
    virtual boost::shared_ptr<ConfigNode> open(const std::string &path,
                                               PropertyType type,
                                               const std::string &otherId = std::string(""));
    virtual boost::shared_ptr<ConfigNode> add(const std::string &path,
                                              const boost::shared_ptr<ConfigNode> &bode);
    std::list<std::string> getChildren(const std::string &path);

 private:
    boost::shared_ptr<DataBlob> m_data;

    /**
     * maps from normalized file name (see normalizePath()) to content for that name
     */
    typedef std::map<std::string, boost::shared_ptr<std::string> > FileContent_t;
    FileContent_t m_content;

    /** cache of all nodes ever accessed */
    typedef std::map< std::string, boost::shared_ptr<ConfigNode> > NodeCache_t;
    NodeCache_t m_nodes;

    /**
     * populate m_content from m_data
     */
    void readFile();
};

SE_END_CXX
#endif
