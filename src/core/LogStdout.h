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

#ifndef INCL_LOGSTDOUT
#define INCL_LOGSTDOUT

#include "Logging.h"
#include <stdio.h>

namespace SyncEvolution {

/**
 * A logger which writes to stdout or a file.
 */
class LoggerStdout : public LoggerBase
{
 public:
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
