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

#include "gdbus-cxx-bridge.h"
#include "timeout.h"

#include <boost/signals2.hpp>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

class Server;

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
            const std::string &sessionPath,
            uint32_t timeout = 120);

    ~InfoReq();

    std::string getId() const { return m_id; }
    std::string getSessionPath() const;
    std::string getInfoStateStr() const { return infoStateToString(m_infoState); }
    std::string getHandler() const { return m_handler; }
    std::string getType() const { return m_type; }
    const InfoMap& getParam() const { return m_param; }

    /**
     * Connect to this signal to be notified that a final response has
     * been received.
     */
    typedef boost::signals2::signal<void (const InfoMap &)> ResponseSignal_t;
    ResponseSignal_t m_responseSignal;

    /**
     * Connect to this signal to be notified when it is considered timed out.
     * The timeout counting restarts each time any client sends any kind of
     * response.
     */
    typedef boost::signals2::signal<void ()> TimeoutSignal_t;
    TimeoutSignal_t m_timeoutSignal;

    /** get current status in string format */
    std::string getStatusStr() const { return statusToString(m_status); }

    /** set response from dbus clients */
    void setResponse(const GDBusCXX::Caller_t &caller, const std::string &state, const InfoMap &response);

private:
    static std::string statusToString(Status status);

    enum InfoState {
        IN_REQ,  //request
        IN_WAIT, // waiting
        IN_DONE  // done
    };

    static std::string infoStateToString(InfoState state);

    Server &m_server;

    /** caller's session, might be NULL */
    const std::string m_sessionPath;

    /** unique id of this info request */
    std::string m_id;

    /** times out this amount of seconds after last interaction with client */
    uint32_t m_timeoutSeconds;
    Timeout m_timeout;

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

    void done();
};

SE_END_CXX

#endif // INFO_REQ_H
