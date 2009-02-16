/*
 * Copyright (C) 2009 Patrick Ohly
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "Logging.h"
#include "LogStdout.h"

namespace SyncEvolution {

static LoggerStdout DefaultLogger;

LoggerBase *LoggerBase::m_logger;
LoggerBase &LoggerBase::instance()
{
    if (m_logger) {
        return *m_logger;
    } else {
        return DefaultLogger;
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
