
/*
 * Copyright (C) 2011 Intel Corporation
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

#ifndef TIMER_H
#define TIMER_H

#include <unistd.h>
#include <sys/time.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

/**
 * A timer helper to check whether now is timeout according to
 * user's setting. Timeout is calculated in milliseconds
 */
class Timer {
    timeval m_startTime;  ///< start time
    unsigned long m_timeoutMs; ///< timeout in milliseconds, set by user

    /**
     * calculate duration between now and start time
     * return value is in milliseconds
     */
    unsigned long duration(const timeval &minuend, const timeval &subtrahend)
    {
        unsigned long result = 0;
        if(minuend.tv_sec > subtrahend.tv_sec ||
                (minuend.tv_sec == subtrahend.tv_sec && minuend.tv_usec > subtrahend.tv_usec)) {
            result = minuend.tv_sec - subtrahend.tv_sec;
            result *= 1000;
            result += (minuend.tv_usec - subtrahend.tv_usec) / 1000;
        }
        return result;
    }

 public:
    /**
     * constructor
     * @param timeoutMs timeout in milliseconds
     */
    Timer(unsigned long timeoutMs = 0) : m_timeoutMs(timeoutMs)
    {
        reset();
    }

    /**
     * reset the timer and mark start time as current time
     */
    void reset() { gettimeofday(&m_startTime, NULL); }

    /**
     * check whether it is timeout
     */
    bool timeout()
    {
        return timeout(m_timeoutMs);
    }

    /**
     * check whether the duration timer records is longer than the given duration
     */
    bool timeout(unsigned long timeoutMs)
    {
        timeval now;
        gettimeofday(&now, NULL);
        return duration(now, m_startTime) >= timeoutMs;
    }
};

SE_END_CXX

#endif // TIMER_H
