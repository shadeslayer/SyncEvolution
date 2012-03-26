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

#include "info-req.h"
#include "server.h"

using namespace GDBusCXX;

SE_BEGIN_CXX

InfoReq::InfoReq(Server &server,
                 const string &type,
                 const InfoMap &parameters,
                 const string &sessionPath,
                 uint32_t timeout) :
    m_server(server),
    m_sessionPath(sessionPath),
    m_id(server.getNextInfoReq()),
    m_timeoutSeconds(timeout),
    m_infoState(IN_REQ),
    m_status(ST_RUN),
    m_type(type),
    m_param(parameters)
{
    m_server.emitInfoReq(*this);
    m_timeout.runOnce(m_timeoutSeconds, boost::bind(boost::ref(m_timeoutSignal)));
    m_param.clear();
}

InfoReq::~InfoReq()
{
    m_handler = "";
    done();
}

string InfoReq::statusToString(Status status)
{
    switch(status) {
    case ST_RUN:
        return "running";
    case ST_OK:
        return "ok";
    case ST_CANCEL:
        return "cancelled";
    case ST_TIMEOUT:
        return "timeout";
    default:
        return "";
    };
}

string InfoReq::infoStateToString(InfoState state)
{
    switch(state) {
    case IN_REQ:
        return "request";
    case IN_WAIT:
        return "waiting";
    case IN_DONE:
        return "done";
    default:
        return "";
    }
}

void InfoReq::setResponse(const Caller_t &caller, const string &state, const InfoMap &response)
{
    if(m_status != ST_RUN) {
        return;
    } else if(m_infoState == IN_REQ && state == "working") {
        m_handler = caller;
        m_infoState = IN_WAIT;
        m_server.emitInfoReq(*this);
        //reset the timer, used to check timeout
        m_timeout.runOnce(m_timeoutSeconds, boost::bind(boost::ref(m_timeoutSignal)));
    } else if ((m_infoState == IN_WAIT || m_infoState == IN_REQ) && state == "response") {
        m_response = response;
        m_handler = caller;
        m_status = ST_OK;
        m_responseSignal(m_response);
        done();
    }
}

string InfoReq::getSessionPath() const
{
    return m_sessionPath;
}

void InfoReq::done()
{
    if (m_infoState != IN_DONE) {
        m_infoState = IN_DONE;
        m_server.emitInfoReq(*this);
    }
    m_server.removeInfoReq(getId());
}

SE_END_CXX
