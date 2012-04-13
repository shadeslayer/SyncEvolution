/*
 * Copyright (C) 2008-2009 Patrick Ohly <patrick.ohly@gmx.de>
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

#ifndef INCL_SYNCEVOLUTION_TIME
# define INCL_SYNCEVOLUTION_TIME

#include <time.h>
#include <sys/types.h>
#include <sys/select.h>
#include <unistd.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

/**
 * Sub-second time stamps. Thin wrapper around timespec
 * and clock_gettime() (for monotonic time). Comparisons
 * assume normalized values (tv_nsec >= 0, < 1e9). Addition
 * and substraction produce normalized values, as long
 * as the result is positive. Substracting a - b where a < b
 * leads to an undefined result.
 */
class Timespec : public timespec
{
 public:
    Timespec() { tv_sec = 0; tv_nsec = 0; }
    Timespec(time_t sec, long nsec) { tv_sec = sec; tv_nsec = nsec; }

    bool operator < (const Timespec &other) const {
        return tv_sec < other.tv_sec ||
            (tv_sec == other.tv_sec && tv_nsec < other.tv_nsec);
    }
    bool operator > (const Timespec &other) const {
        return tv_sec > other.tv_sec ||
            (tv_sec == other.tv_sec && tv_nsec > other.tv_nsec);
    }
    bool operator <= (const Timespec &other) const { return !(*this > other); }
    bool operator >= (const Timespec &other) const { return !(*this < other); }

    operator bool () const { return tv_sec || tv_nsec; }

    Timespec operator + (int seconds) const { return Timespec(tv_sec + seconds, tv_nsec); }
    Timespec operator - (int seconds) const { return Timespec(tv_sec - seconds, tv_nsec); }
    Timespec operator + (unsigned seconds) const { return Timespec(tv_sec + seconds, tv_nsec); }
    Timespec operator - (unsigned seconds) const { return Timespec(tv_sec - seconds, tv_nsec); }
    Timespec operator + (const Timespec &other) const;
    Timespec operator - (const Timespec &other) const;

    operator timeval () const { timeval res; res.tv_sec = tv_sec; res.tv_usec = tv_nsec / 1000; return res; }

    time_t seconds() const { return tv_sec; }
    long nsecs() const { return tv_nsec; }
    double duration() const { return (double)tv_sec + ((double)tv_nsec) / 1e9;  }

    static Timespec monotonic() { Timespec res; clock_gettime(CLOCK_MONOTONIC, &res); return res; }
    static Timespec system() { Timespec res; clock_gettime(CLOCK_REALTIME, &res); return res; }
};

SE_END_CXX
#endif // INCL_SYNCEVOLUTION_TIME
