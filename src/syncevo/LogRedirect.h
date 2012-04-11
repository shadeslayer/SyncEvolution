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
#include <syncevo/util.h>

#include <string>
#include <set>

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
 *
 * In contrast to stderr, stdout is only passed into the logging
 * system as complete lines. That's because it may include data (like
 * synccompare output) which is not printed line-oriented and
 * inserting line breaks (as the logging system does) is undesirable.
 * If an output packet does not end in a line break, that last line
 * is buffered and written together with the next packet, or in flush().
 */
class LogRedirect : public LoggerStdout
{
 public:
    struct FDs {
        int m_original;     /** the original output FD, 2 for stderr */
        int m_copy;         /** a duplicate of the original output file descriptor */
        int m_write;        /** the write end of the replacement */
        int m_read;         /** the read end of the replacement */
    };

    /** ignore any error output containing "error" */
    static void addIgnoreError(const std::string &error) { m_knownErrors.insert(error); }

 private:
    FDs m_stdout, m_stderr;
    bool m_streams;         /**< using reliable streams instead of UDP */
    FILE *m_out;            /** a stream for Logger::SHOW output which isn't redirected */
    FILE *m_err;            /** corresponding stream for any other output */
    char *m_buffer;         /** typically fairly small buffer for reading */
    std::string m_stdoutData;  /**< incomplete stdout line */
    size_t m_len;           /** total length of buffer */
    bool m_processing;      /** flag to detect recursive process() calls */
    static LogRedirect *m_redirect; /**< single active instance, for signal handler */
    static std::set<std::string> m_knownErrors; /** texts contained in errors which are to be ignored */

    // non-virtual helper functions which can always be called,
    // including the constructor and destructor
    void redirect(int original, FDs &fds) throw();
    void restore(FDs &fds) throw();
    void restore() throw();
    /** @return true if data was available */
    bool process(FDs &fds) throw();
    static void abortHandler(int sig) throw();

    /**
     * ignore error messages containing text listed in
     * SYNCEVOLUTION_SUPPRESS_ERRORS env variable (new-line
     * separated)
     */
    bool ignoreError(const std::string &text);

    void init();

 public:
    /** 
     * Redirect both stderr and stdout or just stderr,
     * using UDP so that we don't block when not reading
     * redirected output.
     *
     * messagev() only writes messages to the previous stdout
     * or the optional file which pass the filtering (relevant,
     * suppress known errors, ...).
     */
    LogRedirect(bool both = true, const char *filename = NULL) throw();
    ~LogRedirect() throw();

    /**
     * re-initialize redirection after a fork:
     * - closes inherited file descriptors, except for the original output file descriptor
     * - sets up new sockets
     */
    void redoRedirect() throw();

    /**
     * Meant to be used for redirecting output of a specific command
     * via fork()/exec(). Prepares reliable streams, as determined by
     * ExecuteFlags, without touch file descriptor 1 and 2 and without
     * installing itself as logger. In such an instance, process()
     * will block until both streams get closed on the writing end.
     */
    LogRedirect(ExecuteFlags flags);

    /** true if stdout is redirected */
    static bool redirectingStdout() { return m_redirect && m_redirect->m_stdout.m_read > 0; }

    /** true if stderr is redirected */
    static bool redirectingStderr() { return m_redirect && m_redirect->m_stderr.m_read > 0; }

    /** reset any redirection, if active */
    static void reset() {
        if (m_redirect) {
            m_redirect->flush();
            m_redirect->restore();
        }
    }

    const FDs &getStdout() { return m_stdout; }
    const FDs &getStderr() { return m_stderr; }

    /**
     * Read currently available redirected output and handle it.
     *
     * When using unreliable output redirection, it will always
     * keep going without throwing exceptions. When using reliable
     * redirection and a fatal error occurs, then and exception
     * is thrown.
     */
    void process();

    /** same as process(), but also dump all cached output */
    void flush() throw();

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
