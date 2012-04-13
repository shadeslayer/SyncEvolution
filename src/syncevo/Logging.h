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

#ifndef INCL_LOGGING
#define INCL_LOGGING

#include <stdarg.h>
#include <string>

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#ifdef HAVE_GLIB
# include <glib.h>
#endif

#include <syncevo/Timespec.h>

#include <boost/function.hpp>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

/**
 * Abstract interface for logging in SyncEvolution.  Can be
 * implemented by other classes to add information (like a certain
 * prefix) before passing the message on to a global instance for the
 * actual processing.
 */
class Logger
{
 public:
    /**
     * Which of these levels is the right one for a certain message
     * is a somewhat subjective choice. Here is a definition how they
     * are supposed to be used:
     * - error: severe problem which the user and developer have to
     *          know about
     * - warning: a problem that was handled, but users and developers
     *            probably will want to know about
     * - info: information about a sync session which the user
     *         will want to read during/after each sync session
     * - developer: information about a sync session that is not
     *              interesting for a user (for example, because it
     *              is constant and already known) but which should
     *              be in each log because developers need to know
     *              it. Messages logged with this calls will be included
     *              at LOG_LEVEL_INFO, therefore messages should be small and
     *              not recur so that the log file size remains small.
     * - debug: most detailed logging, messages may be arbitrarily large
     *
     * Here is a decision tree which helps to pick the right level:
     * - an error: => ERROR
     * - a non-fatal error: => WARNING
     * - it changes during each sync or marks important steps
     *   in the sync: INFO
     * - same as before, but without the [INFO] prefix added to each line: => SHOW
     * - small, non-recurring message which is important for developers
     *   who read a log produced at LOG_LEVEL_INFO: DEVELOPER
     * - everything else: DEBUG
     */
    typedef enum {
        /**
         * only error messages printed
         */
        ERROR,
        /**
         * error and warning messages printed
         */
        WARNING,
        /**
         * "Normal" stdout output which is meant to be seen by a
         * user.
         */
        SHOW,
        /**
         * errors and info messages for users and developers will be
         * printed: use this to keep the output consise and small
         */
        INFO,
        /**
         * important messages to developers
         */
        DEV,
        /**
         * all messages will be printed, including detailed debug
         * messages
         */
        DEBUG
    } Level;
    static const char *levelToStr(Level level);

    /** always returns a valid level, also for NULL, by falling back to DEBUG */
    static Level strToLevel(const char *str);

    /**
     * additional, short string identifying the SyncEvolution process;
     * empty if master process
     *
     * Included by LoggerStdout in the [INFO/DEBUG/...] tag.
     */
    static void setProcessName(const std::string &name) { m_processName = name; }
    static std::string getProcessName() { return m_processName; }

#ifdef HAVE_GLIB
    /**
     * can be used in g_log_set_handler() to redirect log messages
     * into our own logging; must be called for each log domain
     * that may be relevant
     */
    static void glogFunc(const gchar *logDomain,
                         GLogLevelFlags logLevel,
                         const gchar *message,
                         gpointer userData);
#endif

    virtual ~Logger() {}

    /**
     * output a single message
     *
     * @param level     level for current message
     * @param prefix    inserted at beginning of each line, if non-NULL
     * @param file      source file where message comes from, if non-NULL
     * @param line      source line number, if file is non-NULL
     * @param function  surrounding function name, if non-NULL
     * @param format    sprintf format
     * @param args      parameters for sprintf: consumed by this function, 
     *                  make copy with va_copy() if necessary!
     */
    virtual void messagev(Level level,
                          const char *prefix,
                          const char *file,
                          int line,
                          const char *function,
                          const char *format,
                          va_list args) = 0;

    /** default: redirect into messagev() */
    virtual void message(Level level,
                         const char *prefix,
                         const char *file,
                         int line,
                         const char *function,
                         const char *format,
                         ...)
#ifdef __GNUC__
        __attribute__((format(printf, 7, 8)))
#endif
        ;

    /**
     * this logger instance can be used by multiple processes:
     * true for those which write single lines, false
     * for more complicated output like HTML (Synthesis log)
     */
    virtual bool isProcessSafe() const = 0;        

 protected:
    static std::string m_processName;
};

/**
 * Global logging, implemented as a singleton with one instance per
 * process.
 *
 * @TODO avoid global variable
 */
class LoggerBase : public Logger
{
 public:
     LoggerBase() : m_level(INFO) {}

    /**
     * Grants access to the singleton which implements logging.
     * The implementation of this function and thus the Log
     * class itself is platform specific: if no Log instance
     * has been set yet, then this call has to create one.
     */
    static LoggerBase &instance();

    /**
     * Overrides the default Logger implementation. The Logger class
     * itself will never delete the active logger.
     *
     * @param logger    will be used for all future logging activities
     */

    static void pushLogger(LoggerBase *logger);
    /**
     * Remove the current logger and restore previous one.
     * Must match a pushLogger() call.
     */
    static void popLogger();

    /** total number of active loggers */
    static int numLoggers();

    /**
     * access to active logger
     * @param index    0 for oldest (inner-most) logger
     * @return pointer or NULL for invalid index
     */
    static LoggerBase *loggerAt(int index);

    virtual void setLevel(Level level) { m_level = level; }
    virtual Level getLevel() { return m_level; }

 protected:
    /**
     * Prepares the output. The result is passed back to the caller
     * line-by-line (expectedTotal > 0) and/or as full chunk
     * (expectedTotal = 0). The expected size is just a guess, be
     * prepared to handle more output.
     *
     * Each chunk already includes the necessary line breaks (in
     * particular after the last line when it contains the entire
     * output). It may be modified by the callback.
     */
    void formatLines(Level msglevel,
                     Level outputlevel,
                     const std::string &processName,
                     const char *prefix,
                     const char *format,
                     va_list args,
                     boost::function<void (std::string &chunk, size_t expectedTotal)> print);

 private:
    Level m_level;

    /**
     * Set by formatLines() before writing the first message if log
     * level is debugging, together with printing a message that gives
     * the local time.
     */
    Timespec m_startTime;
};


/**
 * Vararg macro which passes the message through a specific
 * Logger class instance (if non-NULL) and otherwise calls
 * the global logger directly. Adds source file and line.
 *
 * @TODO make source and line info optional for release
 * @TODO add function name (GCC extension)
 */
#define SE_LOG(_level, _instance, _prefix, _format, _args...) \
    do { \
        if (_instance) { \
            static_cast<Logger *>(_instance)->message(_level, \
                                                                     _prefix, \
                                                                     __FILE__, \
                                                                     __LINE__, \
                                                                     0, \
                                                                     _format, \
                                                                     ##_args); \
        } else { \
            LoggerBase::instance().message(_level, \
                                                          _prefix, \
                                                          __FILE__, \
                                                          __LINE__, \
                                                          0, \
                                                          _format, \
                                                          ##_args); \
        } \
    } while(false)

#define SE_LOG_SHOW(_instance, _prefix, _format, _args...) SE_LOG(Logger::SHOW, _instance, _prefix, _format, ##_args)
#define SE_LOG_ERROR(_instance, _prefix, _format, _args...) SE_LOG(Logger::ERROR, _instance, _prefix, _format, ##_args)
#define SE_LOG_WARNING(_instance, _prefix, _format, _args...) SE_LOG(Logger::WARNING, _instance, _prefix, _format, ##_args)
#define SE_LOG_INFO(_instance, _prefix, _format, _args...) SE_LOG(Logger::INFO, _instance, _prefix, _format, ##_args)
#define SE_LOG_DEV(_instance, _prefix, _format, _args...) SE_LOG(Logger::DEV, _instance, _prefix, _format, ##_args)
#define SE_LOG_DEBUG(_instance, _prefix, _format, _args...) SE_LOG(Logger::DEBUG, _instance, _prefix, _format, ##_args)
 
SE_END_CXX
#endif // INCL_LOGGING
