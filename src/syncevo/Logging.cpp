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

void LoggerBase::formatLines(Level msglevel,
                             Level outputlevel,
                             const std::string &processName,
                             const char *prefix,
                             const char *format,
                             va_list args,
                             boost::function<void (std::string &buffer, size_t expectedTotal)> print)
{
    std::string tag;

    // in case of 'SHOW' level, don't print level and prefix information
    if (msglevel != SHOW) {
        std::string reltime;
        std::string procname;
        if (!processName.empty()) {
            procname.reserve(processName.size() + 1);
            procname += " ";
            procname += m_processName;
        }

        if (outputlevel >= DEBUG) {
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
                reltime = " 00:00:00";
                strftime(buffer[0], sizeof(buffer[0]),
                         "%a %Y-%m-%d %H:%M:%S",
                         &tm_gm);
                strftime(buffer[1], sizeof(buffer[1]),
                         "%H:%M %z %Z",
                         &tm_local);
                std::string line =
                    StringPrintf("[DEBUG%s%s] %s UTC = %s\n",
                                 procname.c_str(),
                                 reltime.c_str(),
                                 buffer[0],
                                 buffer[1]);
                print(line, 1);
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
        tag = StringPrintf("[%s%s%s] %s%s",
                           levelToStr(msglevel),
                           procname.c_str(),
                           reltime.c_str(),
                           prefix ? prefix : "",
                           prefix ? ": " : "");
    }

    std::string output = StringPrintfV(format, args);

    if (!tag.empty()) {
        // Print individual lines.
        //
        // Total size is guessed by assuming an average line length of
        // around 40 characters to predict number of lines.
        size_t expectedTotal = (output.size() / 40 + 1) * tag.size() + output.size();
        size_t pos = 0;
        while (true) {
            size_t next = output.find('\n', pos);
            if (next != output.npos) {
                std::string line;
                line.reserve(tag.size() + next + 1 - pos);
                line.append(tag);
                line.append(output, pos, next + 1 - pos);
                print(line, expectedTotal);
                pos = next + 1;
            } else {
                break;
            }
        }
        if (pos < output.size() || output.empty()) {
            // handle dangling last line or empty chunk (don't
            // want empty line for that, print at least the tag)
            std::string line;
            line.reserve(tag.size() + output.size() - pos + 1);
            line.append(tag);
            line.append(output, pos, output.size() - pos);
            line += '\n';
            print(line, expectedTotal);
        }
    } else {
        if (!boost::ends_with(output, "\n")) {
            // append newline if necessary
            output += '\n';
        }
        print(output, 0);
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
