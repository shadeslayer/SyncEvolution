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

#include "progress-data.h"
#include <cmath>
#include <limits>

SE_BEGIN_CXX

const float ProgressData::PRO_SYNC_PREPARE_RATIO = 0.2;
const float ProgressData::DATA_PREPARE_RATIO = 0.10;
const float ProgressData::ONEITEM_SEND_RATIO = 0.05;
const float ProgressData::ONEITEM_RECEIVE_RATIO = 0.05;
const float ProgressData::CONN_SETUP_RATIO = 0.5;

ProgressData::ProgressData(int32_t &progress)
    : m_progress(progress),
    m_step(PRO_SYNC_INVALID),
    m_sendCounts(0),
    m_internalMode(INTERNAL_NONE)
{
    /**
     * init default units of each step
     */
    float totalUnits = 0.0;
    for(int i = 0; i < PRO_SYNC_TOTAL; i++) {
        float units = getDefaultUnits((ProgressStep)i);
        m_syncUnits[i] = units;
        totalUnits += units;
    }
    m_propOfUnit = 1.0 / totalUnits;

    /**
     * init default sync step proportions. each step stores proportions of
     * its previous steps and itself.
     */
    m_syncProp[0] = 0;
    for(int i = 1; i < PRO_SYNC_TOTAL - 1; i++) {
        m_syncProp[i] = m_syncProp[i - 1] + m_syncUnits[i] / totalUnits;
    }
    m_syncProp[PRO_SYNC_TOTAL - 1] = 1.0;
}

void ProgressData::setStep(ProgressStep step)
{
    if(m_step != step) {
        /** if state is changed, progress is set as the end of current step*/
        m_progress = 100.0 * m_syncProp[(int)m_step];
        m_step = step; ///< change to new state
        m_sendCounts = 0; ///< clear send/receive counts
        m_source = ""; ///< clear source
    }
}

void ProgressData::sendStart()
{
    checkInternalMode();
    m_sendCounts++;

    /* self adapts. If a new send and not default, we need re-calculate proportions */
    if(m_sendCounts > MSG_SEND_RECEIVE_TIMES) {
        m_syncUnits[(int)m_step] += 1;
        recalc();
    }
    /**
     * If in the send operation of PRO_SYNC_UNINIT, it often takes extra time
     * to send message due to items handling
     */
    if(m_step == PRO_SYNC_UNINIT && m_syncUnits[(int)m_step] != MSG_SEND_RECEIVE_TIMES) {
        updateProg(DATA_PREPARE_RATIO);
    }
}

void ProgressData::receiveEnd()
{
    /**
     * often receiveEnd is the last operation of each step by default.
     * If more send/receive, then we need expand proportion of current
     * step and re-calc them
     */
    updateProg(m_syncUnits[(int)m_step]);
}

void ProgressData::addSyncMode(SyncMode mode)
{
    switch(mode) {
        case SYNC_TWO_WAY:
        case SYNC_SLOW:
            m_internalMode |= INTERNAL_TWO_WAY;
            break;
        case SYNC_ONE_WAY_FROM_CLIENT:
        case SYNC_REFRESH_FROM_CLIENT:
            m_internalMode |= INTERNAL_ONLY_TO_CLIENT;
            break;
        case SYNC_ONE_WAY_FROM_SERVER:
        case SYNC_REFRESH_FROM_SERVER:
            m_internalMode |= INTERNAL_ONLY_TO_SERVER;
            break;
        default:
            ;
    };
}

void ProgressData::itemPrepare()
{
    checkInternalMode();
    /**
     * only the first PEV_ITEMPREPARE event takes some time
     * due to data access, other events don't according to
     * profiling data
     */
    if(m_source.empty()) {
        m_source = "source"; ///< use this to check whether itemPrepare occurs
        updateProg(DATA_PREPARE_RATIO);
    }
}

void ProgressData::itemReceive(const std::string &source, int count, int total)
{
    /**
     * source is used to check whether a new source is received
     * If the first source, we compare its total number and default number
     * then re-calc sync units
     */
    if(m_source.empty()) {
        m_source = source;
        if(total != 0) {
            m_syncUnits[PRO_SYNC_UNINIT] += ONEITEM_RECEIVE_RATIO * (total - DEFAULT_ITEMS);
            recalc();
        }
    /** if another new source, add them into sync units */
    } else if(m_source != source){
        m_source = source;
        if(total != 0) {
            m_syncUnits[PRO_SYNC_UNINIT] += ONEITEM_RECEIVE_RATIO * total;
            recalc();
        }
    }
    updateProg(ONEITEM_RECEIVE_RATIO);
}

void ProgressData::updateProg(float ratio)
{
    m_progress += m_propOfUnit * 100 * ratio;
    m_syncUnits[(int)m_step] -= ratio;
}

/** dynamically adapt the proportion of each step by their current units */
void ProgressData::recalc()
{
    float units = getRemainTotalUnits();
    if(std::abs(units) < std::numeric_limits<float>::epsilon()) {
        m_propOfUnit = 0.0;
    } else {
        m_propOfUnit = ( 100.0 - m_progress ) / (100.0 * units);
    }
    if(m_step != PRO_SYNC_TOTAL -1 ) {
        m_syncProp[(int)m_step] = m_progress / 100.0 + m_syncUnits[(int)m_step] * m_propOfUnit;
        for(int i = ((int)m_step) + 1; i < PRO_SYNC_TOTAL - 1; i++) {
            m_syncProp[i] = m_syncProp[i - 1] + m_syncUnits[i] * m_propOfUnit;
        }
    }
}

void ProgressData::checkInternalMode()
{
    if(!m_internalMode) {
        return;
    } else if(m_internalMode & INTERNAL_TWO_WAY) {
        // don't adjust
    } else if(m_internalMode & INTERNAL_ONLY_TO_CLIENT) {
        // only to client, remove units of prepare and send
        m_syncUnits[PRO_SYNC_DATA] -= (ONEITEM_RECEIVE_RATIO * DEFAULT_ITEMS + DATA_PREPARE_RATIO);
        recalc();
    } else if(m_internalMode & INTERNAL_ONLY_TO_SERVER) {
        // only to server, remove units of receive
        m_syncUnits[PRO_SYNC_UNINIT] -= (ONEITEM_RECEIVE_RATIO * DEFAULT_ITEMS + DATA_PREPARE_RATIO);
        recalc();
    }
    m_internalMode = INTERNAL_NONE;
}

float ProgressData::getRemainTotalUnits()
{
    float total = 0.0;
    for(int i = (int)m_step; i < PRO_SYNC_TOTAL; i++) {
        total += m_syncUnits[i];
    }
    return total;
}

float ProgressData::getDefaultUnits(ProgressStep step)
{
    switch(step) {
        case PRO_SYNC_PREPARE:
            return PRO_SYNC_PREPARE_RATIO;
        case PRO_SYNC_INIT:
            return CONN_SETUP_RATIO + MSG_SEND_RECEIVE_TIMES;
        case PRO_SYNC_DATA:
            return ONEITEM_SEND_RATIO * DEFAULT_ITEMS + DATA_PREPARE_RATIO + MSG_SEND_RECEIVE_TIMES;
        case PRO_SYNC_UNINIT:
            return ONEITEM_RECEIVE_RATIO * DEFAULT_ITEMS + DATA_PREPARE_RATIO + MSG_SEND_RECEIVE_TIMES;
        default:
            return 0;
    };
}

SE_END_CXX
