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

#include "config.h"
#include <syncevo/util.h>
#include <syncevo/SyncContext.h>
#include <syncevo/TransportAgent.h>
#include <syncevo/SynthesisEngine.h>
#include <syncevo/Logging.h>
#include <syncevo/LogRedirect.h>

#include <synthesis/syerror.h>

#include <boost/scoped_array.hpp>
#include <boost/foreach.hpp>
#include <boost/algorithm/string/join.hpp>
#include <fstream>
#include <iostream>

#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <dirent.h>
#include <limits.h>
#include <stdlib.h>
#include <math.h>

#if USE_SHA256 == 1
# include <glib.h>
#elif USE_SHA256 == 2
# include <nss/sechash.h>
# include <nss/hasht.h>
# include <nss.h>
#endif

#ifdef ENABLE_UNIT_TESTS
#include "test.h"
CPPUNIT_REGISTRY_ADD_TO_DEFAULT("SyncEvolution");
#endif

#include <syncevo/declarations.h>
SE_BEGIN_CXX

string normalizePath(const string &path)
{
    string res;

    res.reserve(path.size());
    size_t index = 0;
    while (index < path.size()) {
        char curr = path[index];
        res += curr;
        index++;
        if (curr == '/') {
            while (index < path.size() &&
                   (path[index] == '/' ||
                    (path[index] == '.' &&
                     index + 1 < path.size() &&
                     path[index + 1] == '/'))) {
                index++;
            }
        }
    }
    if (!res.empty() && res[res.size() - 1] == '/') {
        res.resize(res.size() - 1);
    }
    return res;
}

string getBasename(const string &path)
{
    string dir;
    string file;
    splitPath(path, dir, file);
    return file;
}

string getDirname(const string &path)
{
    string dir;
    string file;
    splitPath(path, dir, file);
    return dir;
}

void splitPath(const string &path, string &dir, string &file)
{
    string normal = normalizePath(path);
    size_t offset = normal.rfind('/');
    if (offset != normal.npos) {
        dir = normal.substr(0, offset);
        file = normal.substr(offset + 1);
    } else {
        dir = "";
        file = normal;
    }
}

bool relToAbs(string &path)
{
    char *buffer;
    if ((buffer = realpath(path.c_str(), NULL)) != NULL) {
        path = buffer;
	free(buffer);
        return true;
    } else {
        return false; 
    }
}

void mkdir_p(const string &path)
{
    boost::scoped_array<char> dirs(new char[path.size() + 1]);
    char *curr = dirs.get();
    strcpy(curr, path.c_str());
    do {
        char *nextdir = strchr(curr, '/');
        if (nextdir) {
            *nextdir = 0;
            nextdir++;
        }
        if (*curr) {
            if (access(dirs.get(),
                       nextdir ? (R_OK|X_OK) : (R_OK|X_OK|W_OK)) &&
                (errno != ENOENT ||
                 mkdir(dirs.get(), 0700))) {
                SyncContext::throwError(string(dirs.get()), errno);
            }
        }
        if (nextdir) {
            nextdir[-1] = '/';
        }
        curr = nextdir;
    } while (curr);
}

void rm_r(const string &path, boost::function<bool (const string &,
                                                    bool)> filter)
{
    struct stat buffer;
    if (lstat(path.c_str(), &buffer)) {
        if (errno == ENOENT) {
            return;
        } else {
            SyncContext::throwError(path, errno);
        }
    }

    if (!S_ISDIR(buffer.st_mode)) {
        if (!filter(path, false) ||
            !unlink(path.c_str())) {
            return;
        } else {
            SyncContext::throwError(path, errno);
        }
    }

    ReadDir dir(path);
    BOOST_FOREACH(const string &entry, dir) {
        rm_r(path + "/" + entry, filter);
    }
    if (filter(path, true) &&
        rmdir(path.c_str())) {
        SyncContext::throwError(path, errno);
    }
}

void cp_r(const string &from, const string &to)
{
    if (isDir(from)) {
        mkdir_p(to);
        ReadDir dir(from);
        BOOST_FOREACH(const string &entry, dir) {
            cp_r(from + "/" + entry, to + "/" + entry);
        }
    } else {
        ofstream out;
        ifstream in;
        out.open(to.c_str());
        in.open(from.c_str());
        char buf[8192];
        do {
            in.read(buf, sizeof(buf));
            out.write(buf, in.gcount());
        } while(in);
        in.close();
        out.close();
        if (out.bad() || in.bad()) {
            SE_THROW(string("failed copying ") + from + " to " + to);
        }
    }
}

bool isDir(const string &path)
{
    DIR *dir = opendir(path.c_str());
    if (dir) {
        closedir(dir);
        return true;
    } else if (errno != ENOTDIR && errno != ENOENT) {
        SyncContext::throwError(path, errno);
    }

    return false;
}

int Execute(const std::string &cmd, ExecuteFlags flags) throw()
{
    int ret = -1;

    try {
        // use simpler system() calls whenever we don't want to capture
        // output, because it means that output is sent to the user
        // directly
        if (((flags & EXECUTE_NO_STDERR) || !LogRedirect::redirectingStderr()) &&
            ((flags & EXECUTE_NO_STDOUT) || !LogRedirect::redirectingStdout())) {
            string fullcmd = cmd;
            if (flags & EXECUTE_NO_STDERR) {
                fullcmd += " 2>/dev/null";
            }
            if (flags & EXECUTE_NO_STDOUT) {
                fullcmd += " >/dev/null";
            }
            ret = system(fullcmd.c_str());
        } else {
            // Need to catch at least one of stdout or stderr. A
            // low-tech solution would be to use temporary files which
            // are read after system() returns. But we want true
            // streaming of the output, so use fork()/exec() plus
            // reliable output redirection.
            LogRedirect io(flags);
            pid_t child = fork();
            switch (child) {
            case 0: {
                // child process:
                // - close unused end of the pipes
                if (io.getStdout().m_read >= 0) {
                    close(io.getStdout().m_read);
                }
                if (io.getStderr().m_read >= 0) {
                    close(io.getStderr().m_read);
                }
                // - replace file descriptors 1 and 2 with the ones
                //   prepared for us or /dev/null
                int fd;
                int fd_null = open("/dev/null", O_WRONLY);
                fd = io.getStdout().m_write;
                if (fd <= 0) {
                    fd = fd_null;
                }
                dup2(fd, STDOUT_FILENO);
                fd = io.getStderr().m_write;
                if (fd <= 0) {
                    fd = fd_null;
                }
                dup2(fd, STDERR_FILENO);
                // - run command
                execl("/bin/sh", "sh", "-c", cmd.c_str(), NULL);
                // - error handling if execl() failed (= returned)
                std::cerr << cmd << ": execl() failed: " << strerror(errno);
                exit(1);
                break;
            }
            case -1:
                // error handling in parent when fork() fails
                SE_LOG_ERROR(NULL, NULL, "%s: fork() failed: %s",
                             cmd.c_str(), strerror(errno));
                break;
            default:
                // parent:
                // - close write side so that we can detect "end of data"
                if (io.getStdout().m_write >= 0) {
                    close(io.getStdout().m_write);
                }
                if (io.getStderr().m_write >= 0) {
                    close(io.getStderr().m_write);
                }
                // - read until no more data or error triggers exception
                io.process();
                // - wait for child, without caring about errors
                waitpid(child, &ret, 0);
                break;
            }
        }
    } catch (...) {
        Exception::handle();
    }

    return ret;
}

UUID::UUID()
{
    static class InitSRand {
    public:
        InitSRand() {
            ifstream seedsource("/dev/urandom");
            unsigned int seed;
            if (!seedsource.get((char *)&seed, sizeof(seed))) {
                seed = time(NULL);
            }
            srand(seed);
        }
    } initSRand;

    char buffer[16 * 4 + 5];
    sprintf(buffer, "%08x-%04x-%04x-%02x%02x-%08x%04x",
            rand() & 0xFFFFFFFF,
            rand() & 0xFFFF,
            (rand() & 0x0FFF) | 0x4000 /* RFC 4122 time_hi_and_version */,
            (rand() & 0xBF) | 0x80 /* clock_seq_hi_and_reserved */,
            rand() & 0xFF,
            rand() & 0xFFFFFFFF,
            rand() & 0xFFFF
            );
    this->assign(buffer);
}


ReadDir::ReadDir(const string &path, bool throwError) : m_path(path)
{
    DIR *dir = NULL;

    try {
        dir = opendir(path.c_str());
        if (!dir) {
            SyncContext::throwError(path, errno);
        }
        errno = 0;
        struct dirent *entry = readdir(dir);
        while (entry) {
            if (strcmp(entry->d_name, ".") &&
                strcmp(entry->d_name, "..")) {
                m_entries.push_back(entry->d_name);
            }
            entry = readdir(dir);
        }
        if (errno) {
            SyncContext::throwError(path, errno);
        }
    } catch(...) {
        if (dir) {
            closedir(dir);
        }
        if (throwError) {
            throw;
        } else {
            return;
        }
    }

    closedir(dir);
}

std::string ReadDir::find(const string &entry, bool caseSensitive)
{
    BOOST_FOREACH(const string &e, *this) {
        if (caseSensitive ? e == entry : boost::iequals(e, entry)) {
            return m_path + "/" + e;
        }
    }
    return "";
}

bool ReadFile(const string &filename, string &content)
{
    ifstream in;
    in.open(filename.c_str());
    return ReadFile(in, content);
}

bool ReadFile(istream &in, string &content)
{
    ostringstream out;
    char buf[8192];
    do {
        in.read(buf, sizeof(buf));
        out.write(buf, in.gcount());
    } while(in);

    content = out.str();
    return in.eof();
}

unsigned long Hash(const char *str)
{
    unsigned long hashval = 5381;
    int c;

    while ((c = *str++) != 0) {
        hashval = ((hashval << 5) + hashval) + c;
    }

    return hashval;
}

unsigned long Hash(const std::string &str)
{
    unsigned long hashval = 5381;

    BOOST_FOREACH(int c, str) {
        hashval = ((hashval << 5) + hashval) + c;
    }

    return hashval;
}

std::string SHA_256(const std::string &data)
{
#if USE_SHA256 == 1
    GString hash(g_compute_checksum_for_data(G_CHECKSUM_SHA256, (guchar *)data.c_str(), data.size()),
                 "g_compute_checksum_for_data() failed");
    return std::string(hash.get());
#elif USE_SHA256 == 2
    std::string res;
    unsigned char hash[SHA256_LENGTH];
    static bool initialized;
    if (!initialized) {
        // https://wiki.mozilla.org/NSS_Shared_DB_And_LINUX has
        // some comments which indicate that calling init multiple
        // times works, but http://www.mozilla.org/projects/security/pki/nss/ref/ssl/sslfnc.html#1234224
        // says it must only be called once. How that is supposed
        // to work when multiple, independent libraries have to
        // use NSS is beyond me. Bad design. At least let's do the
        // best we can here.
        NSS_NoDB_Init(NULL);
	initialized = true;
    }

    if (HASH_HashBuf(HASH_AlgSHA256, hash, (unsigned char *)data.c_str(), data.size()) != SECSuccess) {
        SE_THROW("NSS HASH_HashBuf() failed");
    }
    res.reserve(SHA256_LENGTH * 2);
    BOOST_FOREACH(unsigned char value, hash) {
        res += StringPrintf("%02x", value);
    }
    return res;
#else
    SE_THROW("Hash256() not implemented");
    return "";
#endif
}

StringEscape::StringEscape(char escapeChar, const char *forbidden) :
    m_escapeChar(escapeChar)
{
    while (*forbidden) {
        m_forbidden.insert(*forbidden);
        ++forbidden;
    }
}

string StringEscape::escape(const string &str) const
{
    if (m_mode != SET) {
        return escape(str, m_escapeChar, m_mode);
    }

    string res;
    char buffer[4];

    res.reserve(str.size() * 3);
    BOOST_FOREACH(char c, str) {
        if(c != m_escapeChar &&
           m_forbidden.find(c) == m_forbidden.end()) {
            res += c;
        } else {
            sprintf(buffer, "%c%02x",
                    m_escapeChar,
                    (unsigned int)(unsigned char)c);
            res += buffer;
        }
    }
    return res;
}

string StringEscape::escape(const string &str, char escapeChar, Mode mode)
{
    string res;
    char buffer[4];
    bool isLeadingSpace = true;
    res.reserve(str.size() * 3);

    BOOST_FOREACH(char c, str) {
        if(c != escapeChar &&
           (mode == STRICT ?
            (isalnum(c) ||
             c == '-' ||
             c == '_') :
            !(((isLeadingSpace || mode == INI_WORD) && isspace(c)) ||
              c == '=' ||
              c == '\r' ||
              c == '\n'))) {
            res += c;
            if (!isspace(c)) {
                isLeadingSpace = false;
            }
        } else {
            sprintf(buffer, "%c%02x",
                    escapeChar,
                    (unsigned int)(unsigned char)c);
            res += buffer;
        }
    }

    // also encode trailing space?
    if (mode == INI_VALUE) {
        size_t numspaces = 0;
        ssize_t off = res.size() - 1;
        while (off >= 0 && isspace(res[off])) {
            off--;
            numspaces++;
        }
        res.resize(res.size() - numspaces);
        BOOST_FOREACH(char c, str.substr(str.size() - numspaces)) {
            sprintf(buffer, "%c%02x",
                    escapeChar,
                    (unsigned int)(unsigned char)c);
            res += buffer;
        }
    }

    return res;
}

string StringEscape::unescape(const string &str, char escapeChar)
{
    string res;
    size_t curr;

    res.reserve(str.size());

    curr = 0;
    while (curr < str.size()) {
        if (str[curr] == escapeChar) {
            string hex = str.substr(curr + 1, 2);
            res += (char)strtol(hex.c_str(), NULL, 16);
            curr += 3;
        } else {
            res += str[curr];
            curr++;
        }
    }

    return res;
}

#ifdef ENABLE_UNIT_TESTS

class StringEscapeTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(StringEscapeTest);
    CPPUNIT_TEST(escape);
    CPPUNIT_TEST(unescape);
    CPPUNIT_TEST_SUITE_END();

    void escape() {
        const string test = " _-%\rfoo bar?! \n ";

        StringEscape def;
        CPPUNIT_ASSERT_EQUAL(string("%20_-%25%0dfoo%20bar%3f%21%20%0a%20"), def.escape(test));
        CPPUNIT_ASSERT_EQUAL(string("%20_-%25%0dfoo%20bar%3f%21%20%0a%20"),
                             StringEscape::escape(test, '%', StringEscape::STRICT));

        StringEscape word('%', StringEscape::INI_WORD);
        CPPUNIT_ASSERT_EQUAL(string("%20_-%25%0dfoo%20bar?!%20%0a%20"), word.escape(test));
        CPPUNIT_ASSERT_EQUAL(string("%20_-%25%0dfoo%20bar?!%20%0a%20"),
                             StringEscape::escape(test, '%', StringEscape::INI_WORD));

        StringEscape ini('%', StringEscape::INI_VALUE);
        CPPUNIT_ASSERT_EQUAL(string("%20_-%25%0dfoo bar?! %0a%20"), ini.escape(test));
        CPPUNIT_ASSERT_EQUAL(string("%20_-%25%0dfoo bar?! %0a%20"),
                             StringEscape::escape(test, '%', StringEscape::INI_VALUE));

        StringEscape alt('!', StringEscape::INI_VALUE);
        CPPUNIT_ASSERT_EQUAL(string("!20_-%!0dfoo bar?!21 !0a!20"), alt.escape(test));
        CPPUNIT_ASSERT_EQUAL(string("!20_-%!0dfoo bar?!21 !0a!20"),
                             StringEscape::escape(test, '!', StringEscape::INI_VALUE));
    }

    void unescape() {
        const string escaped = "%20_-%25foo%20bar%3F%21%20%0A";
        const string plain = " _-%foo bar?! \n";

        StringEscape def;
        CPPUNIT_ASSERT_EQUAL(plain, def.unescape(escaped));
        CPPUNIT_ASSERT_EQUAL(plain, StringEscape::unescape(escaped, '%'));

        CPPUNIT_ASSERT_EQUAL(string("%41B"), StringEscape::unescape("%41!42", '!'));
        CPPUNIT_ASSERT_EQUAL(string("A!42"), StringEscape::unescape("%41!42", '%'));
    }
};

SYNCEVOLUTION_TEST_SUITE_REGISTRATION(StringEscapeTest);

#endif // ENABLE_UNIT_TESTS

Timespec Timespec::operator + (const Timespec &other) const
{
    Timespec res(tv_sec + other.tv_sec,
                 tv_nsec + other.tv_nsec);
    if (res.tv_nsec > 1000000000) {
        res.tv_sec++;
        res.tv_nsec -= 1000000000;
    }
    return res;
}

Timespec Timespec::operator - (const Timespec &other) const
{
    Timespec res(tv_sec - other.tv_sec, 0);
    if (other.tv_nsec > tv_nsec) {
        res.tv_sec--;
        res.tv_nsec = tv_nsec + 1000000000 - other.tv_nsec;
    } else {
        res.tv_nsec = tv_nsec - other.tv_nsec;
    }
    return res;
}

#ifdef ENABLE_UNIT_TESTS

class TimespecTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(TimespecTest);
    CPPUNIT_TEST(add);
    CPPUNIT_TEST(substract);
    CPPUNIT_TEST_SUITE_END();

    void add()
    {
        CPPUNIT_ASSERT_EQUAL(Timespec(1, 0), Timespec(0, 0) + 1);
        CPPUNIT_ASSERT_EQUAL(Timespec(1, 0), Timespec(0, 0) + Timespec(1, 0));
        CPPUNIT_ASSERT_EQUAL(Timespec(1, 0), Timespec(0, 500000000) + Timespec(0, 500000000));
        CPPUNIT_ASSERT_EQUAL(Timespec(1, 999999998), Timespec(0, 999999999) + Timespec(0, 999999999));
    }

    void substract()
    {
        CPPUNIT_ASSERT_EQUAL(Timespec(1, 0), Timespec(2, 0) - 1);
        CPPUNIT_ASSERT_EQUAL(Timespec(1, 0), Timespec(2, 0) - Timespec(1, 0));
        CPPUNIT_ASSERT_EQUAL(Timespec(1, 0), Timespec(1, 500000000) - Timespec(0, 500000000));
        CPPUNIT_ASSERT_EQUAL(Timespec(0, 999999999), Timespec(1, 999999998) - Timespec(0, 999999999));
    }
};

SYNCEVOLUTION_TEST_SUITE_REGISTRATION(TimespecTest);

#endif // ENABLE_UNIT_TESTS



std::string StringPrintf(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    std::string res = StringPrintfV(format, ap);
    va_end(ap);
    return res;
}

std::string StringPrintfV(const char *format, va_list ap)
{
    va_list aq;

    char *buffer = NULL, *nbuffer = NULL;
    ssize_t size = 0;
    ssize_t realsize = 255;
    do {
        // vsnprintf() destroys ap, so make a copy first
        va_copy(aq, ap);

        if (size < realsize) {
            nbuffer = (char *)realloc(buffer, realsize + 1);
            if (!nbuffer) {
                if (buffer) {
                    free(buffer);
                }
                return "";
            }
            size = realsize;
            buffer = nbuffer;
        }

        realsize = vsnprintf(buffer, size + 1, format, aq);
        if (realsize == -1) {
            // old-style vnsprintf: exact len unknown, try again with doubled size
            realsize = size * 2;
        }
        va_end(aq);
    } while(realsize > size);

    std::string res = buffer;
    free(buffer);
    return res;
}

char *Strncpy(char *dest, const char *src, size_t n)
{
    strncpy(dest, src, n);
    if (n) {
        dest[n - 1] = 0;
    }
    return dest;
}

void Sleep(double seconds)
{
    timeval delay;
    delay.tv_sec = floor(seconds);
    delay.tv_usec = (seconds - (double)delay.tv_sec) * 1e6;
    select(0, NULL, NULL, NULL, &delay);
}


SyncMLStatus Exception::handle(SyncMLStatus *status,
                               Logger *logger,
                               std::string *explanation,
                               Logger::Level level,
                               HandleExceptionFlags flags)
{
    // any problem here is a fatal local problem, unless set otherwise
    // by the specific exception
    SyncMLStatus new_status = SyncMLStatus(STATUS_FATAL + sysync::LOCAL_STATUS_CODE);
    std::string error;

    try {
        throw;
    } catch (const TransportException &ex) {
        SE_LOG_DEBUG(logger, NULL, "TransportException thrown at %s:%d",
                     ex.m_file.c_str(), ex.m_line);
        error = ex.what();
        new_status = SyncMLStatus(sysync::LOCERR_TRANSPFAIL);
    } catch (const BadSynthesisResult &ex) {
        new_status = SyncMLStatus(ex.result());
        error = StringPrintf("error code from Synthesis engine %s",
                             Status2String(new_status).c_str());
    } catch (const StatusException &ex) {
        new_status = ex.syncMLStatus();
        SE_LOG_DEBUG(logger, NULL, "exception thrown at %s:%d",
                     ex.m_file.c_str(), ex.m_line);
        error = StringPrintf("error code from SyncEvolution %s: %s",
                             Status2String(new_status).c_str(), ex.what());
        if (new_status == STATUS_NOT_FOUND &&
            (flags & HANDLE_EXCEPTION_404_IS_OKAY)) {
            level = Logger::DEBUG;
        }
    } catch (const Exception &ex) {
        SE_LOG_DEBUG(logger, NULL, "exception thrown at %s:%d",
                     ex.m_file.c_str(), ex.m_line);
        error = ex.what();
    } catch (const std::exception &ex) {
        error = ex.what();
    } catch (...) {
        error = "unknown error";
    }
    SE_LOG(level, logger, NULL, "%s", error.c_str());

    if (explanation) {
        *explanation = error;
    }

    if (status && *status == STATUS_OK) {
        *status = new_status;
    }
    return status ? *status : new_status;
}

std::string SubstEnvironment(const std::string &str)
{
    std::stringstream res;
    size_t envstart = std::string::npos;
    size_t off;

    for(off = 0; off < str.size(); off++) {
        if (envstart != std::string::npos) {
            if (str[off] == '}') {
                std::string envname = str.substr(envstart, off - envstart);
                envstart = std::string::npos;

                const char *val = getenv(envname.c_str());
                if (val) {
                    res << val;
                } else if (envname == "XDG_CONFIG_HOME") {
                    res << getHome() << "/.config";
                } else if (envname == "XDG_DATA_HOME") {
                    res << getHome() << "/.local/share";
                } else if (envname == "XDG_CACHE_HOME") {
                    res << getHome() << "/.cache";
                }
            }
        } else {
            if (str[off] == '$' &&
                off + 1 < str.size() &&
                str[off + 1] == '{') {
                envstart = off + 2;
                off++;
            } else {
                res << str[off];
            }
        }
    }

    return res.str();
}

std::vector<std::string> unescapeJoinedString (const std::string& src, char sep)
{
    std::vector<std::string> splitStrings;
    size_t pos1 = 0, pos2 = 0, pos3 = 0;
    std::string s1, s2;
    while (pos3 != src.npos) {
        pos2 = src.find (sep, pos3);
        s1 = src.substr (pos1, 
                (pos2 == std::string::npos) ? std::string::npos : pos2-pos1);
        size_t pos = s1.find_last_not_of ("\\");
        pos3 = (pos2 == std::string::npos) ?pos2 : pos2+1;
        // A matching delimiter is a comma with even trailing '\'
        // characters
        if (!((s1.length() - ((pos == s1.npos) ? 0: pos-1)) &1 )) {
            s2="";
            boost::trim (s1);
            for (std::string::iterator i = s1.begin(); i != s1.end(); i++) {
                //unescape characters
                if (*i == '\\') {
                    if(++i == s1.end()) {
                        break;
                    }
                }
                s2+=*i;
            }
            splitStrings.push_back (s2);
            pos1 = pos3;
        }
    }
    return splitStrings;
}

std::string Flags2String(int flags, const Flag *descr, const std::string &sep)
{
    std::list<std::string> tmp;

    while (descr->m_flag) {
        if (flags & descr->m_flag) {
            tmp.push_back(descr->m_description);
        }
        ++descr;
    }
    return boost::join(tmp, ", ");
}

ScopedEnvChange::ScopedEnvChange(const string &var, const string &value) :
    m_var(var)
{
    const char *oldval = getenv(var.c_str());
    if (oldval) {
        m_oldvalset = true;
        m_oldval = oldval;
    } else {
        m_oldvalset = false;
    }
    setenv(var.c_str(), value.c_str(), 1);
}

ScopedEnvChange::~ScopedEnvChange()
{
    if (m_oldvalset) {
        setenv(m_var.c_str(), m_oldval.c_str(), 1);
    } else {
        unsetenv(m_var.c_str());
    } 
}

std::string getCurrentTime()
{
    time_t seconds = time (NULL);
    tm *data = localtime (&seconds);
    arrayptr<char> buffer (new char [13]);
    strftime (buffer.get(), 13, "%y%m%d%H%M%S", data);
    return buffer.get();
}

SE_END_CXX
