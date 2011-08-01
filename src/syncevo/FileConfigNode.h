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

#ifndef INCL_EVOLUTION_FILE_CONFIG_NODE
# define INCL_EVOLUTION_FILE_CONFIG_NODE

#include <syncevo/ConfigNode.h>

#include <string>
#include <list>

#include <syncevo/declarations.h>
SE_BEGIN_CXX
using namespace std;

/**
 * A base class for file related config
 */
class FileBaseConfigNode: public ConfigNode {
  protected:
    string m_path;
    string m_fileName;
    bool m_modified;
    const bool m_readonly;
    bool m_exists;
    
    /**
     * Open or create a new file. The file will be read (if it exists)
     * but not create or written to unless flush() is called explicitly
     *
     * @param path      node name, maps to directory
     * @param fileName  name of file inside that directory
     * @param readonly  do not create or write file, it must exist;
     *                  flush() will throw an exception when changes would have to be written
     */
    FileBaseConfigNode(const string &path, const string &fileName, bool readonly);
    /** 
     * a virtual method to serial data structure to the file
     * It is used by flush function to flush memory into disk file
     */
    virtual void toFile(FILE* file) = 0;
  public:
    virtual void flush();
    virtual string getName() const { return m_path + "/" + m_fileName; }
    virtual bool exists() const { return m_exists; }
    virtual bool isReadOnly() const { return m_readonly; }
};
/**
 * This class started its life as the Posix implementation of the
 * ManagementNode in the Funambol C++ client library. Nowadays it is
 * part of the SyncEvoluition ConfigTree (see there for details).
 *
 * Each node is mapped to one file whose location is determined by
 * the ConfigTree when the node gets created. Each node represents
 * one .ini file with entries of the type
 * <property>\s*=\s*<value>\s*\n
 *
 * Comments look like:
 * \s*# <comment>
 *
 * @todo rewrite with standard C++ containers
 */
class FileConfigNode : public FileBaseConfigNode {
    list<string> m_lines;

    void read();

 protected:

    virtual void toFile(FILE* file);

 public:
    /**
     * Open or create a new file. The file will be read (if it exists)
     * but not create or written to unless flush() is called explicitly
     *
     * @param path      node name, maps to directory
     * @param fileName  name of file inside that directory
     * @param readonly  do not create or write file, it must exist;
     *                  flush() will throw an exception when changes would have to be written
     */
    FileConfigNode(const string &path, const string &fileName, bool readonly);

    /* keep underlying methods visible; our own setProperty() would hide them */
    using ConfigNode::setProperty;

    virtual string readProperty(const string &property) const;
    virtual void setProperty(const string &property,
                             const string &value,
                             const string &comment = "",
                             const string *defValue = NULL);
    virtual void readProperties(ConfigProps &props) const;
    virtual void removeProperty(const string &property);
    virtual void clear();
};

/**
 * The main difference from FileConfigNode is to store pair of 'property-value'
 * in a map to avoid O(n^2) string comparison
 * Here comments for property default value are discarded.
 */
class HashFileConfigNode: public FileBaseConfigNode {
    map<std::string, std::string> m_props;
    /**
     * Map used to store pairs
     */
    void read();

 protected:

    virtual void toFile(FILE* file);

 public:
    HashFileConfigNode(const string &path, const string &fileName, bool readonly);
    virtual string readProperty(const string &property) const;
    virtual void setProperty(const string &property,
                             const string &value,
                             const string &comment = "",
                             const string *defValue = NULL);
    virtual void readProperties(ConfigProps &props) const;
    virtual void writeProperties(const ConfigProps &props);
    virtual void removeProperty(const string &property);
    virtual void clear();
};


SE_END_CXX
#endif
