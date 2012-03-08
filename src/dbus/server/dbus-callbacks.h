/*
 * Copyright (C) 2012 Intel Corporation
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

#ifndef INCL_DBUS_CALLBACKS
#define INCL_DBUS_CALLBACKS

#include <boost/shared_ptr.hpp>
#include <boost/function.hpp>
#include <boost/bind.hpp>

#include <syncevo/declarations.h>

#include <stdint.h>

namespace GDBusCXX {
    class Result;
}

SE_BEGIN_CXX

/**
 * Any method inside syncevo-dbus-server which might block for
 * extended periods of time must be asynchronous. It has to alert the
 * caller of success (with a custom callback) or failure (with the
 * ErrorCb_t callback) once it is done with executing the triggered
 * operation.
 *
 * The error callback is invoked inside an exception handler. The
 * callback then needs to rethrow the exection to determine what the
 * real error is and react accordingly. A default error callback which
 * relays the error back to the D-Bus caller is provided below
 * (dbusErrorCallback(), used by createDBusErrorCb()).
 *
 * Asynchronous functions have to take care that exactly those
 * exceptions which indicate a failure of the requested operation
 * invoke the error callback. There might be other exceptions, usually
 * related to fatal problems in the process itself.
 *
 * The caller of an asynchronous method doesn't have to (and in fact,
 * shouldn't!) catch these exceptions and leave handling of them to
 * the top-level catch clauses. In return it may assume that the error
 * callback is invoked only in relation to the requested operation and
 * that the server is able to continue to run.

 * Only one of these two callbacks gets invoked, and only once. Empty
 * callbacks are allowed.
 *
 * It is the responsibility of the caller to ensure that any objects
 * bound to the callback are still around when the callback gets
 * invoked.
 *
 * TODO: utility class which allows runtime checking whether the
 * callback is still possible when it binds to volatile instances (see
 * boost::signals2 automatic connection management).
 *
 * The recommended naming is to use the "Async" suffix in the function
 * name and a "const SimpleResult &result" as last parameter. Example:
 *
 *  void killSessionsAsync(const std::string &peerDeviceID,
 *                         const SimpleResult &result);
 *
 * Some asynchronous methods might also take a D-Bus result pointer
 * plus a success callback, then deal with errors internally by
 * relaying them to the D-Bus client. Example:
 *
 *  void runOperationAsync(RunOperation op,
 *                         const boost::shared_ptr<GDBusCXX::Result0> &dbusResult,
 *                         const SuccessCb_t &helperReady)
 *  ...
 *     useHelperAsync(SimpleResult(helperReady,
 *                                 boost::bind(&Session::failureCb, this)));
 *
 * Session::failureCb() in this example then does some work on its own
 * and finally calls dbusErrorCallback(dbusResult).
 */
typedef boost::function<void ()> ErrorCb_t;

/**
 * Because callbacks always come in pairs, the following
 * utility class is usually used in asynchronous
 * calls. It's parameterized with the prototype of
 * the success call.
 */
template <class P> class Result
{
    boost::function<P> m_onSuccess;
    ErrorCb_t m_onError;

 public:
    Result(const boost::function<P> &onSuccess,
           const ErrorCb_t &onError) :
       m_onSuccess(onSuccess),
       m_onError(onError)
       {}

    void done() const { if (m_onSuccess) m_onSuccess(); }
    template <class A1> void done(const A1 &a1) const { if (m_onSuccess) m_onSuccess(a1); }
    template <class A1, class A2> void done(const A1 &a1, const A2 &a2) const { if (m_onSuccess) m_onSuccess(a1, a2); }
    template <class A1, class A2, class A3> void done(const A1 &a1, const A2 &a2, const A3 &a3) const { if (m_onSuccess) m_onSuccess(a1, a2, a3); }

    void failed() const { if (m_onError) m_onError(); }
};

/**
 * Convenience function for creating a ResultCb for a pair of success
 * and failure callbacks. Determines type automatically based on type
 * of success callback.
 */
template <class P> Result<P> makeCb(const boost::function<P> &onSuccess,
                                    const ErrorCb_t &onFailure)
{
    return Result<P>(onSuccess, onFailure);
}

/**
 * Implements the error callback, can also be called directly inside a
 * catch clause as a general utility function in other error callbacks.
 *
 * @param result    failed() is called here 
 * @return status code (see SyncML.h)
 */
uint32_t dbusErrorCallback(const boost::shared_ptr<GDBusCXX::Result> &result);

/**
 * Creates an error callback which can be used to return a pending
 * exception as a D-Bus error. Template call which is parameterized
 * with the GDBusCXX::Result* class that takes the error.
 */
ErrorCb_t createDBusErrorCb(const boost::shared_ptr<GDBusCXX::Result> &result);

/**
 * a generic "operation successful" callback with no parameters
 */
typedef boost::function<void ()> SuccessCb_t;

/**
 * A generic "operation completed/failed" result pair (no parameters
 * for completion). Same as Result<void ()>, but because it doesn't
 * have overloaded done() template methods the done method can be used
 * in boost::bind().
 */
class SimpleResult {
 public:
    SuccessCb_t m_onSuccess;
    ErrorCb_t m_onError;

 public:
    SimpleResult(const SuccessCb_t &onSuccess,
                 const ErrorCb_t &onError) :
        m_onSuccess(onSuccess),
        m_onError(onError)
        {}

    void done() const { if (m_onSuccess) m_onSuccess(); }
    void failed() const { if (m_onError) m_onError(); }
};

SE_END_CXX

#endif // INCL_DBUS_CALLBACKS
