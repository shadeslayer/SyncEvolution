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

#include <config.h>
#include "test.h"
#include <syncevo/SingleFileConfigTree.h>
#include <syncevo/StringDataBlob.h>
#include <syncevo/FileDataBlob.h>
#include <syncevo/IniConfigNode.h>
#include <syncevo/util.h>

#include <boost/foreach.hpp>
#include <boost/algorithm/string.hpp>

#include <syncevo/declarations.h>
SE_BEGIN_CXX
using namespace std;

SingleFileConfigTree::SingleFileConfigTree(const boost::shared_ptr<DataBlob> &data) :
    m_data(data)
{
    readFile();
}

SingleFileConfigTree::SingleFileConfigTree(const string &fullpath) :
    m_data(new FileDataBlob(fullpath, true))
{
    readFile();
}

boost::shared_ptr<ConfigNode> SingleFileConfigTree::open(const string &filename)
{
    string normalized = normalizePath(string("/") + filename);
    boost::shared_ptr<ConfigNode> &entry = m_nodes[normalized];
    if (entry) {
        return entry;
    }

    string name = getRootPath() + " - " + normalized;
    boost::shared_ptr<DataBlob> data; 

    BOOST_FOREACH(const FileContent_t::value_type &file, m_content) {
        if (file.first == normalized) {
            data.reset(new StringDataBlob(name, file.second, true));
            break;
        }
    }
    if (!data) {
        /*
         * creating new files not supported, would need support for detecting
         * StringDataBlob::write()
         */
        data.reset(new StringDataBlob(name, boost::shared_ptr<std::string>(), true));
    }
    entry.reset(new IniFileConfigNode(data));
    return entry;
}

void SingleFileConfigTree::flush()
{
    // not implemented, cannot write anyway
}

void SingleFileConfigTree::reload()
{
    SE_THROW("SingleFileConfigTree::reload() not implemented");
}

void SingleFileConfigTree::remove(const string &path)
{
    SE_THROW("internal error: SingleFileConfigTree::remove() called");
}

void SingleFileConfigTree::reset()
{
    m_nodes.clear();
    readFile();
}

boost::shared_ptr<ConfigNode> SingleFileConfigTree::open(const string &path,
                                                         PropertyType type,
                                                         const string &otherId)
{
    string fullpath = path;
    if (!fullpath.empty()) {
        fullpath += "/";
    }
    switch (type) {
    case visible:
        fullpath += "config.ini";
        break;
    case hidden:
        fullpath += ".internal.ini";
        break;
    case other:
        fullpath += ".other.ini";
        break;
    case server:
        fullpath += ".server.ini";
        break;
    }
    
    return open(fullpath);
}

boost::shared_ptr<ConfigNode> SingleFileConfigTree::add(const string &path,
                                                        const boost::shared_ptr<ConfigNode> &bode)
{
    SE_THROW("SingleFileConfigTree::add() not supported");
}


static void checkChild(const string &normalized,
                       const string &node,
                       set<string> &subdirs)
{
    if (boost::starts_with(node, normalized)) {
        string remainder = node.substr(normalized.size());
        size_t offset = remainder.find('/');
        if (offset != remainder.npos) {
            // only directories underneath path matter
            subdirs.insert(remainder.substr(0, offset));
        }
    }
}

list<string> SingleFileConfigTree::getChildren(const string &path)
{
    set<string> subdirs;
    string normalized = normalizePath(string("/") + path);
    if (normalized != "/") {
        normalized += "/";
    }

    // must check both actual files as well as unsaved nodes
    BOOST_FOREACH(const FileContent_t::value_type &file, m_content) {
        checkChild(normalized, file.first, subdirs);
    }
    BOOST_FOREACH(const NodeCache_t::value_type &file, m_nodes) {
        checkChild(normalized, file.first, subdirs);
    }

    list<string> result;
    BOOST_FOREACH(const string &dir, subdirs) {
        result.push_back(dir);
    }
    return result;
}

void SingleFileConfigTree::readFile()
{
    boost::shared_ptr<istream> in(m_data->read());
    boost::shared_ptr<string> content;
    string line;

    m_content.clear();
    while (getline(*in, line)) {
        if (boost::starts_with(line, "=== ") &&
            boost::ends_with(line, " ===")) {
            string name = line.substr(4, line.size() - 8);
            name = normalizePath(string("/") + name);
            content.reset(new string);
            m_content[name] = content;
        } else if (content) {
            (*content) += line;
            (*content) += "\n";
        }
    }
}

#ifdef ENABLE_UNIT_TESTS

class SingleIniTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(SingleIniTest);
    CPPUNIT_TEST(simple);
    CPPUNIT_TEST_SUITE_END();

    void simple() {
        boost::shared_ptr<string> data(new string);
        data->assign("# comment\n"
                     "# foo\n"
                     "=== foo/config.ini ===\n"
                     "foo = bar\n"
                     "foo2 = bar2\n"
                     "=== foo/.config.ini ===\n"
                     "foo_internal = bar_internal\n"
                     "foo2_internal = bar2_internal\n"
                     "=== /bar/.internal.ini ===\n"
                     "bar = foo\n"
                     "=== sources/addressbook/config.ini ===\n"
                     "=== sources/calendar/config.ini ===\n"
                     "evolutionsource = Personal\n");
        boost::shared_ptr<DataBlob> blob(new StringDataBlob("test", data, true));
        SingleFileConfigTree tree(blob);
        boost::shared_ptr<ConfigNode> node;
        node = tree.open("foo/config.ini");
        CPPUNIT_ASSERT(node);
        CPPUNIT_ASSERT(node->exists());
        CPPUNIT_ASSERT_EQUAL(string("test - /foo/config.ini"), node->getName());
        CPPUNIT_ASSERT(node->readProperty("foo").wasSet());
        CPPUNIT_ASSERT_EQUAL(string("bar"), node->readProperty("foo").get());
        CPPUNIT_ASSERT_EQUAL(string("bar2"), node->readProperty("foo2").get());
        CPPUNIT_ASSERT(node->readProperty("foo2").wasSet());
        CPPUNIT_ASSERT_EQUAL(string(""), node->readProperty("no_such_bar").get());
        CPPUNIT_ASSERT(!node->readProperty("no_such_bar").wasSet());
        node = tree.open("/foo/config.ini");
        CPPUNIT_ASSERT(node);
        CPPUNIT_ASSERT(node->exists());
        node = tree.open("foo//.config.ini");
        CPPUNIT_ASSERT(node);
        CPPUNIT_ASSERT(node->exists());
        CPPUNIT_ASSERT_EQUAL(string("bar_internal"), node->readProperty("foo_internal").get());
        CPPUNIT_ASSERT_EQUAL(string("bar2_internal"), node->readProperty("foo2_internal").get());
        node = tree.open("bar///./.internal.ini");
        CPPUNIT_ASSERT(node);
        CPPUNIT_ASSERT(node->exists());
        CPPUNIT_ASSERT_EQUAL(string("foo"), node->readProperty("bar").get());
        node = tree.open("sources/addressbook/config.ini");
        CPPUNIT_ASSERT(node);
        CPPUNIT_ASSERT(node->exists());
        node = tree.open("sources/calendar/config.ini");
        CPPUNIT_ASSERT(node);
        CPPUNIT_ASSERT(node->exists());
        CPPUNIT_ASSERT_EQUAL(string("Personal"), node->readProperty("evolutionsource").get());

        node = tree.open("no-such-source/config.ini");
        CPPUNIT_ASSERT(node);
        CPPUNIT_ASSERT(!node->exists());

        list<string> dirs = tree.getChildren("");
        CPPUNIT_ASSERT_EQUAL(string("bar|foo|no-such-source|sources"), boost::join(dirs, "|"));
        dirs = tree.getChildren("sources/");
        CPPUNIT_ASSERT_EQUAL(string("addressbook|calendar"), boost::join(dirs, "|"));
    }
};

SYNCEVOLUTION_TEST_SUITE_REGISTRATION(SingleIniTest);

#endif // ENABLE_UNIT_TESTS


SE_END_CXX
