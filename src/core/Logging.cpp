/*
 * Copyright (C) 2009 Patrick Ohly <patrick.ohly@gmx.de>
 * Copyright (C) 2009 Intel Corporation
 */

#include "Logging.h"
#include "LogStdout.h"

#include <vector>

namespace SyncEvolution {

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


}
