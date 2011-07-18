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

#ifndef INFO_REQ_H
#define INFO_REQ_H

#include <string>

#include "timer.h"
#include "gdbus/gdbus-cxx-bridge.h"

SE_BEGIN_CXX

class Server;
class Session;

/**
 * A wrapper for handling info request and response.
 */
class InfoReq {
public:
    typedef std::map<std::string, std::string> InfoMap;

    // status of current request
    enum Status {
        ST_RUN, // request is running
        ST_OK, // ok, response is gotten
        ST_TIMEOUT, // timeout
        ST_CANCEL // request is cancelled
    };

    /**
     * constructor
     * The default timeout is 120 seconds
     */
    InfoReq(Server &server,
            const std::string &type,
            const InfoMap &parameters,
            const Session *session,
            uint32_t timeout = 120);

    ~InfoReq();

    /**
     * check whether the request is ready. Also give an opportunity
     * to poll the sources and then check the response is ready
     * @return the state of the request
     */
    Status check();

    /**
     * wait the response until timeout, abort or suspend. It may be blocked.
     * The response is returned though the parameter 'response' when the Status is
     * 'ST_OK'. Otherwise, corresponding statuses are returned.
     * @param response the received response if gotten
     * @param interval the interval to check abort, suspend and timeout, in seconds
     * @return the current status
     */
    Status wait(InfoMap &response, uint32_t interval = 3);

    /**
     * get response when it is ready. If false, nothing will be set in response
     */
    bool getResponse(InfoMap &response);

    /** cancel the request. If request is done, cancel won't do anything */
    void cancel();

    /** get current status in string format */
    std::string getStatusStr() const { return statusToString(m_status); }

private:
    static std::string statusToString(Status status);

    enum InfoState {
        IN_REQ,  //request
        IN_WAIT, // waiting
        IN_DONE  // done
    };

    static std::string infoStateToString(InfoState state);

    /** callback for the timemout source */
    static gboolean checkCallback(gpointer data);

    /** check whether the request is timeout */
    bool checkTimeout();

    friend class Server;

    /** set response from dbus clients */
void setResponse(const GDBusCXX::Caller_t &caller, const std::string &state, const InfoMap &response);

    /** send 'done' state if needed */
    void done();

    std::string getId() const { return m_id; }
    std::string getSessionPath() const;
    std::string getInfoStateStr() const { return infoStateToString(m_infoState); }
    std::string getHandler() const { return m_handler; }
    std::string getType() const { return m_type; }
    const InfoMap& getParam() const { return m_param; }

    Server &m_server;

    /** caller's session, might be NULL */
    const Session *m_session;

    /** unique id of this info request */
    std::string m_id;

    /** info req state defined in dbus api */
    InfoState m_infoState;

    /** status to indicate the info request is timeout, ok, abort, etc */
    Status m_status;

    /** the handler of the responsed dbus client */
    GDBusCXX::Caller_t m_handler;

    /** the type of the info request */
    std::string m_type;

    /** parameters from info request callers */
    InfoMap m_param;

    /** response returned from dbus clients */
    InfoMap m_response;

    /** default timeout is 120 seconds */
    uint32_t m_timeout;

    /** a timer */
    Timer m_timer;
};

SE_END_CXX

#endif // INFO_REQ_H
