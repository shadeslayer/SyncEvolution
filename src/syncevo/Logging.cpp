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

#include <syncevo/declarations.h>
SE_BEGIN_CXX

static LoggerStdout DefaultLogger;

static std::vector<LoggerBase *> loggers;
LoggerBase &LoggerBase::instance()
{
    if (!loggers.empty()) {
        return *loggers[loggers.size() - 1];
    } else {
        return DefaultLogger;
    }
}

void LoggerBase::pushLogger(LoggerBase *logger)
{
    loggers.push_back(logger);
}

void LoggerBase::popLogger()
{
    if (loggers.empty()) {
        throw "too many popLogger() calls";
    } else {
        loggers.pop_back();
    }
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
    case ERROR: return "ERROR";
    case WARNING: return "WARNING";
    case INFO: return "INFO";
    case DEV: return "DEVELOPER";
    case DEBUG: return "DEBUG";
    default: return "???";
    }
}

SE_END_CXX
