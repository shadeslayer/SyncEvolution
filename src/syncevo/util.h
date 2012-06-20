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
#include <boost/utility/value_init.hpp>

#include <stdarg.h>

#include <vector>
#include <sstream>
#include <string>
#include <utility>
#include <exception>
#include <list>

#include <syncevo/Timespec.h>    // definitions used to be included in util.h,
                                 // include it to avoid changing code using the time things
#include <syncevo/Logging.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

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
typedef std::pair<std::string, std::string> StringPair;
typedef std::map<std::string, std::string> StringMap;

/**
 * remove multiple slashes in a row and dots directly after a slash if not followed by filename,
 * remove trailing /
 */
std::string normalizePath(const std::string &path);

/**
 * Returns last component of path. Trailing slash is ignored.
 * Empty if path is empty.
 */
std::string getBasename(const std::string &path);

/**
 * Returns path without the last component. Empty if nothing left.
 */
std::string getDirname(const std::string &path);

/**
 * Splits path into directory and file part. Trailing slashes
 * are stripped first.
 */
void splitPath(const std::string &path, std::string &dir, std::string &file);

/**
 * convert relative path to canonicalized absolute path
 * @param path will be turned into absolute path if possible, otherwise left unchanged
 * @return true if conversion is successful, false otherwise(errno will be set)
 */
bool relToAbs(std::string &path);

/** ensure that m_path is writable, otherwise throw error */
void mkdir_p(const std::string &path);

inline bool rm_r_all(const std::string &path, bool isDir) { return true; }

/**
 * remove a complete directory hierarchy; invoking on non-existant directory is okay
 * @param path     relative or absolute path to be removed
 * @param filter   an optional callback which determines whether an entry really is
 *                 to be deleted (return true in that case); called with full path
 *                 to entry and true if known to be a directory
 */
void rm_r(const std::string &path, boost::function<bool (const std::string &,
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
void cp_r(const std::string &from, const std::string &to);

/** true if the path refers to a directory */
bool isDir(const std::string &path);

/**
 * try to read a file into the given string, throw exception if fails
 *
 * @param filename     absolute or relative file name
 * @retval content     filled with file content
 * @return true if file could be read
 */
bool ReadFile(const std::string &filename, std::string &content);
bool ReadFile(std::istream &in, std::string &content);

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
        SET,               /**< explicit list of characters to be escaped */
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
    std::set<char> m_forbidden;

 public:
    /**
     * default constructor, using % as escape character, escaping all spaces (including
     * leading and trailing ones), and all characters besides alphanumeric and -_
     */
    StringEscape(char escapeChar = '%', Mode mode = STRICT) :
        m_escapeChar(escapeChar),
        m_mode(mode)
    {}

    /**
     * @param escapeChar        character used to introduce escape sequence
     * @param forbidden         explicit list of characters which are to be escaped
     */
    StringEscape(char escapeChar, const char *forbidden);

    /** special character which introduces two-char hex encoded original character */
    char getEscapeChar() const { return m_escapeChar; }
    void setEscapeChar(char escapeChar) { m_escapeChar = escapeChar; }

    Mode getMode() const { return m_mode; }
    void setMode(Mode mode) { m_mode = mode; }

    /**
     * escape string according to current settings
     */
    std::string escape(const std::string &str) const;

    /** escape string with the given settings */
    static std::string escape(const std::string &str, char escapeChar, Mode mode);

    /**
     * unescape string, with escape character as currently set
     */
    std::string unescape(const std::string &str) const { return unescape(str, m_escapeChar); }

    /**
     * unescape string, with escape character as given
     */
    static std::string unescape(const std::string &str, char escapeChar);
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
class UUID : public std::string {
 public:
    UUID();
};

/**
 * Safety check for string pointer.
 * Returns pointer if valid, otherwise the default string.
 */
inline const char *NullPtrCheck(const char *ptr, const char *def = "(null)")
{
    return ptr ? ptr : def;
}

/**
 * A C++ wrapper around readir() which provides the names of all
 * directory entries, excluding . and ..
 *
 */
class ReadDir {
 public:
    ReadDir(const std::string &path, bool throwError = true);

    typedef std::vector<std::string>::const_iterator const_iterator;
    typedef std::vector<std::string>::iterator iterator;
    iterator begin() { return m_entries.begin(); }
    iterator end() { return m_entries.end(); }
    const_iterator begin() const { return m_entries.begin(); }
    const_iterator end() const { return m_entries.end(); }

    /**
     * check whether directory contains entry, returns full path
     * @param caseInsensitive    ignore case, pick first entry which matches randomly
     */
    std::string find(const std::string &entry, bool caseSensitive);

 private:
    std::string m_path;
    std::vector<std::string> m_entries;
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
 * strncpy() which inserts adds 0 byte
 */
char *Strncpy(char *dest, const char *src, size_t n);

/**
 * sleep() with sub-second resolution. Might be interrupted by signals
 * or SuspendFlags abort/suspend requests before the time has elapsed.
 *
 * @return seconds not elapsed yet, 0 if not interrupted
 */
double Sleep(double seconds);

/**
 * Acts like the underlying type. In addition ensures that plain types
 * are not left uninitialized.
 */
template<class T> class Init {
 public:
    Init(const T &val) : m_value(val) {}
    Init() : m_value(boost::value_initialized<T>()) {}
    Init(const Init &other) : m_value(other.m_value) {}
    Init & operator = (const T &val) { m_value = val; return *this; }
    operator const T & () const { return m_value; }
    operator T & () { return m_value; }
 private:
    T m_value;
};


/**
 * Version of InitState for scalar values (can't derive from them):
 * acts like the underlying type. In addition ensures that plain types
 * are not left uninitialized and tracks whether a value was every
 * assigned explicitly.
 */
template<class T> class InitState {
 public:
    typedef T value_type;

    InitState(const T &val, bool wasSet) : m_value(val), m_wasSet(wasSet) {}
    InitState() : m_value(boost::value_initialized<T>()), m_wasSet(false) {}
    InitState(const InitState &other) : m_value(other.m_value), m_wasSet(other.m_wasSet) {}
    InitState & operator = (const T &val) { m_value = val; m_wasSet = true; return *this; }
    operator const T & () const { return m_value; }
    operator T & () { return m_value; }
    const T & get() const { return m_value; }
    T & get() { return m_value; }
    bool wasSet() const { return m_wasSet; }
 private:
    T m_value;
    bool m_wasSet;
};

/** version of InitState for classes */
template<class T> class InitStateClass : public T {
 public:
    typedef T value_type;

    InitStateClass(const T &val, bool wasSet) : T(val), m_wasSet(wasSet) {}
    InitStateClass() : m_wasSet(false) {}
    InitStateClass(const char *val) : T(val), m_wasSet(false) {}
    InitStateClass(const InitStateClass &other) : T(other), m_wasSet(other.m_wasSet) {}
    InitStateClass & operator = (const T &val) { T::operator = (val); m_wasSet = true; return *this; }
    const T & get() const { return *this; }
    T & get() { return *this; }
    bool wasSet() const { return m_wasSet; }
 private:
    bool m_wasSet;
};

/**
 * a nop destructor which doesn't do anything, for boost::shared_ptr
 */
struct NopDestructor
{
    template <class T> void operator () (T *) {}
};

/**
 * Acts like a boolean, but in addition, can also tell whether the
 * value was explicitly set. Defaults to false for both.
 */
typedef InitState<bool> Bool;

/**
 * Acts like a string, but in addition, can also tell whether the
 * value was explicitly set.
 */
typedef InitStateClass<std::string> InitStateString;

/**
 * Version of InitState where the value can true, false, or a string.
 * Recognizes 0/1/false/true/no/yes case-insensitively as special
 * booleans, everything else is considered a string.
 */
class InitStateTri : public InitStateString
{
 public:
    InitStateTri(const std::string &val, bool wasSet) : InitStateString(val, wasSet) {}
    InitStateTri() {}
    InitStateTri(const char *val) : InitStateString(val, false) {}
    InitStateTri(const InitStateTri &other) : InitStateString(other) {}
    InitStateTri(const InitStateString &other) : InitStateString(other) {}

    enum Value {
        VALUE_TRUE,
        VALUE_FALSE,
        VALUE_STRING
    };

    // quick check for true/false, use get() for string case
    Value getValue() const;
};

enum HandleExceptionFlags {
    HANDLE_EXCEPTION_FLAGS_NONE = 0,

    /**
     * a 404 status error is possible and must not be logged as ERROR
     */
    HANDLE_EXCEPTION_404_IS_OKAY = 1 << 0,
    HANDLE_EXCEPTION_FATAL = 1 << 1,
    /**
     * don't log exception as ERROR
     */
    HANDLE_EXCEPTION_NO_ERROR = 1 << 2,
    HANDLE_EXCEPTION_MAX = 1 << 3,
};

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
     * Rethrows the exception to determine what it is, then logs it
     * at the chosen level (error by default).
     *
     * Turns certain known exceptions into the corresponding
     * status code if status still was STATUS_OK when called.
     * Returns updated status code.
     *
     * @param logger    the class which does the logging
     * @retval explanation   set to explanation for problem, if non-NULL
     * @param level     level to be used for logging
     */
    static SyncMLStatus handle(SyncMLStatus *status = NULL, Logger *logger = NULL, std::string *explanation = NULL, Logger::Level = Logger::ERROR, HandleExceptionFlags flags = HANDLE_EXCEPTION_FLAGS_NONE);
    static SyncMLStatus handle(Logger *logger, HandleExceptionFlags flags = HANDLE_EXCEPTION_FLAGS_NONE) { return handle(NULL, logger, NULL, Logger::ERROR, flags); }
    static SyncMLStatus handle(std::string &explanation, HandleExceptionFlags flags = HANDLE_EXCEPTION_FLAGS_NONE) { return handle(NULL, NULL, &explanation, Logger::ERROR, flags); }
    static void handle(HandleExceptionFlags flags) { handle(NULL, NULL, NULL, Logger::ERROR, flags); }
    static void log() { handle(NULL, NULL, NULL, Logger::DEBUG); }

    /**
     * Tries to identify exception class based on explanation string created by
     * handle(). If successful, that exception is throw with the same
     * attributes as in the original exception. Otherwise parse() returns.
     */
    static void tryRethrow(const std::string &explanation);

    /**
     * Same as tryRethrow() for strings with a 'org.syncevolution.xxxx:' prefix,
     * as passed as D-Bus error strings.
     */
    static void tryRethrowDBus(const std::string &error);
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

class TransportException : public Exception
{
 public:
    TransportException(const std::string &file,
                       int line,
                       const std::string &what) :
    Exception(file, line, what) {}
    ~TransportException() throw() {}
};

class TransportStatusException : public StatusException
{
 public:
    TransportStatusException(const std::string &file,
                             int line,
                             const std::string &what,
                             SyncMLStatus status) :
    StatusException(file, line, what, status) {}
    ~TransportStatusException() throw() {}
};

/**
 * replace ${} with environment variables, with
 * XDG_DATA_HOME, XDG_CACHE_HOME and XDG_CONFIG_HOME having their normal
 * defaults
 */
std::string SubstEnvironment(const std::string &str);

/** getenv() with default value */
inline const char *getEnv(const char *var, const char *def)
{
    const char *res = getenv(var);
    return res ? res : def;
}

inline std::string getHome() { return getEnv("HOME", "."); }

/**
 * Parse a separator splitted set of strings src, the separator itself is
 * escaped by a backslash. Spaces around the separator is also stripped.
 * */
std::vector<std::string> unescapeJoinedString (const std::string &src, char separator);

/**
 * mapping from int flag to explanation
 */
struct Flag {
    int m_flag;
    const char *m_description;
};

/**
 * turn flags into comma separated list of explanations
 *
 * @param flags     bit mask
 * @param descr     array with zero m_flag as end marker
 * @param sep       used to join m_description strings
 */
std::string Flags2String(int flags, const Flag *descr, const std::string &sep = ", ");

/**
 * Returns the path to the data directory. This is generally
 * /usr/share/syncevolution/ but can be overridden by setting the
 * SYNCEVOLUTION_DATA_DIR environment variable.
 *
 * @retval dataDir the path to the data directory
 */
std::string SyncEvolutionDataDir();

/**
 * Temporarily set env variable, restore old value on destruction.
 * Useful for unit tests which depend on the environment.
 */
class ScopedEnvChange
{
 public:
    ScopedEnvChange(const std::string &var, const std::string &value);
    ~ScopedEnvChange();
 private:
    std::string m_var, m_oldval;
    bool m_oldvalset;
};

std::string getCurrentTime();

/** throw a normal SyncEvolution Exception, including source information */
#define SE_THROW(_what) \
    SE_THROW_EXCEPTION(Exception, _what)

/** throw a class which accepts file, line, what parameters */
#define SE_THROW_EXCEPTION(_class,  _what) \
    throw _class(__FILE__, __LINE__, _what)

/** throw a class which accepts file, line, what plus 1 additional parameter */
#define SE_THROW_EXCEPTION_1(_class,  _what, _x1)   \
    throw _class(__FILE__, __LINE__, (_what), (_x1))

/** throw a class which accepts file, line, what plus 2 additional parameters */
#define SE_THROW_EXCEPTION_2(_class,  _what, _x1, _x2) \
    throw _class(__FILE__, __LINE__, (_what), (_x1), (_x2))

/** throw a class which accepts file, line, what plus 2 additional parameters */
#define SE_THROW_EXCEPTION_3(_class,  _what, _x1, _x2, _x3) \
    throw _class(__FILE__, __LINE__, (_what), (_x1), (_x2), (_x3))

/** throw a class which accepts file, line, what plus 2 additional parameters */
#define SE_THROW_EXCEPTION_4(_class,  _what, _x1, _x2, _x3, _x4) \
    throw _class(__FILE__, __LINE__, (_what), (_x1), (_x2), (_x3), (_x4))

/** throw a class which accepts file, line, what parameters and status parameters*/
#define SE_THROW_EXCEPTION_STATUS(_class,  _what, _status) \
    throw _class(__FILE__, __LINE__, _what, _status)

SE_END_CXX
#endif // INCL_SYNCEVOLUTION_UTIL
