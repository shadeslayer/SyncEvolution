/*
 * Copyright (C) 2009 Patrick Ohly <patrick.ohly@gmx.de>
 * Copyright (C) 2009 Intel Corporation
 */

#include "LogStdout.h"
#include <string.h>
#include <errno.h>

namespace SyncEvolution {

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
        // TODO: print time
        fprintf(file, "[%s] ", levelToStr(msglevel));
        if (prefix) {
            fprintf(file, "%s: ", prefix);
        }
        // TODO: print debugging information, perhaps only in log file
        vfprintf(file, format, args);
        // TODO: add newline only when needed, add prefix to all lines
        fprintf(file, "\n");
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

}
