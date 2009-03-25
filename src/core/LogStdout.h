/*
 * Copyright (C) 2009 Patrick Ohly <patrick.ohly@gmx.de>
 * Copyright (C) 2009 Intel Corporation
 */

#ifndef INCL_LOGSTDOUT
#define INCL_LOGSTDOUT

#include "Logging.h"
#include <stdio.h>
#include <string>

namespace SyncEvolution {

/**
 * A logger which writes to stdout or a file.
 */
class LoggerStdout : public LoggerBase
{
    FILE *m_file;
    bool m_closeFile;

 public:
    /**
     * write to stdout by default
     *
     * @param file    override default file; NULL disables printing
     */
    LoggerStdout(FILE *file = stdout);

    /**
     * open and own the given log file
     *
     * @param filename     will be opened relative to current directory
     */
    LoggerStdout(const std::string &filename);

    ~LoggerStdout();

    virtual void messagev(FILE *file,
                          Level msglevel,
                          Level filelevel,
                          const char *prefix,
                          const char *filename,
                          int line,
                          const char *function,
                          const char *format,
                          va_list args);
    virtual void messagev(Level level,
                          const char *prefix,
                          const char *file,
                          int line,
                          const char *function,
                          const char *format,
                          va_list args);
};

} // namespace

#endif // INCL_LOGSTDOUT
