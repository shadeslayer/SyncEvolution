/*
 * Copyright (C) 2008 Patrick Ohly
 */

#ifndef INCL_EVOLUTION_FILE_CONFIG_NODE
# define INCL_EVOLUTION_FILE_CONFIG_NODE

#include "ConfigNode.h"

#include <string>
#include <list>
using namespace std;

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
class FileConfigNode : public ConfigNode {
    string m_path;
    string m_fileName;

    list<string> m_lines;
    bool m_modified;
    bool m_exists;

    void read();

 public:
    /**
     * Open or create a new file. The file will be physically created
     * right away whereas changes to its content will not be written
     * immediately.
     *
     * @param path      node name, maps to directory
     * @param fileName  name of file inside that directory
     */
    FileConfigNode(const string &path, const string &fileName);

    virtual string getName() const { return m_path + "/" + m_fileName; }

    virtual void flush();
    virtual string readProperty(const string &property) const;
    virtual void setProperty(const string &property,
                             const string &value,
                             const string &comment = "",
                             const string *defValue = NULL);
    virtual void readProperties(map<string, string> &props) const;
    virtual void removeProperty(const string &property);
    virtual bool exists() const { return m_exists; }
};

#endif
