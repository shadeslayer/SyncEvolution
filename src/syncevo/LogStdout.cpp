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

#include <boost/bind.hpp>

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

static void appendOutput(std::string &output, std::string &chunk, size_t expectedTotal)
{
    if (expectedTotal) {
        output.reserve(expectedTotal);
    }
    output.append(chunk);
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
        // TODO: print debugging information, perhaps only in log file
        std::string output;
        formatLines(msglevel, filelevel,
                    m_processName,
                    prefix,
                    format, args,
                    boost::bind(appendOutput, boost::ref(output), _1, _2));
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
