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
#include "session.h"
#include "syncevo-dbus-server.h"

SE_BEGIN_CXX

InfoReq::InfoReq(DBusServer &server,
                 const string &type,
                 const InfoMap &parameters,
                 const Session *session,
                 uint32_t timeout) :
    m_server(server), m_session(session), m_infoState(IN_REQ),
    m_status(ST_RUN), m_type(type), m_param(parameters),
    m_timeout(timeout), m_timer(m_timeout * 1000)
{
    m_id = m_server.getNextInfoReq();
    m_server.emitInfoReq(*this);
    m_param.clear();
}

InfoReq::~InfoReq()
{
    m_handler = "";
    done();
    m_server.removeInfoReq(*this);
}

InfoReq::Status InfoReq::check()
{
    if(m_status == ST_RUN) {
        // give an opportunity to poll the sources on the main context
        g_main_context_iteration(g_main_loop_get_context(m_server.getLoop()), false);
        checkTimeout();
    }
    return m_status;
}

bool InfoReq::getResponse(InfoMap &response)
{
    if (m_status == ST_OK) {
        response = m_response;
        return true;
    }
    return false;
}

InfoReq::Status InfoReq::wait(InfoMap &response, uint32_t interval)
{
    // give a chance to check whether it has been timeout
    check();
    if(m_status == ST_RUN) {
        guint checkSource = g_timeout_add_seconds(interval,
                                                  (GSourceFunc) checkCallback,
                                                  static_cast<gpointer>(this));
        while(m_status == ST_RUN) {
            g_main_context_iteration(g_main_loop_get_context(m_server.getLoop()), true);
        }

        // if the source is not removed
        if(m_status != ST_TIMEOUT && m_status != ST_CANCEL) {
            g_source_remove(checkSource);
        }
    }
    if (m_status == ST_OK) {
        response = m_response;
    }
    return m_status;
}

void InfoReq::cancel()
{
    if(m_status == ST_RUN) {
        m_handler = "";
        done();
        m_status = ST_CANCEL;
    }
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

gboolean InfoReq::checkCallback(gpointer data)
{
    // TODO: check abort and suspend(MB#8730)

    // if InfoRequest("request") is sent and waiting for InfoResponse("working"),
    // add a timeout mechanism
    InfoReq *req = static_cast<InfoReq*>(data);
    if (req->checkTimeout()) {
        return FALSE;
    }
    return TRUE;
}

bool InfoReq::checkTimeout()
{
    // if waiting for client response, check time out
    if(m_status == ST_RUN) {
        if (m_timer.timeout()) {
            m_status = ST_TIMEOUT;
            return true;
        }
    }
    return false;
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
        m_timer.reset();
    } else if(m_infoState == IN_WAIT && state == "response") {
        m_response = response;
        m_handler = caller;
        done();
        m_status = ST_OK;
    }
}

string InfoReq::getSessionPath() const
{
    return m_session ? m_session->getPath() : "";
}


void InfoReq::done()
{
    if (m_infoState != IN_DONE) {
        m_infoState = IN_DONE;
        m_server.emitInfoReq(*this);
    }
}

SE_END_CXX
