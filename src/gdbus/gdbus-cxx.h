/*
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

#ifndef INCL_BDBUS_CXX
#define INCL_BDBUS_CXX

#include <string>
#include <stdexcept>
#include <boost/function.hpp>
#include <boost/noncopyable.hpp>

/**
 * An exception class which can be thrown to create
 * specific D-Bus exception on the bus.
 */
class dbus_error : public std::runtime_error
{
public:
    /**
     * @param dbus_name     the D-Bus error name, like "org.example.error.Invalid"
     * @param what          a more detailed description
     */
    dbus_error(const std::string &dbus_name, const std::string &what) :
        std::runtime_error(what),
        m_dbus_name(dbus_name)
        {}
    ~dbus_error() throw() {}

    const std::string &dbusName() const { return m_dbus_name; }

private:
    std::string m_dbus_name;
};

class Watch : private boost::noncopyable
{
 public:
    virtual ~Watch() {};

    /**
     * Changes the callback triggered by this Watch.  If the watch has
     * already fired, the callback is invoked immediately.
     */
    virtual void setCallback(const boost::function<void (void)> &callback) = 0;
};

/**
 * Special parameter type that identifies a caller. A string in practice.
 */
class Caller_t : public std::string
{
 public:
    Caller_t() {}
    template <class T> Caller_t(T val) : std::string(val) {}
    template <class T> Caller_t &operator = (T val) { assign(val); return *this; }
};

/**
 * Call object which needs to be called with the results
 * of an asynchronous method call. So instead of
 * "int foo()" one would implement
 * "void foo(Result1<int> > *r)"
 * and after foo has returned, call r->done(res). Use const
 * references as type for complex results.
 *
 * A Result instance can be copied, but only called once.
 */
class Result
{
 public:
    virtual ~Result() {}

    /** report failure to caller */
    virtual void failed(const dbus_error &error) = 0;

    /**
     * Calls the given callback once when the peer that the result
     * would be delivered to disconnects.  The callback will also be
     * called if the peer is already gone by the time that the watch
     * is requested.
     *
     * Alternatively a method can ask to get called with a life Watch
     * by specifying "const boost::shared_ptr<Watch> &" as parameter
     * and then calling its setCallback().
     */
    virtual Watch *createWatch(const boost::function<void (void)> &callback) = 0;
 };

class Result0 : virtual public Result
{
 public:
    /** tell caller that we are done */
    virtual void done() = 0;
};

template <typename A1>
class Result1 : virtual public Result
{
 public:
    /** return result to caller */
    virtual void done(A1 a1) = 0;
};

template <typename A1, typename A2>
struct Result2 : virtual public Result
{
    virtual void done(A1 a1, A2 a2) = 0;
};

template <typename A1, typename A2, typename A3>
struct Result3 : virtual public Result
{
    virtual void done(A1 a1, A2 a2, A3 a3) = 0;
};

template <typename A1, typename A2, typename A3, typename A4>
struct Result4 : virtual public Result
{
    virtual void done(A1 a1, A2 a2, A3 a3, A4 a4) = 0;
};

template <typename A1, typename A2, typename A3, typename A4, typename A5>
struct Result5 : virtual public Result
{
    virtual void done(A1 a1, A2 a2, A3 a3, A4 a4, A5 a5) = 0;
};

template <typename A1, typename A2, typename A3, typename A4, typename A5,
          typename A6>
struct Result6 : virtual public Result
{
    virtual void done(A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6) = 0;
};

template <typename A1, typename A2, typename A3, typename A4, typename A5,
          typename A6, typename A7>
struct Result7 : virtual public Result
{
    virtual void done(A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6, A7 a7) = 0;
};

template <typename A1, typename A2, typename A3, typename A4, typename A5,
          typename A6, typename A7, typename A8>
struct Result8 : virtual public Result
{
    virtual void done(A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6, A7 a7, A8 a8) = 0;
};

template <typename A1, typename A2, typename A3, typename A4, typename A5,
          typename A6, typename A7, typename A8, typename A9>
struct Result9 : virtual public Result
{
    virtual void done(A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6, A7 a7, A8 a8, A9 a9) = 0;
};

template <typename A1, typename A2, typename A3, typename A4, typename A5,
          typename A6, typename A7, typename A8, typename A9, typename A10>
struct Result10 : virtual public Result
{
    virtual void done(A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6, A7 a7, A8 a8, A9 a9, A10 a10) = 0;
};



#endif // INCL_BDBUS_CXX
