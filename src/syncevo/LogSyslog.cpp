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

void LoggerSyslog::messagev(Level level,
                            const char *prefix,
                            const char *file,
                            int line,
                            const char *function,
                            const char *format,
                            va_list args)
{

    if (level <= getLevel()) {
        std::string syslog_prefix;

        if (level != SHOW) {
            std::string reltime;

            if (getLevel() >= DEBUG) {
                // add relative time stamp
                Timespec now = Timespec::monotonic();
                if (!m_startTime) {
                    // first message, start counting time
                    m_startTime = now;
                    time_t nowt = time(NULL);
                    struct tm tm_gm, tm_local;
                    char buffer[2][80];

                    gmtime_r(&nowt, &tm_gm);
                    localtime_r(&nowt, &tm_local);
                    strftime(buffer[0], sizeof(buffer[0]),
                             "%a %Y-%m-%d %H:%M:%S",
                             &tm_gm);
                    strftime(buffer[1], sizeof(buffer[1]),
                             "%H:%M %z %Z",
                             &tm_local);
                    syslog(LOG_DEBUG, "[DEBUG 00:00:00] %s UTC = %s\n", buffer[0], buffer[1]);
                } else {
                    if (now >= m_startTime) {
                        Timespec delta = now - m_startTime;
                        reltime = StringPrintf(" %02ld:%02ld:%02ld",
                                               delta.tv_sec / (60 * 60),
                                               (delta.tv_sec % (60 * 60)) / 60,
                                               delta.tv_sec % 60);
                    } else {
                        reltime = " ??:??:??";
                    }
                }
            }

            // in case of 'SHOW' level, don't print level and prefix information
            syslog_prefix = StringPrintf("[%s%s] %s%s",
                                         levelToStr(level),
                                         reltime.c_str(),
                                         prefix ? prefix : "",
                                         prefix ? ": " : "");
        }

        std::string syslog_string(StringPrintfV(format, args));

        // prepend syslog_prefix to syslog_string
        syslog_string.insert(0, syslog_prefix);
        syslog(getSyslogLevel(), "%s\n", syslog_string.c_str());
    }
}

int LoggerSyslog::getSyslogLevel()
{
    switch (getLevel()) {
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
