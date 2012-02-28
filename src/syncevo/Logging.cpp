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

#include <syncevo/Logging.h>
#include <syncevo/LogStdout.h>

#include <vector>
#include <string.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

std::string Logger::m_processName;

static std::vector<LoggerBase *> &loggers()
{
    // allocate array once and never free it because it might be needed till
    // the very end of the application life cycle
    static std::vector<LoggerBase *> *loggers = new std::vector<LoggerBase *>;
    return *loggers;
}

LoggerBase &LoggerBase::instance()
{
    // prevent destructing this instance as part of the executable's
    // shutdown by allocating it dynamically, because it may be
    // needed by other instances that get destructed later
    // (order of global instance construction/destruction is
    // undefined)
    static LoggerStdout *DefaultLogger = new LoggerStdout;
    if (!loggers().empty()) {
        return *loggers()[loggers().size() - 1];
    } else {
        return *DefaultLogger;
    }
}

void LoggerBase::pushLogger(LoggerBase *logger)
{
    loggers().push_back(logger);
}

void LoggerBase::popLogger()
{
    if (loggers().empty()) {
        throw "too many popLogger() calls";
    } else {
        loggers().pop_back();
    }
}

int LoggerBase::numLoggers()
{
    return (int)loggers().size();
}

LoggerBase *LoggerBase::loggerAt(int index)
{
    return index < 0 || index >= (int)loggers().size() ?
        NULL :
        loggers()[index];
}


void Logger::message(Level level,
                     const char *prefix,
                     const char *file,
                     int line,
                     const char *function,
                     const char *format,
                     ...)
{
    va_list args;
    va_start(args, format);
    messagev(level, prefix, file, line, function, format, args);
    va_end(args);
}

const char *Logger::levelToStr(Level level)
{
    switch (level) {
    case SHOW: return "SHOW";
    case ERROR: return "ERROR";
    case WARNING: return "WARNING";
    case INFO: return "INFO";
    case DEV: return "DEVELOPER";
    case DEBUG: return "DEBUG";
    default: return "???";
    }
}

Logger::Level Logger::strToLevel(const char *str)
{
    // order is based on a rough estimate of message frequency of the
    // corresponding type
    if (!str || !strcmp(str, "DEBUG")) {
        return DEBUG;
    } else if (!strcmp(str, "INFO")) {
        return INFO;
    } else if (!strcmp(str, "SHOW")) {
        return SHOW;
    } else if (!strcmp(str, "ERROR")) {
        return ERROR;
    } else if (!strcmp(str, "WARNING")) {
        return WARNING;
    } else if (!strcmp(str, "DEV")) {
        return DEV;
    } else {
        return DEBUG;
    }
}

#ifdef HAVE_GLIB
void Logger::glogFunc(const gchar *logDomain,
                      GLogLevelFlags logLevel,
                      const gchar *message,
                      gpointer userData)
{
    LoggerBase::instance().message((logLevel & (G_LOG_LEVEL_ERROR|G_LOG_LEVEL_CRITICAL)) ? ERROR :
                                   (logLevel & G_LOG_LEVEL_WARNING) ? WARNING :
                                   (logLevel & (G_LOG_LEVEL_MESSAGE|G_LOG_LEVEL_INFO)) ? SHOW :
                                   DEBUG,
                                   NULL,
                                   NULL,
                                   0,
                                   NULL,
                                   "%s%s%s",
                                   logDomain ? logDomain : "",
                                   logDomain ? ": " : "",
                                   message);
}

#endif

SE_END_CXX
