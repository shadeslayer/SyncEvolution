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
#include <list>

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
typedef map<string, string> StringMap;

/**
 * remove multiple slashes in a row and dots directly after a slash if not followed by filename,
 * remove trailing /
 */
string normalizePath(const string &path);

/**
 * Returns last component of path. Trailing slash is ignored.
 * Empty if path is empty.
 */
string getBasename(const string &path);

/**
 * Returns path without the last component. Empty if nothing left.
 */
string getDirname(const string &path);

/**
 * Splits path into directory and file part. Trailing slashes
 * are stripped first.
 */
void splitPath(const string &path, string &dir, string &file);

/**
 * convert relative path to canonicalized absolute path
 * @param path will be turned into absolute path if possible, otherwise left unchanged
 * @return true if conversion is successful, false otherwise(errno will be set)
 */
bool relToAbs(string &path);

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

/**
 * copy complete directory hierarchy
 *
 * If the source is a directory, then the target
 * also has to be a directory name. It will be
 * created if necessary.
 *
 * Alternatively, both names may refer to files.
 * In that case the directory which is going to
 * contain the target file must exist.
 *
 * @param from     source directory or file
 * @param to       target directory or file (must have same type as from)
 */
void cp_r(const string &from, const string &to);

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
bool ReadFile(istream &in, string &content);

enum ExecuteFlags {
    EXECUTE_NO_STDERR = 1<<0,       /**< suppress stderr of command */
    EXECUTE_NO_STDOUT = 1<<1        /**< suppress stdout of command */
};

/**
 * system() replacement
 *
 * If called without output redirection active (see LogRedirect),
 * then it will simply call system(). If output redirection is
 * active, the command is executed in a forked process without
 * blocking the parent process and the parent reads the output,
 * passing it through LogRedirect for processing.
 *
 * This is necessary to capture all output reliably: LogRedirect
 * ensures that we don't deadlock, but to achieve that, it drops
 * data when the child prints too much of it.
 *
 * @param cmd      command including parameters, without output redirection
 * @param flags    see ExecuteFlags
 * @return same as in system(): use WEXITSTATUS() et.al. to decode it
 */
int Execute(const std::string &cmd, ExecuteFlags flags) throw();

/**
 * Simple string hash function, derived from Dan Bernstein's algorithm.
 */
unsigned long Hash(const char *str);
unsigned long Hash(const std::string &str);

/**
 * SHA-256 implementation, returning hash as lowercase hex string (like sha256sum).
 * Might not be available, in which case it raises an exception.
 */
std::string SHA_256(const std::string &in);

/**
 * escape/unescape code
 *
 * Escaping is done URL-like, with a configurable escape
 * character. The exact set of characters to replace (besides the
 * special escape character) is configurable, too.
 *
 * The code used to be in SafeConfigNode, but is of general value.
 */
class StringEscape
{
 public:
    enum Mode {
        INI_VALUE,         /**< right hand side of .ini assignment:
                              escape all spaces at start and end (but not in the middle) and the equal sign */
        INI_WORD,          /**< same as before, but keep it one word:
                              escape all spaces and the equal sign = */
        STRICT             /**< general purpose:
                              escape all characters besides alphanumeric and -_ */
    };

 private:
    char m_escapeChar;
    Mode m_mode;

 public:
    /**
     * default constructor, using % as escape character, escaping all spaces (including
     * leading and trailing ones), and all characters besides alphanumeric and -_
     */
    StringEscape(char escapeChar = '%', Mode mode = STRICT) :
        m_escapeChar(escapeChar),
        m_mode(mode)
    {}

    /** special character which introduces two-char hex encoded original character */
    char getEscapeChar() const { return m_escapeChar; }
    void setEscapeChar(char escapeChar) { m_escapeChar = escapeChar; }

    Mode getMode() const { return m_mode; }
    void setMode(Mode mode) { m_mode = mode; }

    /**
     * escape string according to current settings
     */
    string escape(const string &str) const { return escape(str, m_escapeChar, m_mode); }

    /** escape string with the given settings */
    static string escape(const string &str, char escapeChar, Mode mode);

    /**
     * unescape string, with escape character as currently set
     */
    string unescape(const string &str) const { return unescape(str, m_escapeChar); }

    /**
     * unescape string, with escape character as given
     */
    static string unescape(const string &str, char escapeChar);
};

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
 * StatusException by wrapping a SyncML status
 */
class StatusException : public Exception
{
public:
    StatusException(const std::string &file,
                    int line,
                    const std::string &what,
                    SyncMLStatus status)
        : Exception(file, line, what), m_status(status)
    {}

    SyncMLStatus syncMLStatus() const { return m_status; }
protected:
    SyncMLStatus m_status;
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

/**
 * Parse a separator splitted set of strings src, the separator itself is
 * escaped by a backslash. Spaces around the separator is also stripped.
 * */
std::vector<std::string> unescapeJoinedString (const std::string &src, char separator);

/**
 * Temporarily set env variable, restore old value on destruction.
 * Useful for unit tests which depend on the environment.
 */
class ScopedEnvChange
{
 public:
    ScopedEnvChange(const string &var, const string &value);
    ~ScopedEnvChange();
 private:
    string m_var, m_oldval;
    bool m_oldvalset;
};

std::string getCurrentTime();

/** throw a normal SyncEvolution Exception, including source information */
#define SE_THROW(_what) \
    SE_THROW_EXCEPTION(Exception, _what)

/** throw a class which accepts file, line, what parameters */
#define SE_THROW_EXCEPTION(_class,  _what) \
    throw _class(__FILE__, __LINE__, _what)

/** throw a class which accepts file, line, what parameters and status parameters*/
#define SE_THROW_EXCEPTION_STATUS(_class,  _what, _status) \
    throw _class(__FILE__, __LINE__, _what, _status)

SE_END_CXX
#endif // INCL_SYNCEVOLUTION_UTIL
