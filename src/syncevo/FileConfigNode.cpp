/*
 * Copyright (C) 2008-2009 Patrick Ohly <patrick.ohly@gmx.de>
 * Copyright (C) 2009 Intel Corporation
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

#include <syncevo/FileConfigNode.h>
#include <syncevo/SafeConfigNode.h>
#include <syncevo/SyncContext.h>
#include <syncevo/util.h>

#include <boost/scoped_array.hpp>
#include <boost/foreach.hpp>

#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

/** @TODO: replace stdio.h with streams */

boost::shared_ptr<ConfigNode> ConfigNode::createFileNode(const string &filename)
{
    string::size_type off = filename.rfind('/');
    boost::shared_ptr<ConfigNode> filenode;
    if (off != filename.npos) {
        filenode.reset(new FileConfigNode(filename.substr(0, off),
                                          filename.substr(off + 1),
                                          false));
    } else {
        filenode.reset(new FileConfigNode(".", filename, false));
    }
    boost::shared_ptr<SafeConfigNode> savenode(new SafeConfigNode(filenode));
    savenode->setMode(false);
    return savenode;
}

FileBaseConfigNode::FileBaseConfigNode(const string &path, const string &fileName, bool readonly) :
    m_path(path),
    m_fileName(fileName),
    m_modified(false),
    m_readonly(readonly),
    m_exists(false) 
{
}

void FileBaseConfigNode::flush()
{
    if (!m_modified) {
        return;
    }

    if (m_readonly) {
        throw std::runtime_error(m_path + "/" + m_fileName + ": internal error: flushing read-only file config node not allowed");
    }

    mkdir_p(m_path);

    string filename = m_path + "/" + m_fileName;
    string tmpFilename = m_path + "/.#" + m_fileName;

    FILE *file = fopen(tmpFilename.c_str(), "w");
    if (file) {
        toFile(file);
        fflush(file);
        bool failed = ferror(file);
        if (fclose(file)) {
            failed = true;
        }
        if (failed ||
            rename(tmpFilename.c_str(), filename.c_str())) {
            SyncContext::throwError(tmpFilename, errno);
        }
    } else {
        SyncContext::throwError(tmpFilename, errno);
    }

    m_modified = false;
    m_exists = true;
}

FileConfigNode::FileConfigNode(const string &path, const string &fileName, bool readonly) :
    FileBaseConfigNode(path,fileName,readonly)
{
    read();
}

void FileConfigNode::toFile(FILE* file) {
    BOOST_FOREACH(const string &line, m_lines) {
        fprintf(file, "%s\n", line.c_str());
    }
}

void FileConfigNode::read()
{
    string filename = m_path + "/" + m_fileName;

    FILE *file = fopen(filename.c_str(), "r");
    char buffer[512];

    m_lines.clear();
    if (file) {
        /** add check to avoid errors when a line is larger than 512 bytes */
        string line;
        while (fgets(buffer, sizeof(buffer), file)) {
            char *eol = strchr(buffer, '\n');
            if (eol) {
                *eol = 0;
                line += buffer;
            } else {
                line += buffer;
                continue;
            }
            m_lines.push_back(line);
            line = "";
        }
        m_exists = true;
        fclose(file);
    }
    m_modified = false;
}


/**
 * get property and value from line, if any present
 */
static bool getContent(const string &line,
                       string &property,
                       string &value,
                       bool &isComment,
                       bool fuzzyComments)
{
    size_t start = 0;
    while (start < line.size() &&
           isspace(line[start])) {
        start++;
    }

    // empty line?
    if (start == line.size()) {
        return false;
    }

    // Comment? Potentially keep reading, might be commented out assignment.
    isComment = false;
    if (line[start] == '#') {
        if (!fuzzyComments) {
            return false;
        }
        isComment = true;
    }

    // recognize # <word> = <value> as commented out (= default) value
    if (isComment) {
        start++;
        while (start < line.size() &&
               isspace(line[start])) {
            start++;
        }
    }

    // extract property
    size_t end = start;
    while (end < line.size() &&
           !isspace(line[end])) {
        end++;
    }
    property = line.substr(start, end - start);

    // skip assignment 
    start = end;
    while (start < line.size() &&
           isspace(line[start])) {
        start++;
    }
    if (start == line.size() ||
        line[start] != '=') {
        // invalid syntax or we tried to read a comment as assignment
        return false;
    }

    // extract value
    start++;
    while (start < line.size() &&
           isspace(line[start])) {
        start++;
    }

    value = line.substr(start);
    // remove trailing white space: usually it is
    // added accidentally by users
    size_t numspaces = 0;
    while (numspaces < value.size() &&
           isspace(value[value.size() - 1 - numspaces])) {
        numspaces++;
    }
    value.erase(value.size() - numspaces);

    // @TODO: strip quotation marks around value?!
    
    return true;    
}

/**
 * check whether the line contains the property and if so, extract its value
 */
static bool getValue(const string &line,
                     const string &property,
                     string &value,
                     bool &isComment,
                     bool fuzzyComments)

{
    string curProp;
    return getContent(line, curProp, value, isComment, fuzzyComments) &&
        !strcasecmp(curProp.c_str(), property.c_str());
}

string FileConfigNode::readProperty(const string &property) const {
    string value;

    BOOST_FOREACH(const string &line, m_lines) {
        bool isComment;

        if (getValue(line, property, value, isComment, false)) {
            return value;
        }
    }
    return "";
}



void FileConfigNode::readProperties(map<string, string> &props) const {
    map<string, string> res;
    string value, property;

    BOOST_FOREACH(const string &line, m_lines) {
        bool isComment;
        if (getContent(line, property, value, isComment, false)) {
            // don't care about the result: only the first instance
            // of the property counts, so it doesn't matter when
            // inserting it again later fails
            props.insert(pair<string, string>(property, value));
        }
    }
}

void FileConfigNode::removeProperty(const string &property)
{
    string value;

    list<string>::iterator it = m_lines.begin();
    while (it != m_lines.end()) {
        const string &line = *it;
        bool isComment;
        if (getValue(line, property, value, isComment, false)) {
            it = m_lines.erase(it);
            m_modified = true;
        } else {
            it++;
        }
    }
}

void FileConfigNode::setProperty(const string &property,
                                 const string &newvalue,
                                 const string &comment,
                                 const string *defValue) {
    string newstr;
    string oldvalue;
    bool isDefault = false;

    if (defValue &&
        *defValue == newvalue) {
        newstr += "# ";
        isDefault = true;
    }
    newstr += property + " = " + newvalue;

    BOOST_FOREACH(string &line, m_lines) {
        bool isComment;

        if (getValue(line, property, oldvalue, isComment, true)) {
            if (newvalue != oldvalue ||
                (isComment && !isDefault)) {
                line = newstr;
                m_modified = true;
            }
            return;
        }
    }

    // add each line of the comment as separate line in .ini file
    if (comment.size()) {
        list<string> commentLines;
        ConfigProperty::splitComment(comment, commentLines);
        if (m_lines.size()) {
            m_lines.push_back("");
        }
        BOOST_FOREACH(const string &comment, commentLines) {
            m_lines.push_back(string("# ") + comment);
        }
    }

    m_lines.push_back(newstr);
    m_modified = true;
}

HashFileConfigNode::HashFileConfigNode(const string &path, const string &fileName, bool readonly) :
    FileBaseConfigNode(path,fileName,readonly)
{
    read();
}

void HashFileConfigNode::read()
{
    string filename = m_path + "/" + m_fileName;

    FILE *file = fopen(filename.c_str(), "r");
    char buffer[512];

    if (file) {
        string line;
        while (fgets(buffer, sizeof(buffer), file)) {
            char *eol = strchr(buffer, '\n');
            if (eol) {
                *eol = 0;
                line += buffer;
            }else{
                line += buffer;
                continue;
            }
            string property, value;
            bool isComment;
            if (getContent(line, property, value, isComment, false)) {
                m_props.insert(StringPair(property, value));
            }
            line = "";
        }
        m_exists = true;
        fclose(file);
    }
    m_modified = false;
}

void HashFileConfigNode::toFile(FILE* file) {
    BOOST_FOREACH(const StringPair &prop, m_props) {
        fprintf(file, "%s = %s\n", prop.first.c_str(),prop.second.c_str());
    }
}

void HashFileConfigNode::readProperties(map<string, string> &props) const {
    BOOST_FOREACH(const StringPair &prop, m_props) {
        props.insert(prop);
    }
}

string HashFileConfigNode::readProperty(const string &property) const {
    std::map<std::string, std::string>::const_iterator it = m_props.find(property);
    if (it != m_props.end()) {
        return it->second;
    } else {
        return "";
    }
}

void HashFileConfigNode::removeProperty(const string &property){
    map<string, string>::iterator it = m_props.find(property);
    if(it != m_props.end()) {
        m_props.erase(it);
        m_modified = true;
    }
}

void HashFileConfigNode::setProperty(const string &property,
                                 const string &newvalue,
                                 const string &comment,
                                 const string *defValue) {
    /** we don't support property comments here. Also, we ignore comment*/
    if (defValue &&
        *defValue == newvalue) {
        return;
    }
    map<string, string>::iterator it = m_props.find(property);
    if(it != m_props.end()) {
        string oldvalue = it->second;
        if(oldvalue != newvalue) {
            m_props.erase(it);
            m_props.insert(StringPair(property, newvalue));
            m_modified = true;
        }
    } else {
        m_props.insert(StringPair(property, newvalue));
        m_modified = true;
    }
}


SE_END_CXX
