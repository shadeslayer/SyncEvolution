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

#ifndef INCL_LOGSYSLOG
#define INCL_LOGSYSLOG

#include <syncevo/Logging.h>
#include <syncevo/util.h>
#include <stdio.h>
#include <string>

#include <syncevo/declarations.h>
SE_BEGIN_CXX


/**
 * A logger which writes to syslog.
 */
class LoggerSyslog : public LoggerBase
{
    /**
     * Set before writing the first message if log level is debugging,
     * together with printing a message that gives the local time.
     */
    Timespec m_startTime;
    const std::string m_processName;

public:
    /**
     * Write to syslog by default.
     */
    LoggerSyslog(const std::string &processName);

    ~LoggerSyslog();

    virtual void messagev(Level level,
                          const char *prefix,
                          const char *file,
                          int line,
                          const char *function,
                          const char *format,
                          va_list args);

    virtual bool isProcessSafe() const { return true; }
private:
    int getSyslogLevel();
};

SE_END_CXX
#endif // INCL_LOGSYSLOG
