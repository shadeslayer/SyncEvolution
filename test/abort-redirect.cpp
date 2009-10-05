/*
 * Copyright (C) 2009 Patrick Ohly <patrick.ohly@gmx.de>
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

#include <syncevo/LogRedirect.h>
#include <syncevo/LogStdout.h>

#include <stdlib.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

int main(int argc, char **argv)
{
    // Check that we catch stderr message generated
    // while the process shut down. We assume that
    // libc detects double frees. The expected
    // outcome of this program is that the message
    // appears in abort-redirect.log instead of
    // stderr. A core file should be written normally.

    LogRedirect redirect;
    LoggerStdout out(fopen("abort-redirect.log", "w"));
    out.pushLogger(&out);

    // write without explicit flushing
    fprintf(stdout, "a normal info message, also redirected");

    // cause libc error and abort: for small chunks
    // glibc tends to detect double frees while large
    // chunks are done as mmap()/munmap() and just
    // segfault
    void *small = malloc(1);
    free(small);
    free(small);
    void *large = malloc(1024 * 1024);
    free(large);
    free(large);

    return 0;
}

SE_END_CXX
