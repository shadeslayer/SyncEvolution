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

#ifndef INCL_EVOLUTION_INI_CONFIG_NODE
# define INCL_EVOLUTION_INI_CONFIG_NODE

#include <syncevo/ConfigNode.h>
#include <syncevo/DataBlob.h>

#include <string>
#include <list>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

/**
 * A base class for .ini style data blobs.
 */
class IniBaseConfigNode: public ConfigNode {
  protected:
    boost::shared_ptr<DataBlob> m_data;
    bool m_modified;
    
    /**
     * Open or create a new blob. The blob will be read (if it exists)
     * but not created or written to unless flush() is called explicitly.
     */
    IniBaseConfigNode(const boost::shared_ptr<DataBlob> &data);

    /** 
     * a virtual method to serial data structure to the file
     * It is used by flush function to flush memory into disk file
     */
    virtual void toFile(std::ostream &file) = 0;

  public:
    virtual void flush();
    virtual void reload() = 0;
    virtual std::string getName() const { return m_data->getName(); }
    virtual bool exists() const { return m_data->exists(); }
    virtual bool isReadOnly() const { return true; }
};

/**
 * This class started its life as the Posix implementation of the
 * ManagementNode in the Funambol C++ client library. Nowadays it is
 * part of the SyncEvolution ConfigTree (see there for details).
 *
 * Each node is mapped to one file whose location is determined by
 * the ConfigTree when the node gets created. Each node represents
 * one .ini file with entries of the type
 * <property>\s*=\s*<value>\s*\n
 *
 * Comments look like:
 * \s*# <comment>
 *
 */
class IniFileConfigNode : public IniBaseConfigNode {
    std::list<std::string> m_lines;

    void read();

 protected:
    virtual void toFile(std::ostream &file);

 public:
    IniFileConfigNode(const boost::shared_ptr<DataBlob> &data);
    IniFileConfigNode(const std::string &path, const std::string &fileName, bool readonly);

    /* keep underlying methods visible; our own setProperty() would hide them */
    using ConfigNode::setProperty;

    virtual InitStateString readProperty(const std::string &property) const;
    virtual void writeProperty(const std::string &property,
                               const InitStateString &value,
                               const std::string &comment = "");
    virtual void readProperties(ConfigProps &props) const;
    virtual void removeProperty(const std::string &property);
    virtual void clear();
    virtual void reload() { clear(); read(); }
};

/**
 * The main difference from FileConfigNode is to store pair of 'property-value'
 * in a map to avoid O(n^2) string comparison
 * Here comments for property default value are discarded and unset
 * properties are not stored.
 */
class IniHashConfigNode: public IniBaseConfigNode {
    std::map<std::string, std::string> m_props;
    /**
     * Map used to store pairs
     */
    void read();

 protected:

    virtual void toFile(std::ostream & file);

 public:
    IniHashConfigNode(const boost::shared_ptr<DataBlob> &data);
    IniHashConfigNode(const std::string &path, const std::string &fileName, bool readonly);
    virtual InitStateString readProperty(const std::string &property) const;
    virtual void writeProperty(const std::string &property,
                               const InitStateString &value,
                               const std::string &comment = "");
    virtual void readProperties(ConfigProps &props) const;
    virtual void writeProperties(const ConfigProps &props);
    virtual void removeProperty(const std::string &property);
    virtual void clear();
    virtual void reload() { clear(); read(); }
};


SE_END_CXX
#endif // INCL_EVOLUTION_INI_CONFIG_NODE
