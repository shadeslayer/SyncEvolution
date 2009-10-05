/*
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

#ifndef INCL_LOGREDIRECT
#define INCL_LOGREDIRECT

#include <syncevo/LogStdout.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

/**
 * Intercepts all text written to stdout or stderr and passes it
 * through the currently active logger, which may or may not be
 * this instance itself. In addition, it catches SIGSEGV, SIGABRT,
 * SIGBUS and processes pending output before shutting down
 * by raising these signals again.
 *
 * The interception is done by replacing the file descriptors
 * 1 and 2. The original file descriptors are preserved; the
 * original FD 1 is used for writing log messages that are
 * intended to reach the user.
 *
 * This class tries to be simple and therefore avoids threads
 * and forking. It intentionally doesn't protect against multiple
 * threads accessing it. This is something that has to be avoided
 * by the user. The redirected output has to be read whenever
 * possible, ideally before producing other log output (process()).
 *
 * Because the same thread that produces the output also reads it,
 * there can be a deadlock if more output is produced than the
 * in-kernel buffers allow. Pipes and stream sockets therefore cannot
 * be used. Unreliable datagram sockets work:
 * - normal write() calls produce packets
 * - if the sender always writes complete lines, the reader
 *   will not split them because it can receive the complete packet
 *
 * Unix Domain datagram sockets would be nice:
 * - socketpair() creates an anonymous connection, no-one else
 *   can send us unwanted data (in contrast to, say, UDP)
 * - unlimited chunk size
 * - *but* packets are *not* dropped if too much output is produced
 *   (found with LogRedirectTest::overload test and confirmed by
 *    "man unix")
 *
 * To avoid deadlocks, UDP sockets have to be used. It has drawbacks:
 * - chunk size limited by maximum size of IP4 packets
 * - more complex to set up (currently assumes that 127.0.0.1 is the
 *   local interface)
 * - anyone running locally can send us log data
 *
 * The implementation contains code for both; UDP is active by default
 * because the potential deadlock is considered more severe than UDP's
 * disadvantages.
 *
 * Because this class is to be used early in the startup
 * of the application and in low-level error scenarios, it
 * must not throw exceptions or return errors. If something
 * doesn't work, it stops redirecting output.
 *
 * Redirection and signal handlers are disabled if the environment
 * variable SYNCEVOLUTION_DEBUG is set (regardless of its value).
 */


class LogRedirect : public LoggerStdout
{
    struct FDs {
        int m_original;     /** the original output FD, 2 for stderr */
        int m_copy;         /** a duplicate of the original output file descriptor */
        int m_write;        /** the write end of the replacement */
        int m_read;         /** the read end of the replacement */
    } m_stdout, m_stderr;
    FILE *m_out;            /** a stream for the normal LogStdout which isn't redirected */
    char *m_buffer;         /** typically fairly small buffer for reading */
    size_t m_len;           /** total length of buffer */
    bool m_processing;      /** flag to detect recursive process() calls */
    static LogRedirect *m_redirect; /**< single active instance, for signal handler */

    // non-virtual helper functions which can always be called,
    // including the constructor and destructor
    void redirect(int original, FDs &fds) throw();
    void restore(FDs &fds) throw();
    void restore() throw();
    void process(FDs &fds) throw();
    static void abortHandler(int sig) throw();

 public:
    /** redirect both stderr and stdout or just stderr */
    LogRedirect(bool both = true) throw();
    ~LogRedirect() throw();

    void process() throw();

    /** format log messages via normal LogStdout and print to a valid stream owned by us */
    virtual void messagev(Level level,
                          const char *prefix,
                          const char *file,
                          int line,
                          const char *function,
                          const char *format,
                          va_list args);
};

SE_END_CXX
#endif // INCL_LOGREDIRECT
