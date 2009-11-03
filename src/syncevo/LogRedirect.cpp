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
SE_BEGIN_CXX

LogRedirect *LogRedirect::m_redirect;

void LogRedirect::abortHandler(int sig) throw()
{
    SE_LOG_ERROR(NULL, NULL, "caught signal %d, shutting down", sig);

    // shut down redirection, also flushes to log
    if (m_redirect) {
        m_redirect->restore();
    }

    // Raise same signal again. Because our handler
    // is automatically removed, this will abort
    // for real now.
    raise(sig);
}

LogRedirect::LogRedirect(bool both) throw()
{
    m_processing = false;
    m_buffer = NULL;
    m_len = 0;
    m_out = NULL;
    m_stderr.m_original =
        m_stderr.m_read =
        m_stderr.m_write =
        m_stderr.m_copy = -1;
    m_stdout.m_original =
        m_stdout.m_read =
        m_stdout.m_write =
        m_stdout.m_copy = -1;
    if (!getenv("SYNCEVOLUTION_DEBUG")) {
        redirect(STDERR_FILENO, m_stderr);
        if (both) {
            redirect(STDOUT_FILENO, m_stdout);
            m_out = fdopen(dup(m_stdout.m_copy), "w");
            if (!m_out) {
                restore(m_stdout);
                restore(m_stderr);
                perror("LogRedirect fdopen");
            }
        }
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
}

LogRedirect::~LogRedirect() throw()
{
    m_redirect = NULL;
    process();
    restore();
    if (m_out) {
        fclose(m_out);
    }
    if (m_buffer) {
        free(m_buffer);
    }
    LoggerBase::popLogger();
}

void LogRedirect::restore() throw()
{
    restore(m_stdout);
    restore(m_stderr);
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
    LoggerStdout::messagev(m_out ? m_out : stdout,
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
#ifdef USE_LOGREDIRECT_UNIX_DOMAIN
        int sockets[2];

        if (!socketpair(AF_LOCAL, SOCK_DGRAM, 0, sockets)) {
            if (dup2(sockets[0], fds.m_original) >= 0) {
                // success
                fds.m_write = sockets[0];
                fds.m_read = sockets[1];
                return;
            } else {
                perror("LogRedirect::redirect() dup2");
            }
            close(sockets[0]);
            close(sockets[1]);
        } else {
            perror("LogRedirect::redirect() socketpair");
        }
#else
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
#endif
        close(fds.m_copy);
        fds.m_copy = -1;
    } else {
        perror("LogRedirect::redirect() dup");
    }
}

void LogRedirect::restore(FDs &fds) throw()
{
    if (fds.m_copy < 0) {
        return;
    }

    // flush streams and process what they might have written
    if (fds.m_original == STDOUT_FILENO) {
        fflush(stdout);
        std::cout << std::flush;
    } else {
        fflush(stderr);
        std::cerr << std::flush;
    }
    process(fds);

    dup2(fds.m_copy, fds.m_original);
    close(fds.m_copy);
    close(fds.m_write);
    close(fds.m_read);
    fds.m_copy =
        fds.m_write =
        fds.m_read = -1;
}

void LogRedirect::process(FDs &fds) throw()
{
    bool have_message;

    if (fds.m_read <= 0) {
        return;
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
            // read, but leave space for nul byte
            available = recv(fds.m_read, m_buffer, m_len - 1, MSG_PEEK|MSG_DONTWAIT);
            have_message = available >= 0;
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
        }

        if (available > 0) {
            m_buffer[available] = 0;
            // Now pass it to logger, with a level determined by
            // the channel. This is the point where we can filter
            // out known noise.
            const char *prefix = NULL;
            Logger::Level level = Logger::DEV;
            const char *text = m_buffer;

            if (fds.m_original == STDOUT_FILENO) {
                // stdout: not sure what this could be, so show it
                level = Logger::INFO;
            } else if (fds.m_original == STDERR_FILENO) {
                // stderr: not normally useful for users, so we
                // can filter it more aggressively. For example,
                // swallow extra line breaks, glib inserts those.
                while (*text == '\n') {
                    text++;
                }
                const char *glib_debug_prefix = "** (process:";
                const char *glib_msg_prefix = "** Message:";
                prefix = "stderr";
                if (!strncmp(text, glib_debug_prefix, strlen(glib_debug_prefix)) ||
                    !strncmp(text, glib_msg_prefix, strlen(glib_msg_prefix))) {
                    level = Logger::DEBUG;
                    prefix = "glib";
                } else {
                    level = Logger::DEV;
                }

                // If the text contains the word "error", it probably
                // is severe enough to show to the user, regardless of
                // who produced it.
                if (strcasestr(text, "error")) {
                    level = Logger::ERROR;
                }
            }

            LoggerBase::instance().message(level, prefix,
                                           NULL, 0, NULL,
                                           "%s", text);
        }
    } while(have_message);
}


void LogRedirect::process() throw()
{
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
    };
    
public:
    void simple()
    {
        LogBuffer buffer;

        static const char *simpleMessage = "hello world";
        CPPUNIT_ASSERT_EQUAL((ssize_t)strlen(simpleMessage), write(STDOUT_FILENO, simpleMessage, strlen(simpleMessage)));
        buffer.m_redirect->process();

        CPPUNIT_ASSERT_EQUAL(buffer.m_streams[Logger::INFO].str(), std::string(simpleMessage));
    }

    void largeChunk()
    {
        LogBuffer buffer;

        std::string large;
        large.append(60 * 1024, 'h');
        CPPUNIT_ASSERT_EQUAL((ssize_t)large.size(), write(STDOUT_FILENO, large.c_str(), large.size()));
        buffer.m_redirect->process();

        CPPUNIT_ASSERT_EQUAL(large.size(), buffer.m_streams[Logger::INFO].str().size());
        CPPUNIT_ASSERT_EQUAL(large, buffer.m_streams[Logger::INFO].str());
    }

    void streams()
    {
        LogBuffer buffer;

        static const char *simpleMessage = "hello world";
        CPPUNIT_ASSERT_EQUAL((ssize_t)strlen(simpleMessage), write(STDOUT_FILENO, simpleMessage, strlen(simpleMessage)));
        static const char *errorMessage = "such a cruel place";
        CPPUNIT_ASSERT_EQUAL((ssize_t)strlen(errorMessage), write(STDERR_FILENO, errorMessage, strlen(errorMessage)));
        buffer.m_redirect->process();

        CPPUNIT_ASSERT_EQUAL(std::string(simpleMessage), buffer.m_streams[Logger::INFO].str());
        CPPUNIT_ASSERT_EQUAL(std::string(errorMessage), buffer.m_streams[Logger::DEV].str());
    }

    void overload()
    {
        LogBuffer buffer;

        std::string large;
        large.append(1024, 'h');
        for (int i = 0; i < 4000; i++) {
            CPPUNIT_ASSERT_EQUAL((ssize_t)large.size(), write(STDOUT_FILENO, large.c_str(), large.size()));
        }
        buffer.m_redirect->process();

        CPPUNIT_ASSERT(buffer.m_streams[Logger::INFO].str().size() > large.size());
    }

#ifdef HAVE_GLIB
    void glib()
    {
        static const char *filename = "LogRedirectTest_glib.out";
        int new_stdout = open(filename, O_RDWR|O_CREAT|O_TRUNC, S_IRWXU);

        // check that intercept all glib message and don't print anything to stdout
        int orig_stdout = -1;
        try {
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

            std::string debug = buffer.m_streams[Logger::DEBUG].str();
            std::string dev = buffer.m_streams[Logger::DEV].str();
            CPPUNIT_ASSERT(debug.find("test warning") != debug.npos);
            CPPUNIT_ASSERT(dev.find("normal message stderr") != dev.npos);
        } catch(...) {
            dup2(orig_stdout, STDOUT_FILENO);
            throw;
        }
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
