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

#include <syncevo/LogStdout.h>
#include <string.h>
#include <errno.h>

#include <syncevo/declarations.h>
using namespace std;
SE_BEGIN_CXX


LoggerStdout::LoggerStdout(FILE *file) :
    m_file(file),
    m_closeFile(false)
{}

LoggerStdout::LoggerStdout(const std::string &filename) :
    m_file(fopen(filename.c_str(), "w")),
    m_closeFile(true)
{
    if (!m_file) {
        throw std::string(filename + ": " + strerror(errno));
    }
}

LoggerStdout::~LoggerStdout()
{
    if (m_closeFile) {
        fclose(m_file);
    }
}

void LoggerStdout::messagev(FILE *file,
                            Level msglevel,
                            Level filelevel,
                            const char *prefix,
                            const char *filename,
                            int line,
                            const char *function,
                            const char *format,
                            va_list args)
{
    if (file &&
        msglevel <= filelevel) {
        std::string tag;

        // in case of 'SHOW' level, don't print level and prefix information
        if (msglevel != SHOW) {
            string reltime;
            string procname;
            if (!m_processName.empty()) {
                procname.reserve(m_processName.size() + 1);
                procname += " ";
                procname += m_processName;
            }

            if (filelevel >= DEBUG) {
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
                    fprintf(file, "[DEBUG%s%s] %s UTC = %s\n",
                            procname.c_str(),
                            reltime.c_str(),
                            buffer[0],
                            buffer[1]);
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

        // TODO: print debugging information, perhaps only in log file
        std::string output = StringPrintfV(format, args);

        // Prepare final chunk:
        // - tag at start of each line if not empty
        // - newline at end if not already present
        if (!tag.empty()) {
            std::string buffer;
            // Assume average line length of around 40 characters to
            // predict number of lines.
            buffer.reserve((output.size() / 40 + 1) * tag.size() + output.size());
            bool haveNewline = false;
            size_t pos = 0;
            while (true) {
                size_t next = output.find('\n', pos);
                if (next != output.npos) {
                    buffer.append(tag);
                    buffer.append(output, pos, next + 1 - pos);
                    haveNewline = true;
                    pos = next + 1;
                } else {
                    break;
                }
            }
            if (pos < output.size() || output.empty()) {
                // handle dangling last line or empty chunk (don't
                // want empty line for that, print at least the tag)
                buffer.append(tag);
                buffer.append(output, pos, output.size() - pos);
                haveNewline = false;
            }
            if (!haveNewline) {
                buffer += '\n';
            }
            output = buffer;
        } else if (!boost::ends_with(output, "\n")) {
            // append newline if necessary
            output += '\n';
        }

        fwrite(output.c_str(), 1, output.size(), file);
        fflush(file);
    }
}

void LoggerStdout::messagev(Level level,
                            const char *prefix,
                            const char *file,
                            int line,
                            const char *function,
                            const char *format,
                            va_list args)
{
    messagev(m_file, level, getLevel(),
             prefix, file, line, function,
             format, args);
}

SE_END_CXX
