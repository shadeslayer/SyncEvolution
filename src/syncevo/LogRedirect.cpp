/*
 * Copyright (C) 2009 Patrick Ohly <patrick.ohly@gmx.de>
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
#include <syncevo/LogRedirect.h>
#include <syncevo/Logging.h>
#include <syncevo/SyncContext.h>
#include "test.h"
#include <syncevo/util.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

#include <boost/algorithm/string/predicate.hpp>

#include <algorithm>
#include <iostream>

#ifdef HAVE_GLIB
# include <glib.h>
#endif


#include <syncevo/declarations.h>
using namespace std;
SE_BEGIN_CXX

LogRedirect *LogRedirect::m_redirect;
std::set<std::string> LogRedirect::m_knownErrors;

void LogRedirect::abortHandler(int sig) throw()
{
    // Don't know state of logging system, don't log here!
    // SE_LOG_ERROR(NULL, NULL, "caught signal %d, shutting down", sig);

    // shut down redirection, also flushes to log
    if (m_redirect) {
        m_redirect->restore();
    }

    // Raise same signal again. Because our handler
    // is automatically removed, this will abort
    // for real now.
    raise(sig);
}

void LogRedirect::init()
{
    m_processing = false;
    m_buffer = NULL;
    m_len = 0;
    m_out = NULL;
    m_err = NULL;
    m_streams = false;
    m_stderr.m_original =
        m_stderr.m_read =
        m_stderr.m_write =
        m_stderr.m_copy = -1;
    m_stdout.m_original =
        m_stdout.m_read =
        m_stdout.m_write =
        m_stdout.m_copy = -1;

    const char *lines = getenv("SYNCEVOLUTION_SUPPRESS_ERRORS");
    if (lines) {
        typedef boost::split_iterator<const char *> string_split_iterator;
        string_split_iterator it =
            boost::make_split_iterator(lines, boost::first_finder("\n", boost::is_iequal()));
        while (it != string_split_iterator()) {
            m_knownErrors.insert(std::string(it->begin(), it->end()));
            ++it;
        }
    }
}

LogRedirect::LogRedirect(bool both, const char *filename) throw()
{
    init();
    m_processing = true;
    if (!getenv("SYNCEVOLUTION_DEBUG")) {
        redirect(STDERR_FILENO, m_stderr);
        if (both) {
            redirect(STDOUT_FILENO, m_stdout);
            m_out = filename ?
                fopen(filename, "w") :
                fdopen(dup(m_stdout.m_copy), "w");
            if (!m_out) {
                restore(m_stdout);
                restore(m_stderr);
                perror(filename ? filename : "LogRedirect fdopen");
            }
        } else if (filename) {
            m_out = fopen(filename, "w");
            if (!m_out) {
                perror(filename);
            }
        }
        // Separate FILE, will write into same file as normal output
        // if a filename was given (for testing), otherwise to original
        // stderr.
        m_err = fdopen(dup((filename && m_out) ?
                           fileno(m_out) :
                           m_stderr.m_copy), "w");
    }
    LoggerBase::pushLogger(this);
    m_redirect = this;

    if (!getenv("SYNCEVOLUTION_DEBUG")) {
        struct sigaction new_action, old_action;
        memset(&new_action, 0, sizeof(new_action));
        new_action.sa_handler = abortHandler;
        sigemptyset(&new_action.sa_mask);
        // disable handler after it was called once
        new_action.sa_flags = SA_RESETHAND;
        // block signals while we handler is active
        // to prevent recursive calls
        sigaddset(&new_action.sa_mask, SIGABRT);
        sigaddset(&new_action.sa_mask, SIGSEGV);
        sigaddset(&new_action.sa_mask, SIGBUS);
        sigaction(SIGABRT, &new_action, &old_action);
        sigaction(SIGSEGV, &new_action, &old_action);
        sigaction(SIGBUS, &new_action, &old_action);
    }
    m_processing = false;
}

LogRedirect::LogRedirect(ExecuteFlags flags)
{
    init();

    m_streams = true;
    if (!(flags & EXECUTE_NO_STDERR)) {
        redirect(STDERR_FILENO, m_stderr);
    }
    if (!(flags & EXECUTE_NO_STDOUT)) {
        redirect(STDOUT_FILENO, m_stdout);
    }
}

LogRedirect::~LogRedirect() throw()
{
    bool pop = false;
    if (m_redirect == this) {
        m_redirect = NULL;
        pop = true;
    }
    process();
    restore();
    m_processing = true;
    if (m_out) {
        fclose(m_out);
    }
    if (m_err) {
        fclose(m_err);
    }
    if (m_buffer) {
        free(m_buffer);
    }
    if (pop) {
        LoggerBase::popLogger();
    }
}

void LogRedirect::redoRedirect() throw()
{
    bool doStdout = m_stdout.m_copy >= 0;
    bool doStderr = m_stderr.m_copy >= 0;

    if (doStdout) {
        restore(m_stdout);
        redirect(STDOUT_FILENO, m_stdout);
    }
    if (doStderr) {
        restore(m_stderr);
        redirect(STDERR_FILENO, m_stderr);
    }
}

void LogRedirect::restore() throw()
{
    if (m_processing) {
        return;
    }
    m_processing = true;

    restore(m_stdout);
    restore(m_stderr);

    m_processing = false;
}

void LogRedirect::messagev(Level level,
                           const char *prefix,
                           const char *file,
                           int line,
                           const char *function,
                           const char *format,
                           va_list args)
{
    // check for other output first
    process();
    // Choose output channel: SHOW goes to original stdout,
    // everything else to stderr.
    LoggerStdout::messagev(level == SHOW ?
                           (m_out ? m_out : stdout) :
                           (m_err ? m_err : stderr),
                           level, getLevel(),
                           prefix,
                           file, line, function,
                           format,
                           args);
}

void LogRedirect::redirect(int original, FDs &fds) throw()
{
    fds.m_original = original;
    fds.m_write = fds.m_read = -1;
    fds.m_copy = dup(fds.m_original);
    if (fds.m_copy >= 0) {
        if (m_streams) {
            // According to Stevens, Unix Network Programming, "Unix
            // domain datagram sockets are similar to UDP sockets: the
            // provide an *unreliable* datagram service that preserves
            // record boundaries." (14.4 Socket Functions,
            // p. 378). But unit tests showed that they do block on
            // Linux and thus seem reliable. Not sure what the official
            // spec is.
            //
            // To avoid the deadlock risk, we must use UDP. But when we
            // want "reliable" behavior *and* detect that all output was
            // processed, we have to use streams despite loosing
            // the write() boundaries, because Unix domain datagram sockets
            // do not flag "end of data".
            int sockets[2];
#define USE_UNIX_DOMAIN_DGRAM 0
            if (!socketpair(AF_LOCAL,
                            USE_UNIX_DOMAIN_DGRAM ? SOCK_DGRAM : SOCK_STREAM,
                            0, sockets)) {
                // success
                fds.m_write = sockets[0];
                fds.m_read = sockets[1];
                return;
            } else {
                perror("LogRedirect::redirect() socketpair");
            }
        } else {
            int write = socket(AF_INET, SOCK_DGRAM, 0);
            if (write >= 0) {
                int read = socket(AF_INET, SOCK_DGRAM, 0);
                if (read >= 0) {
                    struct sockaddr_in addr;
                    memset(&addr, 0, sizeof(addr));
                    addr.sin_family = AF_INET;
                    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
                    bool bound = false;
                    for (int port = 1025; !bound && port < 10000; port++) {
                        addr.sin_port = htons(port);
                        if (!bind(read, (struct sockaddr *)&addr, sizeof(addr))) {
                            bound = true;
                        }
                    }

                    if (bound) {
                        if (!connect(write, (struct sockaddr *)&addr, sizeof(addr))) {
                            if (dup2(write, fds.m_original) >= 0) {
                                // success
                                fds.m_write = write;
                                fds.m_read = read;
                                return;
                            }
                            perror("LogRedirect::redirect() dup2");
                        }
                        perror("LogRedirect::redirect connect");
                    }
                    close(read);
                }
                close(write);
            }
        }
        close(fds.m_copy);
        fds.m_copy = -1;
    } else {
        perror("LogRedirect::redirect() dup");
    }
}

void LogRedirect::restore(FDs &fds) throw()
{
    if (!m_streams && fds.m_copy >= 0) {
        // flush our own redirected output and process what they might have written
        if (fds.m_original == STDOUT_FILENO) {
            fflush(stdout);
            std::cout << std::flush;
        } else {
            fflush(stderr);
            std::cerr << std::flush;
        }
        process(fds);

        dup2(fds.m_copy, fds.m_original);
    }

    if (fds.m_copy >= 0) {
        close(fds.m_copy);
    }
    if (fds.m_write >= 0) {
        close(fds.m_write);
    }
    if (fds.m_read >= 0) {
        close(fds.m_read);
    }
    fds.m_copy =
        fds.m_write =
        fds.m_read = -1;
}

bool LogRedirect::process(FDs &fds) throw()
{
    bool have_message;
    bool data_read = false;

    if (fds.m_read <= 0) {
        return data_read;
    }

    do {
        ssize_t available = 0;
        have_message = false;

        // keep peeking at the data with increasing buffer sizes until
        // we are sure that we don't truncate it
        size_t newlen = std::max((size_t)1024, m_len);
        while (true) {
            // increase buffer?
            if (newlen > m_len) {
                m_buffer = (char *)realloc(m_buffer, newlen);
                if (!m_buffer) {
                    m_len = 0;
                    break;
                } else {
                    m_len = newlen;
                }
            }
            // read, but leave space for nul byte;
            // when using datagrams, we only peek here and remove the
            // datagram below, without rereading the data
            if (!USE_UNIX_DOMAIN_DGRAM && m_streams) {
                available = recv(fds.m_read, m_buffer, m_len - 1, MSG_DONTWAIT);
                if (available == 0) {
                    return data_read;
                } else if (available == -1) {
                    if (errno == EAGAIN) {
                        // pretend that data was read, so that caller invokes us again
                        return true;
                    } else {
                        SyncContext::throwError("reading output", errno);
                        return false;
                    }
                } else {
                    // data read, process it
                    data_read = true;
                    break;
                }
            } else {
                available = recv(fds.m_read, m_buffer, m_len - 1, MSG_DONTWAIT|MSG_PEEK);
                have_message = available >= 0;
            }
            if (available < (ssize_t)m_len - 1) {
                break;
            } else {
                // try again with twice the buffer
                newlen *= 2;
            }
        }
        if (have_message) {
            // swallow packet, even if empty or we couldn't receive it
            recv(fds.m_read, NULL, 0, MSG_DONTWAIT);
            data_read = true;
        }

        if (available > 0) {
            m_buffer[available] = 0;
            // Now pass it to logger, with a level determined by
            // the channel. This is the point where we can filter
            // out known noise.
            const char *prefix = NULL;
            Logger::Level level = Logger::DEV;
            char *text = m_buffer;

            if (fds.m_original == STDOUT_FILENO) {
                // stdout: not sure what this could be, so show it
                level = Logger::SHOW;
                char *eol = strchr(text, '\n');
                if (!m_stdoutData.empty()) {
                    // try to complete previous line, can be done
                    // if text contains a line break
                    if (eol) {
                        m_stdoutData.append(text, eol - text);
                        text = eol + 1;
                        LoggerBase::instance().message(level, prefix,
                                                       NULL, 0, NULL,
                                                       "%s", m_stdoutData.c_str());
                        m_stdoutData.clear();
                    }
                }

                // avoid sending incomplete line at end of text,
                // must be done when there is no line break or
                // it is not at the end of the buffer
                eol = strrchr(text, '\n');
                if (eol != m_buffer + available - 1) {
                    if (eol) {
                        m_stdoutData.append(eol + 1);
                        *eol = 0;
                    } else {
                        m_stdoutData.append(text);
                        *text = 0;
                    }
                }

                // output might have been processed as part of m_stdoutData,
                // don't log empty string below
                if (!*text) {
                    continue;
                }
            } else if (fds.m_original == STDERR_FILENO) {
                // stderr: not normally useful for users, so we
                // can filter it more aggressively. For example,
                // swallow extra line breaks, glib inserts those.
                while (*text == '\n') {
                    text++;
                }
                const char *glib_debug_prefix = "** ("; // ** (client-test:875): WARNING **:
                const char *glib_msg_prefix = "** Message:";
                prefix = "stderr";
                if ((!strncmp(text, glib_debug_prefix, strlen(glib_debug_prefix)) &&
                     strstr(text, " **:")) ||
                    !strncmp(text, glib_msg_prefix, strlen(glib_msg_prefix))) {
                    level = Logger::DEBUG;
                    prefix = "glib";
                } else {
                    level = Logger::DEV;
                }

                // If the text contains the word "error", it probably
                // is severe enough to show to the user, regardless of
                // who produced it... except for errors suppressed
                // explicitly.
                if (strcasestr(text, "error") &&
                    !ignoreError(text)) {
                    level = Logger::ERROR;
                }
            }

            // avoid explicit newline at end of output,
            // logging will add it for each message()
            // invocation
            size_t len = strlen(text);
            if (len > 0 && text[len - 1] == '\n') {
                text[len - 1] = 0;
            }
            LoggerBase::instance().message(level, prefix,
                                           NULL, 0, NULL,
                                           "%s", text);
        }
    } while(have_message);

    return data_read;
}

bool LogRedirect::ignoreError(const std::string &text)
{
    BOOST_FOREACH(const std::string &entry, m_knownErrors) {
        if (text.find(entry) != text.npos) {
            return true;
        }
    }
    return false;
}

void LogRedirect::process()
{
    if (m_streams) {
        // iterate until both sockets are closed by peer
        while (true) {
            fd_set readfds;
            fd_set errfds;
            int maxfd = 0;
            FD_ZERO(&readfds);
            FD_ZERO(&errfds);
            if (m_stdout.m_read >= 0) {
                FD_SET(m_stdout.m_read, &readfds);
                FD_SET(m_stdout.m_read, &errfds);
                maxfd = m_stdout.m_read;
            }
            if (m_stderr.m_read >= 0) {
                FD_SET(m_stderr.m_read, &readfds);
                FD_SET(m_stderr.m_read, &errfds);
                if (m_stderr.m_read > maxfd) {
                    maxfd = m_stderr.m_read;
                }
            }
            if (maxfd == 0) {
                // both closed
                return;
            }

            int res = select(maxfd + 1, &readfds, NULL, &errfds, NULL);
            switch (res) {
            case -1:
                // fatal, cannot continue
                SyncContext::throwError("waiting for output", errno);
                return;
                break;
            case 0:
                // None ready? Try again.
                break;
            default:
                if (m_stdout.m_read >= 0 && FD_ISSET(m_stdout.m_read, &readfds)) {
                    if (!process(m_stdout)) {
                        // Exact status of a Unix domain datagram socket upon close
                        // of the remote end is a bit uncertain. For TCP, we would end
                        // up here: marked by select as "ready for read", but no data -> EOF.
                        close(m_stdout.m_read);
                        m_stdout.m_read = -1;
                    }
                }
                if (m_stdout.m_read >= 0 && FD_ISSET(m_stdout.m_read, &errfds)) {
                    // But in practice, Unix domain sockets don't mark the stream
                    // as "closed". This is an attempt to detect that situation
                    // via the FDs exception status, but that also doesn't work.
                    close(m_stdout.m_read);
                    m_stdout.m_read = -1;
                }
                if (m_stderr.m_read >= 0 && FD_ISSET(m_stderr.m_read, &readfds)) {
                    if (!process(m_stderr)) {
                        close(m_stderr.m_read);
                        m_stderr.m_read = -1;
                    }
                }
                if (m_stderr.m_read >= 0 && FD_ISSET(m_stderr.m_read, &errfds)) {
                    close(m_stderr.m_read);
                    m_stderr.m_read = -1;
                }
                break;
            }
        }
    }

    if (m_processing) {
        return;
    }
    m_processing = true;

    process(m_stdout);
    process(m_stderr);

    // avoid hanging onto excessive amounts of memory
    m_len = std::min((size_t)(4 * 1024), m_len);
    m_buffer = (char *)realloc(m_buffer, m_len);
    if (!m_buffer) {
        m_len = 0;
    }

    m_processing = false;
}



void LogRedirect::flush() throw()
{
    process();
    if (!m_stdoutData.empty()) {
        LoggerBase::instance().message(Logger::SHOW, NULL,
                                       NULL, 0, NULL,
                                       "%s", m_stdoutData.c_str());
        m_stdoutData.clear();
    }
}


#ifdef ENABLE_UNIT_TESTS

class LogRedirectTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(LogRedirectTest);
    CPPUNIT_TEST(simple);
    CPPUNIT_TEST(largeChunk);
    CPPUNIT_TEST(streams);
    CPPUNIT_TEST(overload);
#ifdef HAVE_GLIB
    CPPUNIT_TEST(glib);
#endif
    CPPUNIT_TEST_SUITE_END();

    /**
     * redirect stdout/stderr, then intercept the log messages and
     * store them for inspection
     */
    class LogBuffer : public LoggerBase
    {
    public:
        std::stringstream m_streams[DEBUG + 1];
        LogRedirect *m_redirect;

        LogBuffer(bool both = true)
        {
            m_redirect = new LogRedirect(both);
            pushLogger(this);
        }
        ~LogBuffer()
        {
            popLogger();
            delete m_redirect;
        }

        virtual void messagev(Level level,
                          const char *prefix,
                          const char *file,
                          int line,
                          const char *function,
                          const char *format,
                          va_list args)
        {
            CPPUNIT_ASSERT(level <= DEBUG && level >= 0);
            m_streams[level] << StringPrintfV(format, args);
        }
        virtual bool isProcessSafe() const { return true; }
    };
    
public:
    void simple()
    {
        LogBuffer buffer;

        static const char *simpleMessage = "hello world";
        CPPUNIT_ASSERT_EQUAL((ssize_t)strlen(simpleMessage), write(STDOUT_FILENO, simpleMessage, strlen(simpleMessage)));
        buffer.m_redirect->flush();

        CPPUNIT_ASSERT_EQUAL(buffer.m_streams[Logger::SHOW].str(), std::string(simpleMessage));
    }

    void largeChunk()
    {
        LogBuffer buffer;

        std::string large;
        large.append(60 * 1024, 'h');
        CPPUNIT_ASSERT_EQUAL((ssize_t)large.size(), write(STDOUT_FILENO, large.c_str(), large.size()));
        buffer.m_redirect->flush();

        CPPUNIT_ASSERT_EQUAL(large.size(), buffer.m_streams[Logger::SHOW].str().size());
        CPPUNIT_ASSERT_EQUAL(large, buffer.m_streams[Logger::SHOW].str());
    }

    void streams()
    {
        LogBuffer buffer;

        static const char *simpleMessage = "hello world";
        CPPUNIT_ASSERT_EQUAL((ssize_t)strlen(simpleMessage), write(STDOUT_FILENO, simpleMessage, strlen(simpleMessage)));
        static const char *errorMessage = "such a cruel place";
        CPPUNIT_ASSERT_EQUAL((ssize_t)strlen(errorMessage), write(STDERR_FILENO, errorMessage, strlen(errorMessage)));

        // process() keeps unfinished STDOUT lines buffered
        buffer.m_redirect->process();
        CPPUNIT_ASSERT_EQUAL(std::string(errorMessage), buffer.m_streams[Logger::DEV].str());
        CPPUNIT_ASSERT_EQUAL(string(""), buffer.m_streams[Logger::SHOW].str());

        // flush() makes them available
        buffer.m_redirect->flush();
        CPPUNIT_ASSERT_EQUAL(std::string(errorMessage), buffer.m_streams[Logger::DEV].str());
        CPPUNIT_ASSERT_EQUAL(std::string(simpleMessage), buffer.m_streams[Logger::SHOW].str());
    }

    void overload()
    {
        LogBuffer buffer;

        std::string large;
        large.append(1024, 'h');
        for (int i = 0; i < 4000; i++) {
            CPPUNIT_ASSERT_EQUAL((ssize_t)large.size(), write(STDOUT_FILENO, large.c_str(), large.size()));
        }
        buffer.m_redirect->flush();

        CPPUNIT_ASSERT(buffer.m_streams[Logger::SHOW].str().size() > large.size());
    }

#ifdef HAVE_GLIB
    void glib()
    {
        fflush(stdout);
        fflush(stderr);

        static const char *filename = "LogRedirectTest_glib.out";
        int new_stdout = open(filename, O_RDWR|O_CREAT|O_TRUNC, S_IRWXU);

        // check that intercept all glib message and don't print anything to stdout
        int orig_stdout = -1;
        try {
            // need to restore the current state below; would be nice
            // to query it instead of assuming that Logger::glogFunc
            // is the current log handler
            g_log_set_default_handler(g_log_default_handler, NULL);

            orig_stdout = dup(STDOUT_FILENO);
            dup2(new_stdout, STDOUT_FILENO);

            LogBuffer buffer(false);

            fprintf(stdout, "normal message stdout\n");
            fflush(stdout);

            fprintf(stderr, "normal message stderr\n");
            fflush(stderr);

            // ** (process:13552): WARNING **: test warning
            g_warning("test warning");
            // ** Message: test message
            g_message("test message");
            // ** (process:13552): CRITICAL **: test critical
            g_critical("test critical");
            // would abort:
            // g_error("error")
            // ** (process:13552): DEBUG: test debug
            g_debug("test debug");

            buffer.m_redirect->process();

            std::string error = buffer.m_streams[Logger::ERROR].str();
            std::string warning = buffer.m_streams[Logger::WARNING].str();           
            std::string show = buffer.m_streams[Logger::SHOW].str();
            std::string info = buffer.m_streams[Logger::INFO].str();
            std::string dev = buffer.m_streams[Logger::DEV].str();
            std::string debug = buffer.m_streams[Logger::DEBUG].str();
            CPPUNIT_ASSERT_EQUAL(string(""), error);
            CPPUNIT_ASSERT_EQUAL(string(""), warning);
            CPPUNIT_ASSERT_EQUAL(string(""), show);
            CPPUNIT_ASSERT_EQUAL(string(""), info);
            CPPUNIT_ASSERT_EQUAL(string(""), error);
            CPPUNIT_ASSERT(dev.find("normal message stderr") != dev.npos);
            CPPUNIT_ASSERT(debug.find("test warning") != debug.npos);
        } catch(...) {
            g_log_set_default_handler(Logger::glogFunc, NULL);
            dup2(orig_stdout, STDOUT_FILENO);
            throw;
        }
        g_log_set_default_handler(Logger::glogFunc, NULL);
        dup2(orig_stdout, STDOUT_FILENO);

        lseek(new_stdout, 0, SEEK_SET);
        char out[128];
        ssize_t l = read(new_stdout, out, sizeof(out) - 1);
        CPPUNIT_ASSERT(l > 0);
        out[l] = 0;
        CPPUNIT_ASSERT(boost::starts_with(std::string(out), "normal message stdout"));
    }
#endif
};

SYNCEVOLUTION_TEST_SUITE_REGISTRATION(LogRedirectTest);

#endif // ENABLE_UNIT_TESTS


SE_END_CXX
