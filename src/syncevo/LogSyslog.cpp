/*
 * Copyright (C) 2012 Intel Corporation
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

#include <syncevo/LogSyslog.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>

#include <boost/bind.hpp>

#include <syncevo/declarations.h>
using namespace std;
SE_BEGIN_CXX

LoggerSyslog::LoggerSyslog(const std::string &processName)
  : m_processName(processName)
{
    openlog(m_processName.c_str(), LOG_CONS | LOG_NDELAY | LOG_PID, LOG_USER);
    LoggerBase::pushLogger(this);
}

LoggerSyslog::~LoggerSyslog()
{
    closelog();
    LoggerBase::popLogger();
}

static void printToSyslog(int sysloglevel, std::string &chunk, size_t expectedTotal)
{
    if (!expectedTotal) {
        // Might contain line breaks in the middle, split it.
        size_t pos = 0;
        while(true) {
            size_t next = chunk.find('\n', pos);
            if (next == chunk.npos) {
                // Line break is guaranteed to be last character,
                // so we have printed everything now.
                return;
            }
            chunk[next] = 0;
            syslog(sysloglevel, "%s", chunk.c_str() + pos);
            pos = next + 1;
        }
    } else {
        // Single line. We can print the trailing line break.
        syslog(sysloglevel, "%s", chunk.c_str());
    }
}

void LoggerSyslog::messagev(Level level,
                            const char *prefix,
                            const char *file,
                            int line,
                            const char *function,
                            const char *format,
                            va_list args)
{

    if (level <= getLevel()) {
        formatLines(level, getLevel(),
                    "", // process name is set when opening the syslog
                    prefix,
                    format, args,
                    boost::bind(printToSyslog, getSyslogLevel(level), _1, _2));
    }
}

int LoggerSyslog::getSyslogLevel(Level level)
{
    switch (level) {
    case ERROR:
        return LOG_ERR;
    case WARNING:
        return LOG_WARNING;
    case SHOW:
        return LOG_NOTICE;
    case INFO:
    case DEV:
        return LOG_INFO;
    case DEBUG:
    default:
        return LOG_DEBUG;
    }
}

SE_END_CXX
