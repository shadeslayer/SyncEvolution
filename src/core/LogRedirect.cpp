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

#include <config.h>
#include "LogRedirect.h"
#include "Logging.h"
#include "SyncEvolutionUtil.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <algorithm>

namespace SyncEvolution {

LogRedirect::LogRedirect() throw()
{
    m_processing = false;
    m_buffer = NULL;
    m_len = 0;
    redirect(1, m_stdout);
    redirect(2, m_stderr);
    m_out = fdopen(dup(m_stdout.m_copy), "w");
    if (!m_out) {
        restore(m_stdout);
        restore(m_stderr);
        perror("LogRedirect fdopen");
    }
    LoggerBase::pushLogger(this);
}

LogRedirect::~LogRedirect() throw()
{
    process();
    restore(m_stdout);
    restore(m_stderr);
    if (m_out) {
        fclose(m_out);
    }
    if (m_buffer) {
        free(m_buffer);
    }
    LoggerBase::popLogger();
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
        ssize_t available;
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
            LoggerBase::instance().message(fds.m_original == 2 ? Logger::ERROR : Logger::INFO,
                                           fds.m_original == 2 ? "stderr" : NULL,
                                           NULL, 0, NULL,
                                           "%s", m_buffer);
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

} // namespace SyncEvolution

#ifdef ENABLE_UNIT_TESTS
#include <cppunit/extensions/HelperMacros.h>

class LogRedirectTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(LogRedirectTest);
    CPPUNIT_TEST(simple);
    CPPUNIT_TEST(largeChunk);
    CPPUNIT_TEST(streams);
    CPPUNIT_TEST(overload);
    CPPUNIT_TEST_SUITE_END();

    /**
     * redirect stdout/stderr, then intercept the log messages and
     * store them for inspection
     */
    class LogBuffer : public SyncEvolution::LoggerBase
    {
    public:
        std::stringstream m_streams[DEBUG + 1];
        SyncEvolution::LogRedirect *m_redirect;

        LogBuffer()
        {
            m_redirect = new SyncEvolution::LogRedirect();
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
        CPPUNIT_ASSERT_EQUAL((ssize_t)strlen(simpleMessage), write(1, simpleMessage, strlen(simpleMessage)));
        buffer.m_redirect->process();

        CPPUNIT_ASSERT_EQUAL(buffer.m_streams[SyncEvolution::Logger::INFO].str(), std::string(simpleMessage));
    }

    void largeChunk()
    {
        LogBuffer buffer;

        std::string large;
        large.append(60 * 1024, 'h');
        CPPUNIT_ASSERT_EQUAL((ssize_t)large.size(), write(1, large.c_str(), large.size()));
        buffer.m_redirect->process();

        CPPUNIT_ASSERT_EQUAL(large.size(), buffer.m_streams[SyncEvolution::Logger::INFO].str().size());
        CPPUNIT_ASSERT_EQUAL(large, buffer.m_streams[SyncEvolution::Logger::INFO].str());
    }

    void streams()
    {
        LogBuffer buffer;

        static const char *simpleMessage = "hello world";
        CPPUNIT_ASSERT_EQUAL((ssize_t)strlen(simpleMessage), write(1, simpleMessage, strlen(simpleMessage)));
        static const char *errorMessage = "such a cruel place";
        CPPUNIT_ASSERT_EQUAL((ssize_t)strlen(errorMessage), write(2, errorMessage, strlen(errorMessage)));
        buffer.m_redirect->process();

        CPPUNIT_ASSERT_EQUAL(std::string(simpleMessage), buffer.m_streams[SyncEvolution::Logger::INFO].str());
        CPPUNIT_ASSERT_EQUAL(std::string(errorMessage), buffer.m_streams[SyncEvolution::Logger::ERROR].str());
    }

    void overload()
    {
        LogBuffer buffer;

        std::string large;
        large.append(1024, 'h');
        for (int i = 0; i < 4000; i++) {
            CPPUNIT_ASSERT_EQUAL((ssize_t)large.size(), write(1, large.c_str(), large.size()));
        }
        buffer.m_redirect->process();

        CPPUNIT_ASSERT(buffer.m_streams[SyncEvolution::Logger::INFO].str().size() > large.size());
    }

};

SYNCEVOLUTION_TEST_SUITE_REGISTRATION(LogRedirectTest);

#endif // ENABLE_UNIT_TESTS

