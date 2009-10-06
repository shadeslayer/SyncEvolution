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

#ifndef INCL_SYNCEVOLUTION_UTIL
# define INCL_SYNCEVOLUTION_UTIL

#include <syncevo/SyncML.h>

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/function.hpp>

#include <stdarg.h>

#include <vector>
#include <sstream>
#include <string>
#include <utility>
#include <exception>

#include <syncevo/declarations.h>
SE_BEGIN_CXX
using namespace std;

class Logger;

/** case-insensitive less than for assoziative containers */
template <class T> class Nocase : public std::binary_function<T, T, bool> {
public:
    bool operator()(const T &x, const T &y) const { return boost::ilexicographical_compare(x, y); }
};

/** case-insensitive equals */
template <class T> class Iequals : public std::binary_function<T, T, bool> {
public:
    bool operator()(const T &x, const T &y) const { return boost::iequals(x, y); }
};

/** shorthand, primarily useful for BOOST_FOREACH macro */
typedef pair<string, string> StringPair;

/**
 * remove multiple slashes in a row and dots directly after a slash if not followed by filename,
 * remove trailing /
 */
string normalizePath(const string &path);

/** ensure that m_path is writable, otherwise throw error */
void mkdir_p(const string &path);

inline bool rm_r_all(const string &path, bool isDir) { return true; }

/**
 * remove a complete directory hierarchy; invoking on non-existant directory is okay
 * @param path     relative or absolute path to be removed
 * @param filter   an optional callback which determines whether an entry really is
 *                 to be deleted (return true in that case); called with full path
 *                 to entry and true if known to be a directory
 */
void rm_r(const string &path, boost::function<bool (const string &,
                                                    bool)> filter = rm_r_all);

/** true if the path refers to a directory */
bool isDir(const string &path);

/**
 * try to read a file into the given string, throw exception if fails
 *
 * @param filename     absolute or relative file name
 * @retval content     filled with file content
 * @return true if file could be read
 */
bool ReadFile(const string &filename, string &content);

/**
 * Simple string hash function, derived from Dan Bernstein's algorithm.
 */
unsigned long Hash(const char *str);

/**
 * This is a simplified implementation of a class representing and calculating
 * UUIDs v4 inspired from RFC 4122. We do not use cryptographic pseudo-random
 * numbers, instead we rely on rand/srand.
 *
 * We initialize the random generation with the system time given by time(), but
 * only once.
 *
 * Instantiating this class will generate a new unique UUID, available afterwards
 * in the base string class.
 */
class UUID : public string {
 public:
    UUID();
};

/**
 * A C++ wrapper around readir() which provides the names of all
 * directory entries, excluding . and ..
 *
 */
class ReadDir {
 public:
    ReadDir(const string &path, bool throwError = true);

    typedef vector<string>::const_iterator const_iterator;
    typedef vector<string>::iterator iterator;
    iterator begin() { return m_entries.begin(); }
    iterator end() { return m_entries.end(); }
    const_iterator begin() const { return m_entries.begin(); }
    const_iterator end() const { return m_entries.end(); }

    /**
     * check whether directory contains entry, returns full path
     * @param caseInsensitive    ignore case, pick first entry which matches randomly
     */
    string find(const string &entry, bool caseSensitive);

 private:
    string m_path;
    vector<string> m_entries;
};

/**
 * Using this macro ensures that tests, even if defined in
 * object files which are not normally linked into the test
 * binary, are included in the test suite under the group
 * "SyncEvolution".
 *
 * Use it like this:
 * @verbatim
   #include "config.h"
   #ifdef ENABLE_UNIT_TESTS
   # include "test.h"
   class Foo : public CppUnit::TestFixture {
       CPPUNIT_TEST_SUITE(foo);
       CPPUNIT_TEST(testBar);
       CPPUNIT_TEST_SUITE_END();

     public:
       void testBar();
   };
   # SYNCEVOLUTION_TEST_SUITE_REGISTRATION(classname)
   #endif
   @endverbatim
 */
#define SYNCEVOLUTION_TEST_SUITE_REGISTRATION( ATestFixtureType ) \
    CPPUNIT_TEST_SUITE_NAMED_REGISTRATION( ATestFixtureType, "SyncEvolution" ); \
    extern "C" { int funambolAutoRegisterRegistry ## ATestFixtureType = 12345; }

std::string StringPrintf(const char *format, ...)
#ifdef __GNUC__
        __attribute__((format(printf, 1, 2)))
#endif
;
std::string StringPrintfV(const char *format, va_list ap);

/**
 * an exception which records the source file and line
 * where it was thrown
 *
 * @TODO add function name
 */
class Exception : public std::runtime_error
{
 public:
    Exception(const std::string &file,
                           int line,
                           const std::string &what) :
    std::runtime_error(what),
        m_file(file),
        m_line(line)
        {}
    ~Exception() throw() {}
    const std::string m_file;
    const int m_line;

    /**
     * Convenience function, to be called inside a catch(..) block.
     *
     * Rethrows the exception to determine what it is, then logs it as
     * an error. Turns certain known exceptions into the corresponding
     * status code if status still was STATUS_OK when called.
     * Returns updated status code.
     *
     * @param logger    the class which does the logging
     */
    static SyncMLStatus handle(SyncMLStatus *status = NULL, Logger *logger = NULL);
    static SyncMLStatus handle(Logger *logger) { return handle(NULL, logger); }
};

/**
 * replace ${} with environment variables, with
 * XDG_DATA_HOME, XDG_CACHE_HOME and XDG_CONFIG_HOME having their normal
 * defaults
 */
std::string SubstEnvironment(const std::string &str);

inline string getHome() {
    const char *homestr = getenv("HOME");
    return homestr ? homestr : ".";
}

/** throw a normal SyncEvolution Exception, including source information */
#define SE_THROW(_what) \
    SE_THROW_EXCEPTION(Exception, _what)

/** throw a class which accepts file, line, what parameters */
#define SE_THROW_EXCEPTION(_class,  _what) \
    throw _class(__FILE__, __LINE__, _what)


SE_END_CXX
#endif // INCL_SYNCEVOLUTION_UTIL
