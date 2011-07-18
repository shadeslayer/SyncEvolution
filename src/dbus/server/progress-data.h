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

#ifndef PROGRESS_DATA_H
#define PROGRESS_DATA_H

#include <syncevo/SyncML.h>
#include <sys/types.h>

SE_BEGIN_CXX

/**
 * Hold progress info and try to estimate current progress
 */
class ProgressData {
public:
    /**
     * big steps, each step contains many operations, such as
     * data prepare, message send/receive.
     * The partitions of these steps are based on profiling data
     * for many usage scenarios and different sync modes
     */
    enum ProgressStep {
        /** an invalid step */
        PRO_SYNC_INVALID = 0,
        /**
         * sync prepare step: do some preparations and checkings,
         * such as source preparation, engine preparation
         */
        PRO_SYNC_PREPARE,
        /**
         * session init step: transport connection set up,
         * start a session, authentication and dev info generation
         * normally it needs one time syncML messages send-receive.
         * Sometimes it may need messages send/receive many times to
         * handle authentication
         */
        PRO_SYNC_INIT,
        /**
         * prepare sync data and send data, also receive data from server.
         * Also may need send/receive messages more than one time if too
         * much data.
         * assume 5 items to be sent by default
         */
        PRO_SYNC_DATA,
        /**
         * item receive handling, send client's status to server and
         * close the session
         * assume 5 items to be received by default
         */
        PRO_SYNC_UNINIT,
        /** number of sync steps */
        PRO_SYNC_TOTAL
    };
    /**
     * internal mode to represent whether it is possible that data is sent to
     * server or received from server. This could help remove some incorrect
     * hypothesis. For example, if only to client, then it is no data item
     * sending to server.
     */
    enum InternalMode {
        INTERNAL_NONE = 0,
        INTERNAL_ONLY_TO_CLIENT = 1,
        INTERNAL_ONLY_TO_SERVER = 1 << 1,
        INTERNAL_TWO_WAY = 1 + (1 << 1)
    };

    /**
     * treat a one-time send-receive without data items
     * as an internal standard unit.
     * below are ratios of other operations compared to one
     * standard unit.
     * These ratios might be dynamicall changed in the future.
     */
    /** PRO_SYNC_PREPARE step ratio to standard unit */
    static const float PRO_SYNC_PREPARE_RATIO;
    /** data prepare for data items to standard unit. All are combined by profiling data */
    static const float DATA_PREPARE_RATIO;
    /** one data item send's ratio to standard unit */
    static const float ONEITEM_SEND_RATIO;
    /** one data item receive&parse's ratio to standard unit */
    static const float ONEITEM_RECEIVE_RATIO;
    /** connection setup to standard unit */
    static const float CONN_SETUP_RATIO;
    /** assume the number of data items */
    static const int DEFAULT_ITEMS = 5;
    /** default times of message send/receive in each step */
    static const int MSG_SEND_RECEIVE_TIMES = 1;

    ProgressData(int32_t &progress);

    /**
     * change the big step
     */
    void setStep(ProgressStep step);

    /**
     * calc progress when a message is sent
     */
    void sendStart();

    /**
     * calc progress when a message is received from server
     */
    void receiveEnd();

    /**
     * re-calc progress proportions according to syncmode hint
     * typically, if only refresh-from-client, then
     * client won't receive data items.
     */
    void addSyncMode(SyncMode mode);

    /**
     * calc progress when data prepare for sending
     */
    void itemPrepare();

    /**
     * calc progress when a data item is received
     */
    void itemReceive(const std::string &source, int count, int total);

private:

    /** update progress data */
    void updateProg(float ratio);

    /** dynamically adapt the proportion of each step by their current units */
    void recalc();

    /** internally check sync mode */
    void checkInternalMode();

    /** get total units of current step and remaining steps */
    float getRemainTotalUnits();

    /** get default units of given step */
    static float getDefaultUnits(ProgressStep step);

private:
    /** a reference of progress percentage */
    int32_t &m_progress;
    /** current big step */
    ProgressStep m_step;
    /** count of message send/receive in current step. Cleared in the start of a new step */
    int m_sendCounts;
    /** internal sync mode combinations */
    int m_internalMode;
    /** proportions when each step is end */
    float m_syncProp[PRO_SYNC_TOTAL];
    /** remaining units of each step according to current step */
    float m_syncUnits[PRO_SYNC_TOTAL];
    /** proportion of a standard unit, may changes dynamically */
    float m_propOfUnit;
    /** current sync source */
    std::string m_source;
};

SE_END_CXX

#endif // PROGRESS_DATA_H
