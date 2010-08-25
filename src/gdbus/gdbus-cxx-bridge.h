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

/**
 * This file contains everything that a D-Bus server needs to
 * integrate a normal C++ class into D-Bus. Argument and result
 * marshaling is done in wrapper functions which convert directly
 * to normal C++ types (bool, integers, std::string, std::map<>, ...).
 * See dbus_traits for the full list of supported types.
 *
 * Before explaining the binding, some terminology first:
 * - A function has a return type and multiple parameters.
 * - Input parameters are read-only arguments of the function.
 * - The function can return values to the caller via the
 *   return type and output parameters (retvals).
 *
 * The C++ binding roughly looks like this:
 * - Arguments can be passed as plain types or const references:
     void foo(int arg); void bar(const std::string &str);
 * - A single result can be returned as return value:
 *   int foo();
 * - Multiple results can be copied into instances provided by
 *   the wrapper, passed by reference: void foo(std::string &res);
 * - A return value, arguments and retvals can be combined
 *   arbitrarily. In the D-Bus reply the return code comes before
 *   all return values.
 *
 * Asynchronous methods are possible by declaring one parameter as a
 * Result pointer and later calling the virtual function provided by
 * it. Parameter passing of results is less flexible than that of
 * method parameters: the later allows both std::string as well as
 * const std::string &, for results only the const reference is
 * supported. The Result instance is passed as pointer and then owned
 * by the called method.
 *
 * Reference counting via boost::intrusive_ptr ensures that all
 * D-Bus objects are handled automatically internally.
 */


#ifndef INCL_BDBUS_CXX_BRIDGE
#define INCL_BDBUS_CXX_BRIDGE

#include "gdbus.h"
#include "gdbus-cxx.h"

#include <map>
#include <vector>

#include <boost/bind.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/variant.hpp>
#include <boost/variant/get.hpp>

namespace boost {
    void intrusive_ptr_add_ref(DBusConnection *con) { dbus_connection_ref(con); }
    void intrusive_ptr_release(DBusConnection *con) { dbus_connection_unref(con); }
    void intrusive_ptr_add_ref(DBusMessage *msg) { dbus_message_ref(msg); }
    void intrusive_ptr_release(DBusMessage *msg) { dbus_message_unref(msg); }
    void intrusive_ptr_add_ref(DBusPendingCall *call) {dbus_pending_call_ref (call); }
    void intrusive_ptr_release(DBusPendingCall *call) {dbus_pending_call_unref (call); }
}

class DBusConnectionPtr : public boost::intrusive_ptr<DBusConnection>
{
 public:
    DBusConnectionPtr() {}
    // connections are typically created once, so increment the ref counter by default
    DBusConnectionPtr(DBusConnection *conn, bool add_ref = true) :
      boost::intrusive_ptr<DBusConnection>(conn, add_ref)
    {}

    DBusConnection *reference(void) throw()
    {
        DBusConnection *conn = get();
        dbus_connection_ref(conn);
        return conn;
    }
};

class DBusMessagePtr : public boost::intrusive_ptr<DBusMessage>
{
 public:
    DBusMessagePtr() {}
    // expected to be used for messages created anew,
    // so use the reference already incremented for us
    // and don't increment by default
    DBusMessagePtr(DBusMessage *msg, bool add_ref = false) :
      boost::intrusive_ptr<DBusMessage>(msg, add_ref)
    {}

    DBusMessage *reference(void) throw()
    {
        DBusMessage *msg = get();
        dbus_message_ref(msg);
        return msg;
    }
};

class DBusPendingCallPtr : public boost::intrusive_ptr<DBusPendingCall>
{
public:
    DBusPendingCallPtr(DBusPendingCall *call, bool add_ref = false) :
        boost::intrusive_ptr<DBusPendingCall>(call, add_ref)
    {}

    DBusPendingCall *reference(void) throw()
    {
        DBusPendingCall *call = get();
        dbus_pending_call_ref(call);
        return call;
    }
};

/**
 * wrapper around DBusError which initializes
 * the struct automatically, then can be used to
 * throw an exception
 */
class DBusErrorCXX : public DBusError
{
 public:
    DBusErrorCXX() { dbus_error_init(this); }
    void throwFailure(const std::string &operation, const std::string &explanation = " failed")
    {
        if (dbus_error_is_set(this)) {
            throw std::runtime_error(operation + ": " + message);
        } else {
            throw std::runtime_error(operation + explanation);
        }
    }

    operator bool ()
    {
        return dbus_error_is_set(this);
    }
};

/**
 * Special type for object paths. A string in practice.
 */
class DBusObject_t : public std::string
{
 public:
    DBusObject_t() {}
    template <class T> DBusObject_t(T val) : std::string(val) {}
    template <class T> DBusObject_t &operator = (T val) { assign(val); return *this; }
};

/**
 * specializations of this must defined methods for encoding and
 * decoding type C and declare its signature
 */
template<class C> struct dbus_traits {};

struct dbus_traits_base
{
    /**
     * A C++ method or function can handle a call asynchronously by
     * asking to be passed a "boost::shared_ptr<Result*>" parameter.
     * The dbus_traits for those parameters have "asynchronous" set to
     * true, which skips all processing after calling the method.
     */
    static const bool asynchronous = false;
};

/**
 * Append a varying number of parameters as result to the
 * message. Types can be anything that has a dbus_traits, including
 * types which are normally recognized as input parameters in D-Bus
 * method calls.
 */
template <class A1, class A2, class A3, class A4, class A5,
    class A6, class A7, class A8, class A9, class A10>
void append_retvals(DBusMessagePtr &msg,
                    A1 a1,
                    A2 a2,
                    A3 a3,
                    A4 a4,
                    A5 a5,
                    A6 a6,
                    A7 a7,
                    A8 a8,
                    A9 a9,
                    A10 a10)
{
    DBusMessageIter iter;
    dbus_message_iter_init_append(msg.get(), &iter);
    dbus_traits<A1>::append_retval(iter, a1);
    dbus_traits<A2>::append_retval(iter, a2);
    dbus_traits<A3>::append_retval(iter, a3);
    dbus_traits<A4>::append_retval(iter, a4);
    dbus_traits<A5>::append_retval(iter, a5);
    dbus_traits<A6>::append_retval(iter, a6);
    dbus_traits<A7>::append_retval(iter, a7);
    dbus_traits<A8>::append_retval(iter, a8);
    dbus_traits<A9>::append_retval(iter, a9);
    dbus_traits<A10>::append_retval(iter, a10);
}

template <class A1, class A2, class A3, class A4, class A5,
    class A6, class A7, class A8, class A9>
void append_retvals(DBusMessagePtr &msg,
                    A1 a1,
                    A2 a2,
                    A3 a3,
                    A4 a4,
                    A5 a5,
                    A6 a6,
                    A7 a7,
                    A8 a8,
                    A9 a9)
{
    DBusMessageIter iter;
    dbus_message_iter_init_append(msg.get(), &iter);
    dbus_traits<A1>::append_retval(iter, a1);
    dbus_traits<A2>::append_retval(iter, a2);
    dbus_traits<A3>::append_retval(iter, a3);
    dbus_traits<A4>::append_retval(iter, a4);
    dbus_traits<A5>::append_retval(iter, a5);
    dbus_traits<A6>::append_retval(iter, a6);
    dbus_traits<A7>::append_retval(iter, a7);
    dbus_traits<A8>::append_retval(iter, a8);
    dbus_traits<A9>::append_retval(iter, a9);
}

template <class A1, class A2, class A3, class A4, class A5,
    class A6, class A7, class A8>
void append_retvals(DBusMessagePtr &msg,
                    A1 a1,
                    A2 a2,
                    A3 a3,
                    A4 a4,
                    A5 a5,
                    A6 a6,
                    A7 a7,
                    A8 a8)
{
    DBusMessageIter iter;
    dbus_message_iter_init_append(msg.get(), &iter);
    dbus_traits<A1>::append_retval(iter, a1);
    dbus_traits<A2>::append_retval(iter, a2);
    dbus_traits<A3>::append_retval(iter, a3);
    dbus_traits<A4>::append_retval(iter, a4);
    dbus_traits<A5>::append_retval(iter, a5);
    dbus_traits<A6>::append_retval(iter, a6);
    dbus_traits<A7>::append_retval(iter, a7);
    dbus_traits<A8>::append_retval(iter, a8);
}

template <class A1, class A2, class A3, class A4, class A5,
    class A6, class A7>
void append_retvals(DBusMessagePtr &msg,
                    A1 a1,
                    A2 a2,
                    A3 a3,
                    A4 a4,
                    A5 a5,
                    A6 a6,
                    A7 a7)
{
    DBusMessageIter iter;
    dbus_message_iter_init_append(msg.get(), &iter);
    dbus_traits<A1>::append_retval(iter, a1);
    dbus_traits<A2>::append_retval(iter, a2);
    dbus_traits<A3>::append_retval(iter, a3);
    dbus_traits<A4>::append_retval(iter, a4);
    dbus_traits<A5>::append_retval(iter, a5);
    dbus_traits<A6>::append_retval(iter, a6);
    dbus_traits<A7>::append_retval(iter, a7);
}

template <class A1, class A2, class A3, class A4, class A5,
    class A6>
void append_retvals(DBusMessagePtr &msg,
                    A1 a1,
                    A2 a2,
                    A3 a3,
                    A4 a4,
                    A5 a5,
                    A6 a6)
{
    DBusMessageIter iter;
    dbus_message_iter_init_append(msg.get(), &iter);
    dbus_traits<A1>::append_retval(iter, a1);
    dbus_traits<A2>::append_retval(iter, a2);
    dbus_traits<A3>::append_retval(iter, a3);
    dbus_traits<A4>::append_retval(iter, a4);
    dbus_traits<A5>::append_retval(iter, a5);
    dbus_traits<A6>::append_retval(iter, a6);
}

template <class A1, class A2, class A3, class A4, class A5>
void append_retvals(DBusMessagePtr &msg,
                    A1 a1,
                    A2 a2,
                    A3 a3,
                    A4 a4,
                    A5 a5)
{
    DBusMessageIter iter;
    dbus_message_iter_init_append(msg.get(), &iter);
    dbus_traits<A1>::append_retval(iter, a1);
    dbus_traits<A2>::append_retval(iter, a2);
    dbus_traits<A3>::append_retval(iter, a3);
    dbus_traits<A4>::append_retval(iter, a4);
    dbus_traits<A5>::append_retval(iter, a5);
}

template <class A1, class A2, class A3, class A4>
void append_retvals(DBusMessagePtr &msg,
                    A1 a1,
                    A2 a2,
                    A3 a3,
                    A4 a4)
{
    DBusMessageIter iter;
    dbus_message_iter_init_append(msg.get(), &iter);
    dbus_traits<A1>::append_retval(iter, a1);
    dbus_traits<A2>::append_retval(iter, a2);
    dbus_traits<A3>::append_retval(iter, a3);
    dbus_traits<A4>::append_retval(iter, a4);
}

template <class A1, class A2, class A3>
void append_retvals(DBusMessagePtr &msg,
                    A1 a1,
                    A2 a2,
                    A3 a3)
{
    DBusMessageIter iter;
    dbus_message_iter_init_append(msg.get(), &iter);
    dbus_traits<A1>::append_retval(iter, a1);
    dbus_traits<A2>::append_retval(iter, a2);
    dbus_traits<A3>::append_retval(iter, a3);
}

template <class A1, class A2>
void append_retvals(DBusMessagePtr &msg,
                    A1 a1,
                    A2 a2)
{
    DBusMessageIter iter;
    dbus_message_iter_init_append(msg.get(), &iter);
    dbus_traits<A1>::append_retval(iter, a1);
    dbus_traits<A2>::append_retval(iter, a2);
}

template <class A1>
void append_retvals(DBusMessagePtr &msg,
                    A1 a1)
{
    DBusMessageIter iter;
    dbus_message_iter_init_append(msg.get(), &iter);
    dbus_traits<A1>::append_retval(iter, a1);
}

/**
 * interface expected by EmitSignal
 */
class DBusObject
{
 public:
    virtual ~DBusObject() {}

    virtual DBusConnection *getConnection() const = 0;
    virtual const char *getPath() const = 0;
    virtual const char *getInterface() const = 0;
};

class EmitSignal0
{
    const DBusObject &m_object;
    const std::string m_signal;

 public:
    EmitSignal0(const DBusObject &object,
                const std::string &signal) :
        m_object(object),
        m_signal(signal)
    {}

    void operator () ()
    {
        DBusMessagePtr msg(dbus_message_new_signal(m_object.getPath(),
                                                   m_object.getInterface(),
                                                   m_signal.c_str()));
        if (!msg) {
            throw std::runtime_error("dbus_message_new_signal() failed");
        }

        if (!dbus_connection_send(m_object.getConnection(), msg.get(), NULL)) {
            throw std::runtime_error("dbus_connection_send failed");
        }
    }

    BDBusSignalTable makeSignalEntry(BDBusSignalFlags flags = G_DBUS_SIGNAL_FLAG_NONE) const
    {
        BDBusSignalTable entry;
        entry.name = m_signal.c_str();
        std::string buffer;
        entry.signature = strdup(buffer.c_str());
        entry.flags = flags;
        return entry;
    }
};

template <typename A1>
class EmitSignal1
{
    const DBusObject &m_object;
    const std::string m_signal;

 public:
    EmitSignal1(const DBusObject &object,
                const std::string &signal) :
        m_object(object),
        m_signal(signal)
    {}

    void operator () (A1 a1)
    {
        DBusMessagePtr msg(dbus_message_new_signal(m_object.getPath(),
                                                   m_object.getInterface(),
                                                   m_signal.c_str()));
        if (!msg) {
            throw std::runtime_error("dbus_message_new_signal() failed");
        }
        append_retvals(msg, a1);

        if (!dbus_connection_send(m_object.getConnection(), msg.get(), NULL)) {
            throw std::runtime_error("dbus_connection_send failed");
        }
    }

    BDBusSignalTable makeSignalEntry(BDBusSignalFlags flags = G_DBUS_SIGNAL_FLAG_NONE) const
    {
        BDBusSignalTable entry;
        entry.name = m_signal.c_str();
        std::string buffer;
        buffer += dbus_traits<A1>::getSignature();
        entry.signature = strdup(buffer.c_str());
        entry.flags = flags;
        return entry;
    }
};

template <typename A1, typename A2>
class EmitSignal2
{
    const DBusObject &m_object;
    const std::string m_signal;

 public:
    EmitSignal2(const DBusObject &object,
                const std::string &signal) :
        m_object(object),
        m_signal(signal)
    {}

    void operator () (A1 a1, A2 a2)
    {
        DBusMessagePtr msg(dbus_message_new_signal(m_object.getPath(),
                                                   m_object.getInterface(),
                                                   m_signal.c_str()));
        if (!msg) {
            throw std::runtime_error("dbus_message_new_signal() failed");
        }
        append_retvals(msg, a1, a2);

        if (!dbus_connection_send(m_object.getConnection(), msg.get(), NULL)) {
            throw std::runtime_error("dbus_connection_send failed");
        }
    }

    BDBusSignalTable makeSignalEntry(BDBusSignalFlags flags = G_DBUS_SIGNAL_FLAG_NONE) const
    {
        BDBusSignalTable entry;
        entry.name = m_signal.c_str();
        std::string buffer;
        buffer += dbus_traits<A1>::getSignature();
        buffer += dbus_traits<A2>::getSignature();
        entry.signature = strdup(buffer.c_str());
        entry.flags = flags;
        return entry;
    }
};

template <typename A1, typename A2, typename A3>
class EmitSignal3
{
    const DBusObject &m_object;
    const std::string m_signal;

 public:
    EmitSignal3(const DBusObject &object,
                const std::string &signal) :
        m_object(object),
        m_signal(signal)
    {}

    void operator () (A1 a1, A2 a2, A3 a3)
    {
        DBusMessagePtr msg(dbus_message_new_signal(m_object.getPath(),
                                                   m_object.getInterface(),
                                                   m_signal.c_str()));
        if (!msg) {
            throw std::runtime_error("dbus_message_new_signal() failed");
        }
        append_retvals(msg, a1, a2, a3);
        if (!dbus_connection_send(m_object.getConnection(), msg.get(), NULL)) {
            throw std::runtime_error("dbus_connection_send failed");
        }
    }

    BDBusSignalTable makeSignalEntry(BDBusSignalFlags flags = G_DBUS_SIGNAL_FLAG_NONE) const
    {
        BDBusSignalTable entry;
        entry.name = m_signal.c_str();
        std::string buffer;
        buffer += dbus_traits<A1>::getSignature();
        buffer += dbus_traits<A2>::getSignature();
        buffer += dbus_traits<A3>::getSignature();
        entry.signature = strdup(buffer.c_str());
        entry.flags = flags;
        return entry;
    }
};

template <typename A1, typename A2, typename A3, typename A4>
class EmitSignal4
{
    const DBusObject &m_object;
    const std::string m_signal;

 public:
    EmitSignal4(const DBusObject &object,
                const std::string &signal) :
        m_object(object),
        m_signal(signal)
    {}

    void operator () (A1 a1, A2 a2, A3 a3, A4 a4)
    {
        DBusMessagePtr msg(dbus_message_new_signal(m_object.getPath(),
                                                   m_object.getInterface(),
                                                   m_signal.c_str()));
        if (!msg) {
            throw std::runtime_error("dbus_message_new_signal() failed");
        }
        append_retvals(msg, a1, a2, a3, a4);
        if (!dbus_connection_send(m_object.getConnection(), msg.get(), NULL)) {
            throw std::runtime_error("dbus_connection_send failed");
        }
    }

    BDBusSignalTable makeSignalEntry(BDBusSignalFlags flags = G_DBUS_SIGNAL_FLAG_NONE) const
    {
        BDBusSignalTable entry;
        entry.name = m_signal.c_str();
        std::string buffer;
        buffer += dbus_traits<A1>::getSignature();
        buffer += dbus_traits<A2>::getSignature();
        buffer += dbus_traits<A3>::getSignature();
        buffer += dbus_traits<A4>::getSignature();
        entry.signature = strdup(buffer.c_str());
        entry.flags = flags;
        return entry;
    }
};

template <typename A1, typename A2, typename A3, typename A4, typename A5>
class EmitSignal5
{
    const DBusObject &m_object;
    const std::string m_signal;

 public:
    EmitSignal5(const DBusObject &object,
                const std::string &signal) :
        m_object(object),
        m_signal(signal)
    {}

    void operator () (A1 a1, A2 a2, A3 a3, A4 a4, A5 a5)
    {
        DBusMessagePtr msg(dbus_message_new_signal(m_object.getPath(),
                                                   m_object.getInterface(),
                                                   m_signal.c_str()));
        if (!msg) {
            throw std::runtime_error("dbus_message_new_signal() failed");
        }
        append_retvals(msg, a1, a2, a3, a4, a5);
        if (!dbus_connection_send(m_object.getConnection(), msg.get(), NULL)) {
            throw std::runtime_error("dbus_connection_send failed");
        }
    }

    BDBusSignalTable makeSignalEntry(BDBusSignalFlags flags = G_DBUS_SIGNAL_FLAG_NONE) const
    {
        BDBusSignalTable entry;
        entry.name = m_signal.c_str();
        std::string buffer;
        buffer += dbus_traits<A1>::getSignature();
        buffer += dbus_traits<A2>::getSignature();
        buffer += dbus_traits<A3>::getSignature();
        buffer += dbus_traits<A4>::getSignature();
        buffer += dbus_traits<A5>::getSignature();
        entry.signature = strdup(buffer.c_str());
        entry.flags = flags;
        return entry;
    }
};

template <typename A1, typename A2, typename A3, typename A4, typename A5, typename A6>
class EmitSignal6
{
    const DBusObject &m_object;
    const std::string m_signal;

 public:
    EmitSignal6(const DBusObject &object,
                const std::string &signal) :
        m_object(object),
        m_signal(signal)
    {}

    void operator () (A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6)
    {
        DBusMessagePtr msg(dbus_message_new_signal(m_object.getPath(),
                                                   m_object.getInterface(),
                                                   m_signal.c_str()));
        if (!msg) {
            throw std::runtime_error("dbus_message_new_signal() failed");
        }
        append_retvals(msg, a1, a2, a3, a4, a5, a6);
        if (!dbus_connection_send(m_object.getConnection(), msg.get(), NULL)) {
            throw std::runtime_error("dbus_connection_send failed");
        }
    }

    BDBusSignalTable makeSignalEntry(BDBusSignalFlags flags = G_DBUS_SIGNAL_FLAG_NONE) const
    {
        BDBusSignalTable entry;
        entry.name = m_signal.c_str();
        std::string buffer;
        buffer += dbus_traits<A1>::getSignature();
        buffer += dbus_traits<A2>::getSignature();
        buffer += dbus_traits<A3>::getSignature();
        buffer += dbus_traits<A4>::getSignature();
        buffer += dbus_traits<A5>::getSignature();
        buffer += dbus_traits<A6>::getSignature();
        entry.signature = strdup(buffer.c_str());
        entry.flags = flags;
        return entry;
    }
};

template <class M>
struct MakeMethodEntry
{
    // There is no generic implementation of this method.
    // If you get an error about it missing, then write
    // a specialization for your type M (the method pointer).
    //
    // static BDBusMethodTable make(const char *name,
    //                              BDBusMethodFlags flags)
};

/**
 * Storage for method/signal/property arrays.
 * Always contains at least one empty element
 * at the end or is NULL.
 */
template <class T> class DBusVector {
    size_t m_entries;
    size_t m_size;
    T *m_elements;

    static void destroy(BDBusMethodTable &entry) {
        free(const_cast<char *>(entry.name));
        free(const_cast<char *>(entry.signature));
        free(const_cast<char *>(entry.reply));
        if (entry.destroy) {
            entry.destroy(&entry);
        }
    }

    static void destroy(BDBusSignalTable &entry) {
        free(const_cast<char *>(entry.signature));
        // if (entry.destroy) {
        // entry.destroy(&entry);
        // }
    }

 public:
    DBusVector() : m_entries(0), m_size(0), m_elements(NULL) {}
    ~DBusVector() {
        if (m_elements) {
            for (size_t i = 0; i < m_entries; i++) {
                destroy(m_elements[i]);
            }
            free(m_elements);
        }
    }

    T *get() { return m_elements; }
    void push_back(const T &element) {
        if (m_entries + 1 >= m_size) {
            size_t newSize = m_size ? m_size * 2 : 16;
            m_elements = static_cast<T *>(realloc(m_elements, newSize * sizeof(T)));
            if (!m_elements) {
                throw std::bad_alloc();
            }
            m_size = newSize;
        }
        m_elements[m_entries] = element;
        m_entries++;
        memset(m_elements + m_entries, 0, sizeof(T));
    }
};

/**
 * utility class for registering an interface
 */
class DBusObjectHelper : public DBusObject
{
    DBusConnectionPtr m_conn;
    std::string m_path;
    std::string m_interface;
    boost::function<void (void)> m_callback;
    bool m_activated;
    DBusVector<BDBusMethodTable> m_methods;
    DBusVector<BDBusSignalTable> m_signals;

 public:
    DBusObjectHelper(DBusConnection *conn,
                     const std::string &path,
                     const std::string &interface,
                     const boost::function<void (void)> &callback = boost::function<void (void)>()) :
        m_conn(conn),
        m_path(path),
        m_interface(interface),
        m_callback(callback),
        m_activated(false)
    {
    }

    ~DBusObjectHelper()
    {
        deactivate();
    }

    virtual DBusConnection *getConnection() const { return m_conn.get(); }
    virtual const char *getPath() const { return m_path.c_str(); }
    virtual const char *getInterface() const { return m_interface.c_str(); }

    /**
     * binds a member to the this pointer of its instance
     * and invokes it when the specified method is called
     */
    template <class A1, class C, class M> void add(A1 instance, M C::*method,
                                                   const char *name, BDBusMethodFlags flags = G_DBUS_METHOD_FLAG_NONE) {
        typedef MakeMethodEntry< boost::function<M> > entry_type;
        m_methods.push_back(entry_type::make(name, flags, entry_type::boostptr(method, instance)));
    }


    /**
     * binds a plain function pointer with no additional arguments and
     * invokes it when the specified method is called
     */
    template <class M> void add(M *function,
                                const char *name, BDBusMethodFlags flags = G_DBUS_METHOD_FLAG_NONE) {
        m_methods.push_back(MakeMethodEntry< boost::function<M> >::make(name, flags, function));
    }

    /**
     * add an existing signal entry
     */
    template <class S> void add(const S &s) {
        m_signals.push_back(s.makeSignalEntry());
    }

    void activate(BDBusMethodTable *methods,
                  BDBusSignalTable *signals,
                  BDBusPropertyTable *properties,
                  const boost::function<void (void)> &callback) {
        if (!b_dbus_register_interface_with_callback(getConnection(), getPath(), getInterface(),
                                       methods, signals, properties, this, NULL, interfaceCallback)) {
            throw std::runtime_error(std::string("b_dbus_register_interface() failed for ") + getPath() + " " + getInterface());
        }
        m_callback = callback;
        m_activated = true;
    }

    void activate() {
        if (!b_dbus_register_interface_with_callback(getConnection(), getPath(), getInterface(),
                                       m_methods.get(), m_signals.get(), NULL, this, NULL, interfaceCallback)) {
            throw std::runtime_error(std::string("b_dbus_register_interface() failed for ") + getPath() + " " + getInterface());
        }
        m_activated = true;
    }

    void deactivate()
    {
        if (m_activated) {
            if (!b_dbus_unregister_interface(getConnection(),
                                             getPath(),
                                             getInterface())) {
                throw std::runtime_error(std::string("b_dbus_unregister_interface() failed for ") + getPath() + " " + getInterface());
            }
            m_activated = false;
        }
    }
    static void interfaceCallback(void *userData) {
        DBusObjectHelper* helper = static_cast<DBusObjectHelper*>(userData);
        if(!helper->m_callback.empty()) {
            helper->m_callback();
        }
    }
};


/**
 * to be used for plain parameters like int32_t:
 * treat as arguments which have to be extracted
 * from the D-Bus message and can be skipped when
 * encoding the reply
 */
template<class host, int dbus> struct basic_marshal : public dbus_traits_base
{
    typedef host host_type;
    typedef host arg_type;
    static const int dbus_type = dbus;

    /**
     * copy value from D-Bus iterator into variable
     */
    static void get(DBusConnection *conn, DBusMessage *msg,
                    DBusMessageIter &iter, host &value)
    {
        if (dbus_message_iter_get_arg_type(&iter) != dbus) {
            throw std::runtime_error("invalid argument");
        }
        dbus_message_iter_get_basic(&iter, &value);
        dbus_message_iter_next(&iter);
    }

    /**
     * copy value from return value into D-Bus iterator,
     * empty here because plain types are no return values
     */
    static void append(DBusMessageIter &iter, arg_type value)
    {
        // nothing to do
    }

    /**
     * utility function to be used by derived classes which
     * need to copy a variable of this underlying type
     */
    static void append_retval(DBusMessageIter &iter, arg_type value)
    {
        if (!dbus_message_iter_append_basic(&iter, dbus, &value)) {
            throw std::runtime_error("out of memory");
        }
    }
};

template<> struct dbus_traits<uint8_t> :
    public basic_marshal< uint8_t, DBUS_TYPE_BYTE >
{
    /**
     * plain type, regardless of whether used as
     * input or output parameter
     */
    static std::string getType() { return "y"; }

    /**
     * plain type => input parameter => non-empty signature
     */
    static std::string getSignature() {return getType(); }

    /**
     * plain type => not returned to caller
     */
    static std::string getReply() { return ""; }
    
};

/** if the app wants to use signed char, let it and treat it like a byte */
template<> struct dbus_traits<int8_t> : dbus_traits<uint8_t>
{
    typedef int8_t host_type;
    typedef int8_t arg_type;

    static void get(DBusConnection *conn, DBusMessage *msg,
                    DBusMessageIter &iter, host_type &value)
    {
        dbus_traits<uint8_t>::get(conn, msg, iter, reinterpret_cast<uint8_t &>(value));
    }
};

template<> struct dbus_traits<int16_t> :
    public basic_marshal< int16_t, DBUS_TYPE_INT16 >
{
    static std::string getType() { return "n"; }
    static std::string getSignature() {return getType(); }
    static std::string getReply() { return ""; }
};
template<> struct dbus_traits<uint16_t> :
    public basic_marshal< uint16_t, DBUS_TYPE_UINT16 >
{
    static std::string getType() { return "q"; }
    static std::string getSignature() {return getType(); }
    static std::string getReply() { return ""; }
};
template<> struct dbus_traits<int32_t> :
    public basic_marshal< int32_t, DBUS_TYPE_INT32 >
{
    static std::string getType() { return "i"; }
    static std::string getSignature() {return getType(); }
    static std::string getReply() { return ""; }
};
template<> struct dbus_traits<uint32_t> :
    public basic_marshal< uint32_t, DBUS_TYPE_UINT32 >
{
    static std::string getType() { return "u"; }
    static std::string getSignature() {return getType(); }
    static std::string getReply() { return ""; }
};

template<> struct dbus_traits<bool> : public dbus_traits_base
{
    static std::string getType() { return "b"; }
    static std::string getSignature() {return getType(); }
    static std::string getReply() { return ""; }
    static const int dbus = DBUS_TYPE_BOOLEAN;

    static void get(DBusConnection *conn, DBusMessage *msg,
                    DBusMessageIter &iter, bool &value)
    {
        if (dbus_message_iter_get_arg_type(&iter) != dbus) {
            throw std::runtime_error("invalid argument");
        }
        dbus_bool_t dbus_value;
        dbus_message_iter_get_basic(&iter, &dbus_value);
        dbus_message_iter_next(&iter);
        value = dbus_value;
    }    

    static void append(DBusMessageIter &iter, bool value) {}

    static void append_retval(DBusMessageIter &iter, bool value)
    {
        dbus_bool_t dbus_value = value;
        if (!dbus_message_iter_append_basic(&iter, dbus, &dbus_value)) {
            throw std::runtime_error("out of memory");
        }
    }

    typedef bool host_type;
    typedef bool arg_type;
};

template<> struct dbus_traits<std::string> : public dbus_traits_base
{
    static std::string getType() { return "s"; }
    static std::string getSignature() {return getType(); }
    static std::string getReply() { return ""; }
    static const int dbus = DBUS_TYPE_STRING;

    static void get(DBusConnection *conn, DBusMessage *msg,
                    DBusMessageIter &iter, std::string &value)
    {
        if (dbus_message_iter_get_arg_type(&iter) != dbus) {
            throw std::runtime_error("invalid argument");
        }
        const char *str;
        dbus_message_iter_get_basic(&iter, &str);
        dbus_message_iter_next(&iter);
        value = str;
    }

    static void append(DBusMessageIter &iter, const std::string &value) {}

    static void append_retval(DBusMessageIter &iter, const std::string &value)
    {
        const char *str = value.c_str();
        if (!dbus_message_iter_append_basic(&iter, dbus, &str)) {
            throw std::runtime_error("out of memory");
        }
    }

    typedef std::string host_type;
    typedef const std::string &arg_type;
};

template <> struct dbus_traits<DBusObject_t> : public dbus_traits_base
{
    static std::string getType() { return "o"; }
    static std::string getSignature() {return getType(); }
    static std::string getReply() { return ""; }
    static const int dbus = DBUS_TYPE_OBJECT_PATH;

    static void get(DBusConnection *conn, DBusMessage *msg,
                    DBusMessageIter &iter, DBusObject_t &value)
    {
        if (dbus_message_iter_get_arg_type(&iter) != dbus) {
            throw std::runtime_error("invalid argument");
        }
        const char *str;
        dbus_message_iter_get_basic(&iter, &str);
        dbus_message_iter_next(&iter);
        value = str;
    }

    static void append(DBusMessageIter &iter, const DBusObject_t &value) {}

    static void append_retval(DBusMessageIter &iter, const DBusObject_t &value)
    {
        const char *str = value.c_str();
        if (!dbus_message_iter_append_basic(&iter, dbus, &str)) {
            throw std::runtime_error("out of memory");
        }
    }

    typedef DBusObject_t host_type;
    typedef const DBusObject_t &arg_type;
};

/**
 * pseudo-parameter: not part of D-Bus signature,
 * but rather extracted from message attributes
 */
template <> struct dbus_traits<Caller_t> : public dbus_traits_base
{
    static std::string getType() { return ""; }
    static std::string getSignature() { return ""; }
    static std::string getReply() { return ""; }

    static void get(DBusConnection *conn, DBusMessage *msg,
                    DBusMessageIter &iter, Caller_t &value)
    {
        const char *peer = dbus_message_get_sender(msg);
        if (!peer) {
            throw std::runtime_error("D-Bus method call without sender?!");
        }
        value = peer;
    }

    static void append(DBusMessageIter &iter, const DBusObject_t &value) {}

    typedef Caller_t host_type;
    typedef const Caller_t &arg_type;
};

/**
 * Pass array of basic type plus its number of entries.
 * Can only be used in cases where the caller owns the
 * memory and can discard it when the call returns, in
 * other words, for method calls, asynchronous replys and
 * signals, but not for return values.
 */
template<class V> struct dbus_traits< std::pair<size_t, const V *> > : public dbus_traits_base
{
    static std::string getContainedType()
    {
        return dbus_traits<V>::getType();
    }
    static std::string getType()
    {
        return std::string("a") +
            dbus_traits<V>::getType();
    }
    static std::string getSignature() {return getType(); }
    static std::string getReply() { return ""; }
    typedef std::pair<size_t, const V *> host_type;
    typedef const host_type &arg_type;

    static void get(DBusConnection *conn, DBusMessage *msg,
                    DBusMessageIter &iter, host_type &array)
    {
        if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) {
            throw std::runtime_error("invalid argument");
        }
        DBusMessageIter sub;
        dbus_message_iter_recurse(&iter, &sub);
        int type = dbus_message_iter_get_arg_type(&sub);
        if (type != dbus_traits<V>::dbus_type) {
            throw std::runtime_error("invalid argument");
        }
        int nelements;
        typename dbus_traits<V>::host_type *data;
        dbus_message_iter_get_fixed_array(&sub, &data, &nelements);
        array.first = nelements;
        array.second = data;
        dbus_message_iter_next(&iter);
    }

    static void append(DBusMessageIter &iter, arg_type array) {}

    static void append_retval(DBusMessageIter &iter, arg_type array)
    {
        DBusMessageIter sub;
        if (!dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, getContainedType().c_str(), &sub) ||
            !dbus_message_iter_append_fixed_array(&sub, dbus_traits<V>::dbus_type, &array.second, array.first) ||
            !dbus_message_iter_close_container(&iter, &sub)) {
            throw std::runtime_error("out of memory");
        }
    }
};

/**
 * a std::map - treat it like a D-Bus dict
 */
template<class K, class V> struct dbus_traits< std::map<K, V> > : public dbus_traits_base
{
    static std::string getContainedType()
    {
        return std::string("{") +
            dbus_traits<K>::getType() +
            dbus_traits<V>::getType() +
            "}";
    }
    static std::string getType()
    {
        return std::string("a") +
            getContainedType();
    }
    static std::string getSignature() {return getType(); }
    static std::string getReply() { return ""; }
    typedef std::map<K, V> host_type;
    typedef const std::map<K, V> &arg_type;

    static void get(DBusConnection *conn, DBusMessage *msg,
                    DBusMessageIter &iter, host_type &dict)
    {
        if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) {
            throw std::runtime_error("invalid argument");
        }
        DBusMessageIter sub;
        dbus_message_iter_recurse(&iter, &sub);
        int type;
        while ((type = dbus_message_iter_get_arg_type(&sub)) != DBUS_TYPE_INVALID) {
            if (type != DBUS_TYPE_DICT_ENTRY) {
                throw std::runtime_error("invalid argument");
            }
            DBusMessageIter entry;
            dbus_message_iter_recurse(&sub, &entry);
            K key;
            V value;
            dbus_traits<K>::get(conn, msg, entry, key);
            dbus_traits<V>::get(conn, msg, entry, value);
            dict.insert(std::make_pair(key, value));
            dbus_message_iter_next(&sub);
        }
        dbus_message_iter_next(&iter);
    }

    static void append(DBusMessageIter &iter, arg_type dict) {}

    static void append_retval(DBusMessageIter &iter, arg_type dict)
    {
        DBusMessageIter sub;
        if (!dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, getContainedType().c_str(), &sub)) {
            throw std::runtime_error("out of memory");
        }

        for(typename host_type::const_iterator it = dict.begin();
            it != dict.end();
            ++it) {
            DBusMessageIter entry;
            if (!dbus_message_iter_open_container(&sub, DBUS_TYPE_DICT_ENTRY, NULL, &entry)) {
                throw std::runtime_error("out of memory");
            }
            dbus_traits<K>::append_retval(entry, it->first);
            dbus_traits<V>::append_retval(entry, it->second);
            if (!dbus_message_iter_close_container(&sub, &entry)) {
                throw std::runtime_error("out of memory");
            }
        }
        if (!dbus_message_iter_close_container(&iter, &sub)) {
            throw std::runtime_error("out of memory");
        }
    }
};

/**
 * a std::vector - maps to D-Bus array, but with inefficient marshaling
 * because we cannot get a base pointer for the whole array
 */
template<class V> struct dbus_traits< std::vector<V> > : public dbus_traits_base
{
    static std::string getContainedType()
    {
        return dbus_traits<V>::getType();
    }
    static std::string getType()
    {
        return std::string("a") +
            getContainedType();
    }
    static std::string getSignature() {return getType(); }
    static std::string getReply() { return ""; }
    typedef std::vector<V> host_type;
    typedef const std::vector<V> &arg_type;

    static void get(DBusConnection *conn, DBusMessage *msg,
                    DBusMessageIter &iter, host_type &array)
    {
        if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) {
            throw std::runtime_error("invalid argument");
        }
        DBusMessageIter sub;
        dbus_message_iter_recurse(&iter, &sub);
        while (dbus_message_iter_get_arg_type(&sub) != DBUS_TYPE_INVALID) {
            V value;
            dbus_traits<V>::get(conn, msg, sub, value);
            array.push_back(value);
        }
        dbus_message_iter_next(&iter);
    }

    static void append(DBusMessageIter &iter, arg_type array) {}

    static void append_retval(DBusMessageIter &iter, arg_type array)
    {
        DBusMessageIter sub;
        if (!dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, getContainedType().c_str(), &sub)) {
            throw std::runtime_error("out of memory");
        }

        for(typename host_type::const_iterator it = array.begin();
            it != array.end();
            ++it) {
            dbus_traits<V>::append_retval(sub, *it);
        }
        if (!dbus_message_iter_close_container(&iter, &sub)) {
            throw std::runtime_error("out of memory");
        }
    }
};

/**
 * A boost::variant <V> maps to a dbus variant, only care about values of
 * type V but will not throw error if type is not matched, this is useful if
 * application is interested on only a sub set of possible value types
 * in variant.
 */
template <class V> struct dbus_traits <boost::variant <V> > : public dbus_traits_base
{
    static std::string getType() { return "v"; }
    static std::string getSignature() { return getType(); }
    static std::string getReply() { return ""; }
    static const int dbus = DBUS_TYPE_VARIANT;

    static void get(DBusConnection *conn, DBusMessage *msg,
                    DBusMessageIter &iter, boost::variant <V> &value)
    {
        if (dbus_message_iter_get_arg_type(&iter) != dbus) {
            throw std::runtime_error("invalid argument");
            return;
        }
        DBusMessageIter sub;
        dbus_message_iter_recurse(&iter, &sub);
        if (dbus_message_iter_get_signature(&sub) != dbus_traits<V>::getSignature()){
            //ignore unrecognized sub type in variant
            return;
        }
        V val;
        dbus_traits<V>::get (conn, msg, sub, val);
        value = val;
    }

    static void append(DBusMessageIter &iter, const boost::variant <V>  &value) {
    }

    //append_retval not implemented
    //static void append_retval(DBusMessageIter &iter, arg_type array)
    //{
    //}

    typedef boost::variant<V> host_type;
    typedef const boost::variant<V> &arg_type;
};

/**
 * A boost::variant <V1, V2> maps to a dbus variant, only care about values of
 * type V1, V2 but will not throw error if type is not matched, this is useful if
 * application is interested on only a sub set of possible value types
 * in variant.
 */
template <class V1, class V2> struct dbus_traits <boost::variant <V1, V2> > : public dbus_traits_base
{
    static std::string getType() { return "v"; }
    static std::string getSignature() { return getType(); }
    static std::string getReply() { return ""; }
    static const int dbus = DBUS_TYPE_VARIANT;

    static void get(DBusConnection *conn, DBusMessage *msg,
                    DBusMessageIter &iter, boost::variant <V1, V2> &value)
    {
        if (dbus_message_iter_get_arg_type(&iter) != dbus) {
            throw std::runtime_error("invalid argument");
            return;
        }
        DBusMessageIter sub;
        dbus_message_iter_recurse(&iter, &sub);
        if (dbus_message_iter_get_signature(&sub) != dbus_traits<V1>::getSignature()
                && dbus_message_iter_get_signature(&sub) != dbus_traits<V2>::getSignature()){
            //ignore unrecognized sub type in variant
            return;
        } else if (dbus_message_iter_get_signature(&sub) == dbus_traits<V1>::getSignature()) {
            V1 val;
            dbus_traits<V1>::get (conn, msg, sub, val);
            value = val;
        } else {
            V2 val;
            dbus_traits<V2>::get (conn, msg, sub, val);
            value = val;
        }
    }

    static void append(DBusMessageIter &iter, const boost::variant <V1, V2>  &value) {
    }

    typedef boost::variant<V1, V2> host_type;
    typedef const boost::variant<V1, V2> &arg_type;
};

/**
 * a single member m of type V in a struct K
 */
template<class K, class V, V K::*m> struct dbus_member_single
{
    static std::string getType()
    {
        return dbus_traits<V>::getType();
    }
    typedef V host_type;

    static void get(DBusConnection *conn, DBusMessage *msg,
                    DBusMessageIter &iter, K &val)
    {
        dbus_traits<V>::get(conn, msg, iter, val.*m);
    }

    static void append_retval(DBusMessageIter &iter, const K &val)
    {
        dbus_traits<V>::append_retval(iter, val.*m);
    }
};

/**
 * a member m of type V in a struct K, followed by another dbus_member
 * or dbus_member_single to end the chain
 */
template<class K, class V, V K::*m, class M> struct dbus_member
{
    static std::string getType()
    {
        return dbus_traits<V>::getType() + M::getType();
    }
    typedef V host_type;

    static void get(DBusConnection *conn, DBusMessage *msg,
                    DBusMessageIter &iter, K &val)
    {
        dbus_traits<V>::get(conn, msg, iter, val.*m);
        M::get(conn, msg, iter, val);
    }

    static void append_retval(DBusMessageIter &iter, const K &val)
    {
        dbus_traits<V>::append_retval(iter, val.*m);
        M::append_retval(iter, val);
    }
};

/**
 * a helper class which implements dbus_traits for
 * a class, use with:
 * struct foo { int a; std::string b;  };
 * template<> struct dbus_traits< foo > : dbus_struct_traits< foo,
 *                                                            dbus_member<foo, int, &foo::a,
 *                                                            dbus_member_single<foo, std::string, &foo::b> > > {};
 */
template<class K, class M> struct dbus_struct_traits : public dbus_traits_base
{
    static std::string getContainedType()
    {
        return M::getType();
    }
    static std::string getType()
    {
        return std::string("(") +
            getContainedType() +
            ")";
    }
    static std::string getSignature() {return getType(); }
    static std::string getReply() { return ""; }
    typedef K host_type;
    typedef const K &arg_type;

    static void get(DBusConnection *conn, DBusMessage *msg,
                    DBusMessageIter &iter, host_type &val)
    {
        
        if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRUCT) {
            throw std::runtime_error("invalid argument");
        }
        DBusMessageIter sub;
        dbus_message_iter_recurse(&iter, &sub);
        M::get(conn, msg, sub, val);
        dbus_message_iter_next(&iter);
    }

    static void append(DBusMessageIter &iter, arg_type val) {}

    static void append_retval(DBusMessageIter &iter, arg_type val)
    {
        DBusMessageIter sub;
        if (!dbus_message_iter_open_container(&iter, DBUS_TYPE_STRUCT, NULL, &sub)) {
            throw std::runtime_error("out of memory");
        }
        M::append_retval(sub, val);
        if (!dbus_message_iter_close_container(&iter, &sub)) {
            throw std::runtime_error("out of memory");
        }
    }
};

/**
 * special case const reference parameter:
 * treat like pass-by-value input argument
 *
 * Example: const std::string &arg
 */
template<class C> struct dbus_traits<const C &> : public dbus_traits<C> {};

/**
 * special case writeable reference parameter:
 * must be a return value, so provide our own
 * get() and pack() where get() doesn't do
 * anything and pack() really encodes the value
 *
 * Example: std::string &retval
 */
template<class C> struct dbus_traits<C &> : public dbus_traits<C>
{
    /**
     * skip when extracting input arguments
     */
    static void get(DBusConnection *conn, DBusMessage *msg,
                    DBusMessageIter &iter, C &value) {}
    static std::string getSignature() { return ""; }

    /**
     * use utility function provided by underlying trait
     */
    static void append(DBusMessageIter &iter, const C &value)
    {
        dbus_traits<C>::append_retval(iter, value);
    }
    static std::string getReply() { return dbus_traits<C>::getType(); }
};

/**
 * dbus-cxx base exception thrown in dbus server 
 * org.syncevolution.gdbus-cxx.Exception 
 * This base class only contains interfaces, no data members
 */
class DBusCXXException 
{
 public:
    /**
     * get exception name, used to convert to dbus error name
     * subclasses should override it
     */
    virtual std::string getName() const { return "org.syncevolution.gdbus-cxx.Exception"; }

    /**
     * get error message
     */
    virtual const char* getMessage() const { return "unknown"; }
};

static DBusMessage *handleException(DBusMessage *msg)
{
    try {
#ifdef DBUS_CXX_EXCEPTION_HANDLER
        return DBUS_CXX_EXCEPTION_HANDLER(msg);
#else
        throw;
#endif
    } catch (const dbus_error &ex) {
        return b_dbus_create_error(msg, ex.dbusName().c_str(), "%s", ex.what());
    } catch (const DBusCXXException &ex) {
        return b_dbus_create_error(msg, ex.getName().c_str(), "%s", ex.getMessage());
    } catch (const std::runtime_error &ex) {
        return b_dbus_create_error(msg, "org.syncevolution.gdbus-cxx.Exception", "%s", ex.what());
    } catch (...) {
        return b_dbus_create_error(msg, "org.syncevolution.gdbus-cxx.Exception", "unknown");
    }
}

/**
 * Check presence of a certain D-Bus client.
 */
class DBusWatch : public Watch
{
    DBusConnectionPtr m_conn;
    boost::function<void (void)> m_callback;
    bool m_called;
    guint m_watchID;

    static void disconnect(DBusConnection *connection,
                           void *user_data)
    {
        DBusWatch *watch = static_cast<DBusWatch *>(user_data);
        if (!watch->m_called) {
            watch->m_called = true;
            if (watch->m_callback) {
                watch->m_callback();
            }
        }
    }

 public:
    DBusWatch(const DBusConnectionPtr &conn,
              const boost::function<void (void)> &callback = boost::function<void (void)>()) :
        m_conn(conn),
        m_callback(callback),
        m_called(false),
        m_watchID(0)
    {
    }

    virtual void setCallback(const boost::function<void (void)> &callback)
    {
        m_callback = callback;
        if (m_called && m_callback) {
            m_callback();
        }
    }

    void activate(const char *peer)
    {
        if (!peer) {
            throw std::runtime_error("DBusWatch::activate(): no peer");
        }

        // Install watch first ...
        m_watchID = b_dbus_add_disconnect_watch(m_conn.get(),
                                                peer,
                                                disconnect,
                                                this,
                                                NULL);
        if (!m_watchID) {
            throw std::runtime_error("b_dbus_add_disconnect_watch() failed");
        }

        // ... then check that the peer really exists,
        // otherwise we'll never notice the disconnect.
        // If it disconnects while we are doing this,
        // then disconnect() will be called twice,
        // but it handles that.
        DBusErrorCXX error;
        if (!dbus_bus_name_has_owner(m_conn.get(),
                                     peer,
                                     &error)) {
            if (error) {
                error.throwFailure("dbus_bus_name_has_owner()");
            }
            disconnect(m_conn.get(), this);
        }
    }

    ~DBusWatch()
    {
        if (m_watchID) {
            if (!b_dbus_remove_watch(m_conn.get(), m_watchID)) {
                // this may happen because the watch is
                // removed automatically when it was triggered
            }
            m_watchID = 0;
        }
    }
};

/**
 * pseudo-parameter: not part of D-Bus signature,
 * but rather extracted from message attributes
 */
template <> struct dbus_traits< boost::shared_ptr<Watch> >  : public dbus_traits_base
{
    static std::string getType() { return ""; }
    static std::string getSignature() { return ""; }
    static std::string getReply() { return ""; }

    static void get(DBusConnection *conn, DBusMessage *msg,
                    DBusMessageIter &iter, boost::shared_ptr<Watch> &value)
    {
        boost::shared_ptr<DBusWatch> watch(new DBusWatch(conn));
        watch->activate(dbus_message_get_sender(msg));
        value = watch;
    }

    static void append(DBusMessageIter &iter, const boost::shared_ptr<Watch> &value) {}

    typedef boost::shared_ptr<Watch> host_type;
    typedef const boost::shared_ptr<Watch> &arg_type;
};

/**
 * base class for D-Bus results,
 * keeps references to required objects and provides the
 * failed() method
 */
class DBusResult : virtual public Result
{
 protected:
    DBusConnectionPtr m_conn;     /**< connection via which the message was received */
    DBusMessagePtr m_msg;         /**< the method invocation message */

 public:
    DBusResult(DBusConnection *conn,
               DBusMessage *msg) :
        m_conn(conn, true),
        m_msg(msg, true)
    {}

    virtual void failed(const dbus_error &error)
    {
        if (!b_dbus_send_error(m_conn.get(), m_msg.get(),
                               error.dbusName().c_str(),
                               "%s", error.what())) {
            throw std::runtime_error("b_dbus_send_error() failed");
        }
    }

    virtual Watch *createWatch(const boost::function<void (void)> &callback)
    {
        std::auto_ptr<DBusWatch> watch(new DBusWatch(m_conn, callback));
        watch->activate(dbus_message_get_sender(m_msg.get()));
        return watch.release();
    }
};

class DBusResult0 :
    public Result0,
    public DBusResult
{
 public:
    DBusResult0(DBusConnection *conn,
                DBusMessage *msg) :
        DBusResult(conn, msg)
    {}
  
    virtual void done()
    {
        DBusMessagePtr reply(b_dbus_create_reply(m_msg.get(), DBUS_TYPE_INVALID));
        if (!reply) {
            throw std::runtime_error("no DBusMessage");
        }
        if (!dbus_connection_send(m_conn.get(), reply.get(), NULL)) {
            throw std::runtime_error("dbus_connection_send failed");
        }
    }

    static std::string getSignature() { return ""; }
    static void append(DBusMessageIter &iter) {}
};

template <typename A1>
class DBusResult1 :
    public Result1<A1>,
    public DBusResult
{
 public:
    DBusResult1(DBusConnection *conn,
                DBusMessage *msg) :
        DBusResult(conn, msg)
    {}
  
    virtual void done(A1 a1)
    {
        DBusMessagePtr reply(b_dbus_create_reply(m_msg.get(), DBUS_TYPE_INVALID));
        if (!reply) {
            throw std::runtime_error("no DBusMessage");
        }
        append_retvals(reply, a1);
        if (!dbus_connection_send(m_conn.get(), reply.get(), NULL)) {
            throw std::runtime_error("dbus_connection_send failed");
        }
    }

    static std::string getSignature() { return dbus_traits<A1>::getSignature(); }

    static const bool asynchronous =
        dbus_traits<A1>::asynchronous;
};

template <typename A1, typename A2>
class DBusResult2 :
    public Result2<A1, A2>,
    public DBusResult
{
 public:
    DBusResult2(DBusConnection *conn,
                DBusMessage *msg) :
        DBusResult(conn, msg)
    {}
  
    virtual void done(A1 a1, A2 a2)
    {
        DBusMessagePtr reply(b_dbus_create_reply(m_msg.get(), DBUS_TYPE_INVALID));
        if (!reply) {
            throw std::runtime_error("no DBusMessage");
        }
        append_retvals(reply, a1, a2);
        if (!dbus_connection_send(m_conn.get(), reply.get(), NULL)) {
            throw std::runtime_error("dbus_connection_send failed");
        }
    }

    static std::string getSignature() {
        return dbus_traits<A1>::getSignature() +
            DBusResult1<A2>::getSignature();
    }

    static const bool asynchronous =
        dbus_traits<A1>::asynchronous ||
        DBusResult1<A2>::asynchronous;
};

template <typename A1, typename A2, typename A3>
class DBusResult3 :
    public Result3<A1, A2, A3>,
    public DBusResult
{
 public:
    DBusResult3(DBusConnection *conn,
                DBusMessage *msg) :
        DBusResult(conn, msg)
    {}
  
    virtual void done(A1 a1, A2 a2, A3 a3)
    {
        DBusMessagePtr reply(b_dbus_create_reply(m_msg.get(), DBUS_TYPE_INVALID));
        if (!reply) {
            throw std::runtime_error("no DBusMessage");
        }
        append_retvals(reply, a1, a2, a3);
        if (!dbus_connection_send(m_conn.get(), reply.get(), NULL)) {
            throw std::runtime_error("dbus_connection_send failed");
        }
    }

    static std::string getSignature() {
        return dbus_traits<A1>::getSignature() +
            DBusResult2<A2, A3>::getSignature();
    }

    static const bool asynchronous =
        dbus_traits<A1>::asynchronous ||
        DBusResult2<A2, A3>::asynchronous;
};

template <typename A1, typename A2, typename A3, typename A4>
class DBusResult4 :
    public Result4<A1, A2, A3, A4>,
    public DBusResult
{
 public:
    DBusResult4(DBusConnection *conn,
                DBusMessage *msg) :
        DBusResult(conn, msg)
    {}
  
    virtual void done(A1 a1, A2 a2, A3 a3, A4 a4)
    {
        DBusMessagePtr reply(b_dbus_create_reply(m_msg.get(), DBUS_TYPE_INVALID));
        if (!reply) {
            throw std::runtime_error("no DBusMessage");
        }
        append_retvals(reply, a1, a2, a3, a4);
        if (!dbus_connection_send(m_conn.get(), reply.get(), NULL)) {
            throw std::runtime_error("dbus_connection_send failed");
        }
    }

    static std::string getSignature() {
        return dbus_traits<A1>::getSignature() +
            DBusResult3<A2, A3, A4>::getSignature();
    }

    static const bool asynchronous =
        dbus_traits<A1>::asynchronous ||
        DBusResult3<A2, A3, A4>::asynchronous;
};

template <typename A1, typename A2, typename A3, typename A4, typename A5>
class DBusResult5 :
    public Result5<A1, A2, A3, A4, A5>,
    public DBusResult
{
 public:
    DBusResult5(DBusConnection *conn,
                DBusMessage *msg) :
        DBusResult(conn, msg)
    {}
  
    virtual void done(A1 a1, A2 a2, A3 a3, A4 a4, A5 a5)
    {
        DBusMessagePtr reply(b_dbus_create_reply(m_msg.get(), DBUS_TYPE_INVALID));
        if (!reply) {
            throw std::runtime_error("no DBusMessage");
        }
        append_retvals(reply, a1, a2, a3, a4, a5);
        if (!dbus_connection_send(m_conn.get(), reply.get(), NULL)) {
            throw std::runtime_error("dbus_connection_send failed");
        }
    }

    static std::string getSignature() {
        return dbus_traits<A1>::getSignature() +
            DBusResult4<A2, A3, A4, A5>::getSignature();
    }

    static const bool asynchronous =
        dbus_traits<A1>::asynchronous ||
        DBusResult4<A2, A3, A4, A5>::asynchronous;
};

template <typename A1, typename A2, typename A3, typename A4, typename A5,
          typename A6>
class DBusResult6 :
    public Result6<A1, A2, A3, A4, A5, A6>,
    public DBusResult
{
 public:
    DBusResult6(DBusConnection *conn,
                DBusMessage *msg) :
        DBusResult(conn, msg)
    {}
  
    virtual void done(A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6)
    {
        DBusMessagePtr reply(b_dbus_create_reply(m_msg.get(), DBUS_TYPE_INVALID));
        if (!reply) {
            throw std::runtime_error("no DBusMessage");
        }
        append_retvals(reply, a1, a2, a3, a4, a5, a6);
        if (!dbus_connection_send(m_conn.get(), reply.get(), NULL)) {
            throw std::runtime_error("dbus_connection_send failed");
        }
    }

    static std::string getSignature() {
        return dbus_traits<A1>::getSignature() +
            DBusResult5<A2, A3, A4, A5, A6>::getSignature();
    }

    static const bool asynchronous =
        dbus_traits<A1>::asynchronous ||
        DBusResult5<A2, A3, A4, A5, A6>::asynchronous;
};

template <typename A1, typename A2, typename A3, typename A4, typename A5,
          typename A6, typename A7>
class DBusResult7 :
    public Result7<A1, A2, A3, A4, A5, A6, A7>,
    public DBusResult
{
 public:
    DBusResult7(DBusConnection *conn,
                DBusMessage *msg) :
        DBusResult(conn, msg)
    {}
  
    virtual void done(A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6, A7 a7)
    {
        DBusMessagePtr reply(b_dbus_create_reply(m_msg.get(), DBUS_TYPE_INVALID));
        if (!reply) {
            throw std::runtime_error("no DBusMessage");
        }
        append_retvals(reply, a1, a2, a3, a4, a5, a6, a7);
        if (!dbus_connection_send(m_conn.get(), reply.get(), NULL)) {
            throw std::runtime_error("dbus_connection_send failed");
        }
    }

    static std::string getSignature() {
        return dbus_traits<A1>::getSignature() +
            DBusResult6<A2, A3, A4, A5, A6, A7>::getSignature();
    }

    static const bool asynchronous =
        dbus_traits<A1>::asynchronous ||
        DBusResult6<A2, A3, A4, A5, A6, A7>::asynchronous;
};

template <typename A1, typename A2, typename A3, typename A4, typename A5,
          typename A6, typename A7, typename A8>
class DBusResult8 :
    public Result8<A1, A2, A3, A4, A5, A6, A7, A8>,
    public DBusResult
{
 public:
    DBusResult8(DBusConnection *conn,
                DBusMessage *msg) :
        DBusResult(conn, msg)
    {}
  
    virtual void done(A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6, A7 a7, A8 a8)
    {
        DBusMessagePtr reply(b_dbus_create_reply(m_msg.get(), DBUS_TYPE_INVALID));
        if (!reply) {
            throw std::runtime_error("no DBusMessage");
        }
        append_retvals(reply, a1, a2, a3, a4, a5, a6, a7, a8);
        if (!dbus_connection_send(m_conn.get(), reply.get(), NULL)) {
            throw std::runtime_error("dbus_connection_send failed");
        }
    }

    static std::string getSignature() {
        return dbus_traits<A1>::getSignature() +
            DBusResult7<A2, A3, A4, A5, A6, A7, A8>::getSignature();
    }

    static const bool asynchronous =
        dbus_traits<A1>::asynchronous ||
        DBusResult7<A2, A3, A4, A5, A6, A7, A8>::asynchronous;
};

template <typename A1, typename A2, typename A3, typename A4, typename A5,
          typename A6, typename A7, typename A8, typename A9>
class DBusResult9 :
    public Result9<A1, A2, A3, A4, A5, A6, A7, A8, A9>,
    public DBusResult
{
 public:
    DBusResult9(DBusConnection *conn,
                DBusMessage *msg) :
        DBusResult(conn, msg)
    {}
  
    virtual void done(A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6, A7 a7, A8 a8, A9 a9)
    {
        DBusMessagePtr reply(b_dbus_create_reply(m_msg.get(), DBUS_TYPE_INVALID));
        if (!reply) {
            throw std::runtime_error("no DBusMessage");
        }
        append_retvals(reply, a1, a2, a3, a4, a5, a6, a7, a8, a9);
        if (!dbus_connection_send(m_conn.get(), reply.get(), NULL)) {
            throw std::runtime_error("dbus_connection_send failed");
        }
    }

    static std::string getSignature() {
        return dbus_traits<A1>::getSignature() +
            DBusResult8<A2, A3, A4, A5, A6, A7, A8, A9>::getSignature();
    }

    static const bool asynchronous =
        dbus_traits<A1>::asynchronous ||
        DBusResult8<A2, A3, A4, A5, A6, A7, A8, A9>::asynchronous;
};

template <typename A1, typename A2, typename A3, typename A4, typename A5,
          typename A6, typename A7, typename A8, typename A9, typename A10>
class DBusResult10 :
    public Result10<A1, A2, A3, A4, A5, A6, A7, A8, A9, A10>,
    public DBusResult
{
 public:
    DBusResult10(DBusConnection *conn,
                 DBusMessage *msg) :
        DBusResult(conn, msg)
    {}
  
    virtual void done(A1 a1, A2 a2, A3 a3, A4 a4, A5 a5, A6 a6, A7 a7, A8 a8, A9 a9, A10 a10)
    {
        DBusMessagePtr reply(b_dbus_create_reply(m_msg.get(), DBUS_TYPE_INVALID));
        if (!reply) {
            throw std::runtime_error("no DBusMessage");
        }
        append_retvals(reply, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10);
        if (!dbus_connection_send(m_conn.get(), reply.get(), NULL)) {
            throw std::runtime_error("dbus_connection_send failed");
        }
    }

    static std::string getSignature() {
        return dbus_traits<A1>::getSignature() +
            DBusResult9<A2, A3, A4, A5, A6, A7, A8, A9, A10>::getSignature();
    }

    static const bool asynchronous =
        dbus_traits<A1>::asynchronous ||
        DBusResult9<A2, A3, A4, A5, A6, A7, A8, A9, A10>::asynchronous;
};

/**
 * A parameter which points towards one of our Result* structures.
 * All of the types contained in it count towards the Reply signature.
 * The requested Result type itself is constructed here.
 *
 * @param R      Result0, Result1<type>, ...
 * @param DBusR  the class implementing R
 */
template <class R, class DBusR> struct dbus_traits_result
{
    static std::string getType() { return DBusR::getSignature(); }
    static std::string getSignature() { return ""; }
    static std::string getReply() { return getType(); }

    typedef boost::shared_ptr<R> host_type;
    typedef boost::shared_ptr<R> &arg_type;
    static const bool asynchronous = true;

    static void get(DBusConnection *conn, DBusMessage *msg,
                    DBusMessageIter &iter, host_type &value)
    {
        value.reset(new DBusR(conn, msg));
    }

    static void append(DBusMessageIter &iter, const host_type &value) {}
    static void append_retval(DBusMessageIter &iter, const host_type &value) {}
};

template <>
struct dbus_traits< boost::shared_ptr<Result0> > :
    public dbus_traits_result<Result0, DBusResult0>
{};
template <class A1>
struct dbus_traits< boost::shared_ptr< Result1<A1> > >:
    public dbus_traits_result< Result1<A1>, DBusResult1<A1> >
{};
template <class A1, class A2>
struct dbus_traits< boost::shared_ptr< Result2<A1, A2> > >:
    public dbus_traits_result< Result2<A1, A2>, DBusResult2<A1, A2> >
{};
template <class A1, class A2, class A3>
    struct dbus_traits< boost::shared_ptr< Result3<A1, A2, A3> > >:
    public dbus_traits_result< Result3<A1, A2, A3>, DBusResult3<A1, A2, A3> >
{};
template <class A1, class A2, class A3, class A4>
    struct dbus_traits< boost::shared_ptr< Result4<A1, A2, A3, A4> > >:
    public dbus_traits_result< Result4<A1, A2, A3, A4>, DBusResult4<A1, A2, A3, A4> >
{};
template <class A1, class A2, class A3, class A4, class A5>
    struct dbus_traits< boost::shared_ptr< Result5<A1, A2, A3, A4, A5> > >:
    public dbus_traits_result< Result5<A1, A2, A3, A4, A5>, DBusResult5<A1, A2, A3, A4, A5> >
{};
template <class A1, class A2, class A3, class A4, class A5, class A6>
    struct dbus_traits< boost::shared_ptr< Result6<A1, A2, A3, A4, A5, A6> > >:
    public dbus_traits_result< Result6<A1, A2, A3, A4, A5, A6>, DBusResult6<A1, A2, A3, A4, A5, A6> >
{};
template <class A1, class A2, class A3, class A4, class A5, class A6, class A7>
    struct dbus_traits< boost::shared_ptr< Result7<A1, A2, A3, A4, A5, A6, A7> > >:
    public dbus_traits_result< Result7<A1, A2, A3, A4, A5, A6, A7>, DBusResult7<A1, A2, A3, A4, A5, A6, A7> >
{};
template <class A1, class A2, class A3, class A4, class A5, class A6, class A7, class A8>
    struct dbus_traits< boost::shared_ptr< Result8<A1, A2, A3, A4, A5, A6, A7, A8> > >:
    public dbus_traits_result< Result8<A1, A2, A3, A4, A5, A6, A7, A8>, DBusResult8<A1, A2, A3, A4, A5, A6, A7, A8> >
{};
template <class A1, class A2, class A3, class A4, class A5, class A6, class A7, class A8, class A9>
    struct dbus_traits< boost::shared_ptr< Result9<A1, A2, A3, A4, A5, A6, A7, A8, A9> > >:
    public dbus_traits_result< Result9<A1, A2, A3, A4, A5, A6, A7, A8, A9>, DBusResult9<A1, A2, A3, A4, A5, A6, A7, A8, A9> >
{};
template <class A1, class A2, class A3, class A4, class A5, class A6, class A7, class A8, class A9, class A10>
    struct dbus_traits< boost::shared_ptr< Result10<A1, A2, A3, A4, A5, A6, A7, A8, A9, A10> > >:
    public dbus_traits_result< Result10<A1, A2, A3, A4, A5, A6, A7, A8, A9, A10>, DBusResult10<A1, A2, A3, A4, A5, A6, A7, A8, A9, A10> >
{};


#if 0
/**
 * Call with two parameters and one return code. All other calls are
 * variations of this, so this one is fully documented to explain all
 * tricks used in these templates. The actual code without comments is
 * below.
 */
template <class R, class A1, class A2>
struct MakeMethodEntry< boost::function<R (A1, A2)> >
{
    typedef boost::function<R (A1, A2)> M;

    // Any type a Result parameter? This can be computed at compile time.
    static const bool asynchronous = dbus_traits< DBusResult2<A1, A2> >::asynchronous;

    static DBusMessage *methodFunction(DBusConnection *conn,
                                       DBusMessage *msg, void *data)
    {
        // all exceptions must be caught and translated into
        // a suitable D-Bus reply
        try {
            // Argument types might may be references or pointers.
            // To instantiate a variable we need the underlying
            // datatype, which is provided by the dbus_traits.
            // "typename" is necessary to tell the compiler
            // that host_type really is a type.
            typename dbus_traits<R>::host_type r;
            typename dbus_traits<A1>::host_type a1;
            typename dbus_traits<A2>::host_type a2;

            // Extract all parameters. Because we don't now
            // whether a parameter is an argument or a return
            // value, we call get() for each of them and let
            // the corresponding dbus_traits decide that. Traits
            // for types which are plain types or const references
            // have a non-empty get(), whereas references are treated
            // as return values and have an empty get().
            DBusMessageIter iter;
            dbus_message_iter_init(msg, &iter);
            dbus_traits<A1>::get(conn, msg, iter, a1);
            dbus_traits<A2>::get(conn, msg, iter, a2);

            // The data pointer is a pointer to a boost function,
            // as set up for us by make(). The compiler knows the
            // exact method prototype and thus can handle
            // call-by-value and call-by-reference correctly.
            r = (*static_cast<M *>(data))(a1, a2);

            // No reply necessary? If any of the types is asking for
            // a Result handle, then the reply will be sent later.
            if (asynchronous) {
                return NULL;
            }

            // Now prepare the reply. As with extracting parameters,
            // append() is empty for those parameters where nothing
            // has to be done.
            DBusMessage *reply = dbus_message_new_method_return(msg);
            if (!reply)
                return NULL;
            dbus_message_iter_init_append(reply, &iter);
            // We know that the return value has to be appended,
            // even though the trait would not normally do that
            // because it is a plain type => call utility function
            // directly.
            dbus_traits<R>::append_retval(iter, r);
            dbus_traits<A1>::append(iter, a1);
            dbus_traits<A2>::append(iter, a2);
            return reply;
        } catch (...) {
            // let handleException rethrow the exception
            // to determine its type
            return handleException(msg);
        }
    }

    /**
     * The boost function doesn't have a virtual destructor.
     * Therefore we have to cast down to the right type M
     * before deleting it. The rest of the allocated data
     * is freed by BDBusVector.
     */
    static void destroyFunction(void *user_data)
    {
        BDBusMethodTable *entry = static_cast<BDBusMethodTable *>(user_data);
        delete static_cast<M *>(entry->method_data);
    }

    /**
     * Creates a BDBusMethodTable entry.
     * The strings inside the entry are allocated
     * with strdup(), to be freed by BDBusVector::destroy().
     */
    BDBusMethodTable make(const char *name, BDBusMethodFlags flags, const M &m)
    {
        BDBusMethodTable entry;
        entry.name = strdup(name);
        // same trick as before: only argument types
        // are added to the signature
        std::string buffer;
        buffer += dbus_traits<A1>::getSignature();
        buffer += dbus_traits<A2>::getSignature();
        entry.signature = strdup(buffer.c_str());
        // now the same for reply types
        buffer.clear();
        buffer += dbus_traits<R>::getReply();
        buffer += dbus_traits<A1>::getReply();
        buffer += dbus_traits<A2>::getReply();
        entry.reply = strdup(buffer.c_str());
        // these are the function templates above
        entry.function = methodFunction;
        entry.destroy = destroyFunction;
        // make sure that methodFunction has access to the boost function
        entry.flags = BDBusMethodFlags(flags | G_DBUS_METHOD_FLAG_METHOD_DATA |
                                       (asynchronous ? G_DBUS_METHOD_FLAG_ASYNC : 0));
        entry.method_data = new M(m);
        return entry;
    }
};
#endif // 0

/** ===> 10 parameters */
template <class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8, class A9, class A10>
struct MakeMethodEntry< boost::function<void (A1, A2, A3, A4, A5, A6, A7, A8, A9, A10)> >
{
    typedef void (Mptr)(A1, A2, A3, A4, A5, A6, A7, A8, A9, A10);
    typedef boost::function<Mptr> M;

    template <class I, class C> static M bind(Mptr C::*method, I instance) {
        // this fails because bind() only supports up to 9 parameters, including
        // the initial this pointer
        return boost::bind(method, instance, _1, _2, _3, _4, _5, _6, _7, _8, _9 /* _10 */);
    }

    static const bool asynchronous = dbus_traits< DBusResult10<A1, A2, A3, A4, A5, A6, A7, A8, A9, A10> >::asynchronous;

    static DBusMessage *methodFunction(DBusConnection *conn,
                                       DBusMessage *msg, void *data)
    {
        try {
            typename dbus_traits<A1>::host_type a1;
            typename dbus_traits<A2>::host_type a2;
            typename dbus_traits<A3>::host_type a3;
            typename dbus_traits<A4>::host_type a4;
            typename dbus_traits<A5>::host_type a5;
            typename dbus_traits<A6>::host_type a6;
            typename dbus_traits<A7>::host_type a7;
            typename dbus_traits<A8>::host_type a8;
            typename dbus_traits<A9>::host_type a9;
            typename dbus_traits<A10>::host_type a10;

            DBusMessageIter iter;
            dbus_message_iter_init(msg, &iter);
            dbus_traits<A1>::get(conn, msg, iter, a1);
            dbus_traits<A2>::get(conn, msg, iter, a2);
            dbus_traits<A3>::get(conn, msg, iter, a3);
            dbus_traits<A4>::get(conn, msg, iter, a4);
            dbus_traits<A5>::get(conn, msg, iter, a5);
            dbus_traits<A6>::get(conn, msg, iter, a6);
            dbus_traits<A7>::get(conn, msg, iter, a7);
            dbus_traits<A8>::get(conn, msg, iter, a8);
            dbus_traits<A9>::get(conn, msg, iter, a9);
            dbus_traits<A10>::get(conn, msg, iter, a10);

            (*static_cast<M *>(data))(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10);

            if (asynchronous) {
                return NULL;
            }

            DBusMessage *reply = dbus_message_new_method_return(msg);
            if (!reply)
                return NULL;
            dbus_message_iter_init_append(reply, &iter);
            dbus_traits<A1>::append(iter, a1);
            dbus_traits<A2>::append(iter, a2);
            dbus_traits<A3>::append(iter, a3);
            dbus_traits<A4>::append(iter, a4);
            dbus_traits<A5>::append(iter, a5);
            dbus_traits<A6>::append(iter, a6);
            dbus_traits<A7>::append(iter, a7);
            dbus_traits<A8>::append(iter, a8);
            dbus_traits<A9>::append(iter, a9);
            dbus_traits<A10>::append(iter, a10);
            return reply;
        } catch (...) {
            return handleException(msg);
        }
    }

    static void destroyFunction(void *user_data)
    {
        BDBusMethodTable *entry = static_cast<BDBusMethodTable *>(user_data);
        delete static_cast<M *>(entry->method_data);
    }

    static BDBusMethodTable make(const char *name, BDBusMethodFlags flags, const M &m)
    {
        BDBusMethodTable entry;
        entry.name = strdup(name);
        std::string buffer;
        buffer += dbus_traits<A1>::getSignature();
        buffer += dbus_traits<A2>::getSignature();
        buffer += dbus_traits<A3>::getSignature();
        buffer += dbus_traits<A4>::getSignature();
        buffer += dbus_traits<A5>::getSignature();
        buffer += dbus_traits<A6>::getSignature();
        buffer += dbus_traits<A7>::getSignature();
        buffer += dbus_traits<A8>::getSignature();
        buffer += dbus_traits<A9>::getSignature();
        buffer += dbus_traits<A10>::getSignature();
        entry.signature = strdup(buffer.c_str());
        buffer.clear();
        buffer += dbus_traits<A1>::getReply();
        buffer += dbus_traits<A2>::getReply();
        buffer += dbus_traits<A3>::getReply();
        buffer += dbus_traits<A4>::getReply();
        buffer += dbus_traits<A5>::getReply();
        buffer += dbus_traits<A6>::getReply();
        buffer += dbus_traits<A7>::getReply();
        buffer += dbus_traits<A8>::getReply();
        buffer += dbus_traits<A9>::getReply();
        buffer += dbus_traits<A10>::getReply();
        entry.reply = strdup(buffer.c_str());
        entry.function = methodFunction;
        entry.destroy = destroyFunction;
        entry.flags = BDBusMethodFlags(flags | G_DBUS_METHOD_FLAG_METHOD_DATA |
                                       (asynchronous ? G_DBUS_METHOD_FLAG_ASYNC : 0));
        entry.method_data = new M(m);
        return entry;
    }
};

/** 9 arguments, 1 return value */
template <class R,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8, class A9>
struct MakeMethodEntry< boost::function<R (A1, A2, A3, A4, A5, A6, A7, A8, A9)> >
{
    typedef R (Mptr)(A1, A2, A3, A4, A5, A6, A7, A8, A9);
    typedef boost::function<Mptr> M;

    template <class I, class C> static M boostptr(Mptr C::*method, I instance) {
        // this fails because bind() only supports up to 9 parameters, including
        // the initial this pointer
        return boost::bind(method, instance, _1, _2, _3, _4, _5, _6, _7, _8, _9);
    }

    static const bool asynchronous = DBusResult9<A1, A2, A3, A4, A5, A6, A7, A8, A9>::asynchronous;

    static DBusMessage *methodFunction(DBusConnection *conn,
                                       DBusMessage *msg, void *data)
    {
        try {
            typename dbus_traits<R>::host_type r;
            typename dbus_traits<A1>::host_type a1;
            typename dbus_traits<A2>::host_type a2;
            typename dbus_traits<A3>::host_type a3;
            typename dbus_traits<A4>::host_type a4;
            typename dbus_traits<A5>::host_type a5;
            typename dbus_traits<A6>::host_type a6;
            typename dbus_traits<A7>::host_type a7;
            typename dbus_traits<A8>::host_type a8;
            typename dbus_traits<A9>::host_type a9;

            DBusMessageIter iter;
            dbus_message_iter_init(msg, &iter);
            dbus_traits<A1>::get(conn, msg, iter, a1);
            dbus_traits<A2>::get(conn, msg, iter, a2);
            dbus_traits<A3>::get(conn, msg, iter, a3);
            dbus_traits<A4>::get(conn, msg, iter, a4);
            dbus_traits<A5>::get(conn, msg, iter, a5);
            dbus_traits<A6>::get(conn, msg, iter, a6);
            dbus_traits<A7>::get(conn, msg, iter, a7);
            dbus_traits<A8>::get(conn, msg, iter, a8);
            dbus_traits<A9>::get(conn, msg, iter, a9);

            r = (*static_cast<M *>(data))(a1, a2, a3, a4, a5, a6, a7, a8, a9);

            if (asynchronous) {
                return NULL;
            }

            DBusMessage *reply = dbus_message_new_method_return(msg);
            if (!reply)
                return NULL;
            dbus_message_iter_init_append(reply, &iter);
            dbus_traits<R>::append_retval(iter, r);
            dbus_traits<A1>::append(iter, a1);
            dbus_traits<A2>::append(iter, a2);
            dbus_traits<A3>::append(iter, a3);
            dbus_traits<A4>::append(iter, a4);
            dbus_traits<A5>::append(iter, a5);
            dbus_traits<A6>::append(iter, a6);
            dbus_traits<A7>::append(iter, a7);
            dbus_traits<A8>::append(iter, a8);
            dbus_traits<A9>::append(iter, a9);
            return reply;
        } catch (...) {
            return handleException(msg);
        }
    }

    static void destroyFunction(void *user_data)
    {
        BDBusMethodTable *entry = static_cast<BDBusMethodTable *>(user_data);
        delete static_cast<M *>(entry->method_data);
    }

    static BDBusMethodTable make(const char *name, BDBusMethodFlags flags, const M &m)
    {
        BDBusMethodTable entry;
        entry.name = strdup(name);
        std::string buffer;
        buffer += dbus_traits<A1>::getSignature();
        buffer += dbus_traits<A2>::getSignature();
        buffer += dbus_traits<A3>::getSignature();
        buffer += dbus_traits<A4>::getSignature();
        buffer += dbus_traits<A5>::getSignature();
        buffer += dbus_traits<A6>::getSignature();
        buffer += dbus_traits<A7>::getSignature();
        buffer += dbus_traits<A8>::getSignature();
        buffer += dbus_traits<A9>::getSignature();
        entry.signature = strdup(buffer.c_str());
        buffer.clear();
        buffer += dbus_traits<R>::getReply();
        buffer += dbus_traits<A1>::getReply();
        buffer += dbus_traits<A2>::getReply();
        buffer += dbus_traits<A3>::getReply();
        buffer += dbus_traits<A4>::getReply();
        buffer += dbus_traits<A5>::getReply();
        buffer += dbus_traits<A6>::getReply();
        buffer += dbus_traits<A7>::getReply();
        buffer += dbus_traits<A8>::getReply();
        buffer += dbus_traits<A9>::getReply();
        entry.reply = strdup(buffer.c_str());
        entry.function = methodFunction;
        entry.destroy = destroyFunction;
        entry.flags = BDBusMethodFlags(flags | G_DBUS_METHOD_FLAG_METHOD_DATA |
                                       (asynchronous ? G_DBUS_METHOD_FLAG_ASYNC : 0));
        entry.method_data = new M(m);
        return entry;
    }
};

/** ===> 9 parameters */
template <class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8, class A9>
struct MakeMethodEntry< boost::function<void (A1, A2, A3, A4, A5, A6, A7, A8, A9)> >
{
    typedef void (Mptr)(A1, A2, A3, A4, A5, A6, A7, A8, A9);
    typedef boost::function<Mptr> M;

    template <class I, class C> static M boostptr(Mptr C::*method, I instance) {
        return boost::bind(method, instance, _1, _2, _3, _4, _5, _6, _7, _8, _9);
    }

    static const bool asynchronous = DBusResult9<A1, A2, A3, A4, A5, A6, A7, A8, A9>::asynchronous;

    static DBusMessage *methodFunction(DBusConnection *conn,
                                       DBusMessage *msg, void *data)
    {
        try {
            typename dbus_traits<A1>::host_type a1;
            typename dbus_traits<A2>::host_type a2;
            typename dbus_traits<A3>::host_type a3;
            typename dbus_traits<A4>::host_type a4;
            typename dbus_traits<A5>::host_type a5;
            typename dbus_traits<A6>::host_type a6;
            typename dbus_traits<A7>::host_type a7;
            typename dbus_traits<A8>::host_type a8;
            typename dbus_traits<A9>::host_type a9;

            DBusMessageIter iter;
            dbus_message_iter_init(msg, &iter);
            dbus_traits<A1>::get(conn, msg, iter, a1);
            dbus_traits<A2>::get(conn, msg, iter, a2);
            dbus_traits<A3>::get(conn, msg, iter, a3);
            dbus_traits<A4>::get(conn, msg, iter, a4);
            dbus_traits<A5>::get(conn, msg, iter, a5);
            dbus_traits<A6>::get(conn, msg, iter, a6);
            dbus_traits<A7>::get(conn, msg, iter, a7);
            dbus_traits<A8>::get(conn, msg, iter, a8);
            dbus_traits<A9>::get(conn, msg, iter, a9);

            (*static_cast<M *>(data))(a1, a2, a3, a4, a5, a6, a7, a8, a9);

            if (asynchronous) {
                return NULL;
            }

            DBusMessage *reply = dbus_message_new_method_return(msg);
            if (!reply)
                return NULL;
            dbus_message_iter_init_append(reply, &iter);
            dbus_traits<A1>::append(iter, a1);
            dbus_traits<A2>::append(iter, a2);
            dbus_traits<A3>::append(iter, a3);
            dbus_traits<A4>::append(iter, a4);
            dbus_traits<A5>::append(iter, a5);
            dbus_traits<A6>::append(iter, a6);
            dbus_traits<A7>::append(iter, a7);
            dbus_traits<A8>::append(iter, a8);
            dbus_traits<A9>::append(iter, a9);
            return reply;
        } catch (...) {
            return handleException(msg);
        }
    }

    static void destroyFunction(void *user_data)
    {
        BDBusMethodTable *entry = static_cast<BDBusMethodTable *>(user_data);
        delete static_cast<M *>(entry->method_data);
    }

    static BDBusMethodTable make(const char *name, BDBusMethodFlags flags, const M &m)
    {
        BDBusMethodTable entry;
        entry.name = strdup(name);
        std::string buffer;
        buffer += dbus_traits<A1>::getSignature();
        buffer += dbus_traits<A2>::getSignature();
        buffer += dbus_traits<A3>::getSignature();
        buffer += dbus_traits<A4>::getSignature();
        buffer += dbus_traits<A5>::getSignature();
        buffer += dbus_traits<A6>::getSignature();
        buffer += dbus_traits<A7>::getSignature();
        buffer += dbus_traits<A8>::getSignature();
        buffer += dbus_traits<A9>::getSignature();
        entry.signature = strdup(buffer.c_str());
        buffer.clear();
        buffer += dbus_traits<A1>::getReply();
        buffer += dbus_traits<A2>::getReply();
        buffer += dbus_traits<A3>::getReply();
        buffer += dbus_traits<A4>::getReply();
        buffer += dbus_traits<A5>::getReply();
        buffer += dbus_traits<A6>::getReply();
        buffer += dbus_traits<A7>::getReply();
        buffer += dbus_traits<A8>::getReply();
        buffer += dbus_traits<A9>::getReply();
        entry.reply = strdup(buffer.c_str());
        entry.function = methodFunction;
        entry.destroy = destroyFunction;
        entry.flags = BDBusMethodFlags(flags | G_DBUS_METHOD_FLAG_METHOD_DATA |
                                       (asynchronous ? G_DBUS_METHOD_FLAG_ASYNC : 0));
        entry.method_data = new M(m);
        return entry;
    }
};

/** 8 arguments, 1 return value */
template <class R,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8>
struct MakeMethodEntry< boost::function<R (A1, A2, A3, A4, A5, A6, A7, A8)> >
{
    typedef R (Mptr)(A1, A2, A3, A4, A5, A6, A7, A8);
    typedef boost::function<Mptr> M;

    template <class I, class C> static M boostptr(Mptr C::*method, I instance) {
        return boost::bind(method, instance, _1, _2, _3, _4, _5, _6, _7, _8);
    }

    static const bool asynchronous = DBusResult8<A1, A2, A3, A4, A5, A6, A7, A8>::asynchronous;

    static DBusMessage *methodFunction(DBusConnection *conn,
                                       DBusMessage *msg, void *data)
    {
        try {
            typename dbus_traits<R>::host_type r;
            typename dbus_traits<A1>::host_type a1;
            typename dbus_traits<A2>::host_type a2;
            typename dbus_traits<A3>::host_type a3;
            typename dbus_traits<A4>::host_type a4;
            typename dbus_traits<A5>::host_type a5;
            typename dbus_traits<A6>::host_type a6;
            typename dbus_traits<A7>::host_type a7;
            typename dbus_traits<A8>::host_type a8;

            DBusMessageIter iter;
            dbus_message_iter_init(msg, &iter);
            dbus_traits<A1>::get(conn, msg, iter, a1);
            dbus_traits<A2>::get(conn, msg, iter, a2);
            dbus_traits<A3>::get(conn, msg, iter, a3);
            dbus_traits<A4>::get(conn, msg, iter, a4);
            dbus_traits<A5>::get(conn, msg, iter, a5);
            dbus_traits<A6>::get(conn, msg, iter, a6);
            dbus_traits<A7>::get(conn, msg, iter, a7);
            dbus_traits<A8>::get(conn, msg, iter, a8);

            r = (*static_cast<M *>(data))(a1, a2, a3, a4, a5, a6, a7, a8);

            if (asynchronous) {
                return NULL;
            }

            DBusMessage *reply = dbus_message_new_method_return(msg);
            if (!reply)
                return NULL;
            dbus_message_iter_init_append(reply, &iter);
            dbus_traits<R>::append_retval(iter, r);
            dbus_traits<A1>::append(iter, a1);
            dbus_traits<A2>::append(iter, a2);
            dbus_traits<A3>::append(iter, a3);
            dbus_traits<A4>::append(iter, a4);
            dbus_traits<A5>::append(iter, a5);
            dbus_traits<A6>::append(iter, a6);
            dbus_traits<A7>::append(iter, a7);
            dbus_traits<A8>::append(iter, a8);
            return reply;
        } catch (...) {
            return handleException(msg);
        }
    }

    static void destroyFunction(void *user_data)
    {
        BDBusMethodTable *entry = static_cast<BDBusMethodTable *>(user_data);
        delete static_cast<M *>(entry->method_data);
    }

    static BDBusMethodTable make(const char *name, BDBusMethodFlags flags, const M &m)
    {
        BDBusMethodTable entry;
        entry.name = strdup(name);
        std::string buffer;
        buffer += dbus_traits<A1>::getSignature();
        buffer += dbus_traits<A2>::getSignature();
        buffer += dbus_traits<A3>::getSignature();
        buffer += dbus_traits<A4>::getSignature();
        buffer += dbus_traits<A5>::getSignature();
        buffer += dbus_traits<A6>::getSignature();
        buffer += dbus_traits<A7>::getSignature();
        buffer += dbus_traits<A8>::getSignature();
        entry.signature = strdup(buffer.c_str());
        buffer.clear();
        buffer += dbus_traits<R>::getReply();
        buffer += dbus_traits<A1>::getReply();
        buffer += dbus_traits<A2>::getReply();
        buffer += dbus_traits<A3>::getReply();
        buffer += dbus_traits<A4>::getReply();
        buffer += dbus_traits<A5>::getReply();
        buffer += dbus_traits<A6>::getReply();
        buffer += dbus_traits<A7>::getReply();
        buffer += dbus_traits<A8>::getReply();
        entry.reply = strdup(buffer.c_str());
        entry.function = methodFunction;
        entry.destroy = destroyFunction;
        entry.flags = BDBusMethodFlags(flags | G_DBUS_METHOD_FLAG_METHOD_DATA |
                                       (asynchronous ? G_DBUS_METHOD_FLAG_ASYNC : 0));
        entry.method_data = new M(m);
        return entry;
    }
};

/** ===> 8 parameters */
template <class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8>
struct MakeMethodEntry< boost::function<void (A1, A2, A3, A4, A5, A6, A7, A8)> >
{
    typedef void (Mptr)(A1, A2, A3, A4, A5, A6, A7, A8);
    typedef boost::function<Mptr> M;

    template <class I, class C> static M boostptr(Mptr C::*method, I instance) {
        return boost::bind(method, instance, _1, _2, _3, _4, _5, _6, _7, _8);
    }

    static const bool asynchronous = DBusResult8<A1, A2, A3, A4, A5, A6, A7, A8>::asynchronous;

    static DBusMessage *methodFunction(DBusConnection *conn,
                                       DBusMessage *msg, void *data)
    {
        try {
            typename dbus_traits<A1>::host_type a1;
            typename dbus_traits<A2>::host_type a2;
            typename dbus_traits<A3>::host_type a3;
            typename dbus_traits<A4>::host_type a4;
            typename dbus_traits<A5>::host_type a5;
            typename dbus_traits<A6>::host_type a6;
            typename dbus_traits<A7>::host_type a7;
            typename dbus_traits<A8>::host_type a8;

            DBusMessageIter iter;
            dbus_message_iter_init(msg, &iter);
            dbus_traits<A1>::get(conn, msg, iter, a1);
            dbus_traits<A2>::get(conn, msg, iter, a2);
            dbus_traits<A3>::get(conn, msg, iter, a3);
            dbus_traits<A4>::get(conn, msg, iter, a4);
            dbus_traits<A5>::get(conn, msg, iter, a5);
            dbus_traits<A6>::get(conn, msg, iter, a6);
            dbus_traits<A7>::get(conn, msg, iter, a7);
            dbus_traits<A8>::get(conn, msg, iter, a8);

            (*static_cast<M *>(data))(a1, a2, a3, a4, a5, a6, a7, a8);

            if (asynchronous) {
                return NULL;
            }

            DBusMessage *reply = dbus_message_new_method_return(msg);
            if (!reply)
                return NULL;
            dbus_message_iter_init_append(reply, &iter);
            dbus_traits<A1>::append(iter, a1);
            dbus_traits<A2>::append(iter, a2);
            dbus_traits<A3>::append(iter, a3);
            dbus_traits<A4>::append(iter, a4);
            dbus_traits<A5>::append(iter, a5);
            dbus_traits<A6>::append(iter, a6);
            dbus_traits<A7>::append(iter, a7);
            dbus_traits<A8>::append(iter, a8);
            return reply;
        } catch (...) {
            return handleException(msg);
        }
    }

    static void destroyFunction(void *user_data)
    {
        BDBusMethodTable *entry = static_cast<BDBusMethodTable *>(user_data);
        delete static_cast<M *>(entry->method_data);
    }

    static BDBusMethodTable make(const char *name, BDBusMethodFlags flags, const M &m)
    {
        BDBusMethodTable entry;
        entry.name = strdup(name);
        std::string buffer;
        buffer += dbus_traits<A1>::getSignature();
        buffer += dbus_traits<A2>::getSignature();
        buffer += dbus_traits<A3>::getSignature();
        buffer += dbus_traits<A4>::getSignature();
        buffer += dbus_traits<A5>::getSignature();
        buffer += dbus_traits<A6>::getSignature();
        buffer += dbus_traits<A7>::getSignature();
        buffer += dbus_traits<A8>::getSignature();
        entry.signature = strdup(buffer.c_str());
        buffer.clear();
        buffer += dbus_traits<A1>::getReply();
        buffer += dbus_traits<A2>::getReply();
        buffer += dbus_traits<A3>::getReply();
        buffer += dbus_traits<A4>::getReply();
        buffer += dbus_traits<A5>::getReply();
        buffer += dbus_traits<A6>::getReply();
        buffer += dbus_traits<A7>::getReply();
        buffer += dbus_traits<A8>::getReply();
        entry.reply = strdup(buffer.c_str());
        entry.function = methodFunction;
        entry.destroy = destroyFunction;
        entry.flags = BDBusMethodFlags(flags | G_DBUS_METHOD_FLAG_METHOD_DATA |
                                       (asynchronous ? G_DBUS_METHOD_FLAG_ASYNC : 0));
        entry.method_data = new M(m);
        return entry;
    }
};

/** 7 arguments, 1 return value */
template <class R,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7>
struct MakeMethodEntry< boost::function<R (A1, A2, A3, A4, A5, A6, A7)> >
{
    typedef R (Mptr)(A1, A2, A3, A4, A5, A6, A7);
    typedef boost::function<Mptr> M;

    template <class I, class C> static M boostptr(Mptr C::*method, I instance) {
        return boost::bind(method, instance, _1, _2, _3, _4, _5, _6, _7);
    }

    static const bool asynchronous = DBusResult7<A1, A2, A3, A4, A5, A6, A7>::asynchronous;

    static DBusMessage *methodFunction(DBusConnection *conn,
                                       DBusMessage *msg, void *data)
    {
        try {
            typename dbus_traits<R>::host_type r;
            typename dbus_traits<A1>::host_type a1;
            typename dbus_traits<A2>::host_type a2;
            typename dbus_traits<A3>::host_type a3;
            typename dbus_traits<A4>::host_type a4;
            typename dbus_traits<A5>::host_type a5;
            typename dbus_traits<A6>::host_type a6;
            typename dbus_traits<A7>::host_type a7;

            DBusMessageIter iter;
            dbus_message_iter_init(msg, &iter);
            dbus_traits<A1>::get(conn, msg, iter, a1);
            dbus_traits<A2>::get(conn, msg, iter, a2);
            dbus_traits<A3>::get(conn, msg, iter, a3);
            dbus_traits<A4>::get(conn, msg, iter, a4);
            dbus_traits<A5>::get(conn, msg, iter, a5);
            dbus_traits<A6>::get(conn, msg, iter, a6);
            dbus_traits<A7>::get(conn, msg, iter, a7);

            r = (*static_cast<M *>(data))(a1, a2, a3, a4, a5, a6, a7);

            if (asynchronous) {
                return NULL;
            }

            DBusMessage *reply = dbus_message_new_method_return(msg);
            if (!reply)
                return NULL;
            dbus_message_iter_init_append(reply, &iter);
            dbus_traits<R>::append_retval(iter, r);
            dbus_traits<A1>::append(iter, a1);
            dbus_traits<A2>::append(iter, a2);
            dbus_traits<A3>::append(iter, a3);
            dbus_traits<A4>::append(iter, a4);
            dbus_traits<A5>::append(iter, a5);
            dbus_traits<A6>::append(iter, a6);
            dbus_traits<A7>::append(iter, a7);
            return reply;
        } catch (...) {
            return handleException(msg);
        }
    }

    static void destroyFunction(void *user_data)
    {
        BDBusMethodTable *entry = static_cast<BDBusMethodTable *>(user_data);
        delete static_cast<M *>(entry->method_data);
    }

    static BDBusMethodTable make(const char *name, BDBusMethodFlags flags, const M &m)
    {
        BDBusMethodTable entry;
        entry.name = strdup(name);
        std::string buffer;
        buffer += dbus_traits<A1>::getSignature();
        buffer += dbus_traits<A2>::getSignature();
        buffer += dbus_traits<A3>::getSignature();
        buffer += dbus_traits<A4>::getSignature();
        buffer += dbus_traits<A5>::getSignature();
        buffer += dbus_traits<A6>::getSignature();
        buffer += dbus_traits<A7>::getSignature();
        entry.signature = strdup(buffer.c_str());
        buffer.clear();
        buffer += dbus_traits<R>::getReply();
        buffer += dbus_traits<A1>::getReply();
        buffer += dbus_traits<A2>::getReply();
        buffer += dbus_traits<A3>::getReply();
        buffer += dbus_traits<A4>::getReply();
        buffer += dbus_traits<A5>::getReply();
        buffer += dbus_traits<A6>::getReply();
        buffer += dbus_traits<A7>::getReply();
        entry.reply = strdup(buffer.c_str());
        entry.function = methodFunction;
        entry.destroy = destroyFunction;
        entry.flags = BDBusMethodFlags(flags | G_DBUS_METHOD_FLAG_METHOD_DATA |
                                       (asynchronous ? G_DBUS_METHOD_FLAG_ASYNC : 0));
        entry.method_data = new M(m);
        return entry;
    }
};

/** ===> 7 parameters */
template <class A1, class A2, class A3, class A4, class A5,
          class A6, class A7>
struct MakeMethodEntry< boost::function<void (A1, A2, A3, A4, A5, A6, A7)> >
{
    typedef void (Mptr)(A1, A2, A3, A4, A5, A6, A7);
    typedef boost::function<Mptr> M;

    template <class I, class C> static M boostptr(Mptr C::*method, I instance) {
        return boost::bind(method, instance, _1, _2, _3, _4, _5, _6, _7);
    }

    static const bool asynchronous = DBusResult7<A1, A2, A3, A4, A5, A6, A7>::asynchronous;

    static DBusMessage *methodFunction(DBusConnection *conn,
                                       DBusMessage *msg, void *data)
    {
        try {
            typename dbus_traits<A1>::host_type a1;
            typename dbus_traits<A2>::host_type a2;
            typename dbus_traits<A3>::host_type a3;
            typename dbus_traits<A4>::host_type a4;
            typename dbus_traits<A5>::host_type a5;
            typename dbus_traits<A6>::host_type a6;
            typename dbus_traits<A7>::host_type a7;

            DBusMessageIter iter;
            dbus_message_iter_init(msg, &iter);
            dbus_traits<A1>::get(conn, msg, iter, a1);
            dbus_traits<A2>::get(conn, msg, iter, a2);
            dbus_traits<A3>::get(conn, msg, iter, a3);
            dbus_traits<A4>::get(conn, msg, iter, a4);
            dbus_traits<A5>::get(conn, msg, iter, a5);
            dbus_traits<A6>::get(conn, msg, iter, a6);
            dbus_traits<A7>::get(conn, msg, iter, a7);

            (*static_cast<M *>(data))(a1, a2, a3, a4, a5, a6, a7);

            if (asynchronous) {
                return NULL;
            }

            DBusMessage *reply = dbus_message_new_method_return(msg);
            if (!reply)
                return NULL;
            dbus_message_iter_init_append(reply, &iter);
            dbus_traits<A1>::append(iter, a1);
            dbus_traits<A2>::append(iter, a2);
            dbus_traits<A3>::append(iter, a3);
            dbus_traits<A4>::append(iter, a4);
            dbus_traits<A5>::append(iter, a5);
            dbus_traits<A6>::append(iter, a6);
            dbus_traits<A7>::append(iter, a7);
            return reply;
        } catch (...) {
            return handleException(msg);
        }
    }

    static void destroyFunction(void *user_data)
    {
        BDBusMethodTable *entry = static_cast<BDBusMethodTable *>(user_data);
        delete static_cast<M *>(entry->method_data);
    }

    static BDBusMethodTable make(const char *name, BDBusMethodFlags flags, const M &m)
    {
        BDBusMethodTable entry;
        entry.name = strdup(name);
        std::string buffer;
        buffer += dbus_traits<A1>::getSignature();
        buffer += dbus_traits<A2>::getSignature();
        buffer += dbus_traits<A3>::getSignature();
        buffer += dbus_traits<A4>::getSignature();
        buffer += dbus_traits<A5>::getSignature();
        buffer += dbus_traits<A6>::getSignature();
        buffer += dbus_traits<A7>::getSignature();
        entry.signature = strdup(buffer.c_str());
        buffer.clear();
        buffer += dbus_traits<A1>::getReply();
        buffer += dbus_traits<A2>::getReply();
        buffer += dbus_traits<A3>::getReply();
        buffer += dbus_traits<A4>::getReply();
        buffer += dbus_traits<A5>::getReply();
        buffer += dbus_traits<A6>::getReply();
        buffer += dbus_traits<A7>::getReply();
        entry.reply = strdup(buffer.c_str());
        entry.function = methodFunction;
        entry.destroy = destroyFunction;
        entry.flags = BDBusMethodFlags(flags | G_DBUS_METHOD_FLAG_METHOD_DATA |
                                       (asynchronous ? G_DBUS_METHOD_FLAG_ASYNC : 0));
        entry.method_data = new M(m);
        return entry;
    }
};

/** 6 arguments, 1 return value */
template <class R,
          class A1, class A2, class A3, class A4, class A5,
          class A6>
struct MakeMethodEntry< boost::function<R (A1, A2, A3, A4, A5, A6)> >
{
    typedef R (Mptr)(A1, A2, A3, A4, A5, A6);
    typedef boost::function<Mptr> M;

    template <class I, class C> static M boostptr(Mptr C::*method, I instance) {
        return boost::bind(method, instance, _1, _2, _3, _4, _5, _6);
    }

    static const bool asynchronous = DBusResult6<A1, A2, A3, A4, A5, A6>::asynchronous;

    static DBusMessage *methodFunction(DBusConnection *conn,
                                       DBusMessage *msg, void *data)
    {
        try {
            typename dbus_traits<R>::host_type r;
            typename dbus_traits<A1>::host_type a1;
            typename dbus_traits<A2>::host_type a2;
            typename dbus_traits<A3>::host_type a3;
            typename dbus_traits<A4>::host_type a4;
            typename dbus_traits<A5>::host_type a5;
            typename dbus_traits<A6>::host_type a6;

            DBusMessageIter iter;
            dbus_message_iter_init(msg, &iter);
            dbus_traits<A1>::get(conn, msg, iter, a1);
            dbus_traits<A2>::get(conn, msg, iter, a2);
            dbus_traits<A3>::get(conn, msg, iter, a3);
            dbus_traits<A4>::get(conn, msg, iter, a4);
            dbus_traits<A5>::get(conn, msg, iter, a5);
            dbus_traits<A6>::get(conn, msg, iter, a6);

            r = (*static_cast<M *>(data))(a1, a2, a3, a4, a5, a6);

            if (asynchronous) {
                return NULL;
            }

            DBusMessage *reply = dbus_message_new_method_return(msg);
            if (!reply)
                return NULL;
            dbus_message_iter_init_append(reply, &iter);
            dbus_traits<R>::append_retval(iter, r);
            dbus_traits<A1>::append(iter, a1);
            dbus_traits<A2>::append(iter, a2);
            dbus_traits<A3>::append(iter, a3);
            dbus_traits<A4>::append(iter, a4);
            dbus_traits<A5>::append(iter, a5);
            dbus_traits<A6>::append(iter, a6);
            return reply;
        } catch (...) {
            return handleException(msg);
        }
    }

    static void destroyFunction(void *user_data)
    {
        BDBusMethodTable *entry = static_cast<BDBusMethodTable *>(user_data);
        delete static_cast<M *>(entry->method_data);
    }

    static BDBusMethodTable make(const char *name, BDBusMethodFlags flags, const M &m)
    {
        BDBusMethodTable entry;
        entry.name = strdup(name);
        std::string buffer;
        buffer += dbus_traits<A1>::getSignature();
        buffer += dbus_traits<A2>::getSignature();
        buffer += dbus_traits<A3>::getSignature();
        buffer += dbus_traits<A4>::getSignature();
        buffer += dbus_traits<A5>::getSignature();
        buffer += dbus_traits<A6>::getSignature();
        entry.signature = strdup(buffer.c_str());
        buffer.clear();
        buffer += dbus_traits<R>::getReply();
        buffer += dbus_traits<A1>::getReply();
        buffer += dbus_traits<A2>::getReply();
        buffer += dbus_traits<A3>::getReply();
        buffer += dbus_traits<A4>::getReply();
        buffer += dbus_traits<A5>::getReply();
        buffer += dbus_traits<A6>::getReply();
        entry.reply = strdup(buffer.c_str());
        entry.function = methodFunction;
        entry.destroy = destroyFunction;
        entry.flags = BDBusMethodFlags(flags | G_DBUS_METHOD_FLAG_METHOD_DATA |
                                       (asynchronous ? G_DBUS_METHOD_FLAG_ASYNC : 0));
        entry.method_data = new M(m);
        return entry;
    }
};

/** ===> 6 parameters */
template <class A1, class A2, class A3, class A4, class A5,
          class A6>
struct MakeMethodEntry< boost::function<void (A1, A2, A3, A4, A5, A6)> >
{
    typedef void (Mptr)(A1, A2, A3, A4, A5, A6);
    typedef boost::function<Mptr> M;

    template <class I, class C> static M boostptr(Mptr C::*method, I instance) {
        return boost::bind(method, instance, _1, _2, _3, _4, _5, _6);
    }

    static const bool asynchronous = DBusResult6<A1, A2, A3, A4, A5, A6>::asynchronous;

    static DBusMessage *methodFunction(DBusConnection *conn,
                                       DBusMessage *msg, void *data)
    {
        try {
            typename dbus_traits<A1>::host_type a1;
            typename dbus_traits<A2>::host_type a2;
            typename dbus_traits<A3>::host_type a3;
            typename dbus_traits<A4>::host_type a4;
            typename dbus_traits<A5>::host_type a5;
            typename dbus_traits<A6>::host_type a6;

            DBusMessageIter iter;
            dbus_message_iter_init(msg, &iter);
            dbus_traits<A1>::get(conn, msg, iter, a1);
            dbus_traits<A2>::get(conn, msg, iter, a2);
            dbus_traits<A3>::get(conn, msg, iter, a3);
            dbus_traits<A4>::get(conn, msg, iter, a4);
            dbus_traits<A5>::get(conn, msg, iter, a5);
            dbus_traits<A6>::get(conn, msg, iter, a6);

            (*static_cast<M *>(data))(a1, a2, a3, a4, a5, a6);

            if (asynchronous) {
                return NULL;
            }

            DBusMessage *reply = dbus_message_new_method_return(msg);
            if (!reply)
                return NULL;
            dbus_message_iter_init_append(reply, &iter);
            dbus_traits<A1>::append(iter, a1);
            dbus_traits<A2>::append(iter, a2);
            dbus_traits<A3>::append(iter, a3);
            dbus_traits<A4>::append(iter, a4);
            dbus_traits<A5>::append(iter, a5);
            dbus_traits<A6>::append(iter, a6);
            return reply;
        } catch (...) {
            return handleException(msg);
        }
    }
    static void destroyFunction(void *user_data)
    {
        BDBusMethodTable *entry = static_cast<BDBusMethodTable *>(user_data);
        delete static_cast<M *>(entry->method_data);
    }

    static BDBusMethodTable make(const char *name, BDBusMethodFlags flags, const M &m)
    {
        BDBusMethodTable entry;
        entry.name = strdup(name);
        std::string buffer;
        buffer += dbus_traits<A1>::getSignature();
        buffer += dbus_traits<A2>::getSignature();
        buffer += dbus_traits<A3>::getSignature();
        buffer += dbus_traits<A4>::getSignature();
        buffer += dbus_traits<A5>::getSignature();
        buffer += dbus_traits<A6>::getSignature();
        entry.signature = strdup(buffer.c_str());
        buffer.clear();
        buffer += dbus_traits<A1>::getReply();
        buffer += dbus_traits<A2>::getReply();
        buffer += dbus_traits<A3>::getReply();
        buffer += dbus_traits<A4>::getReply();
        buffer += dbus_traits<A5>::getReply();
        buffer += dbus_traits<A6>::getReply();
        entry.reply = strdup(buffer.c_str());
        entry.function = methodFunction;
        entry.destroy = destroyFunction;
        entry.flags = BDBusMethodFlags(flags | G_DBUS_METHOD_FLAG_METHOD_DATA |
                                       (asynchronous ? G_DBUS_METHOD_FLAG_ASYNC : 0));
        entry.method_data = new M(m);
        return entry;
    }
};

/** 5 arguments, 1 return value */
template <class R,
          class A1, class A2, class A3, class A4, class A5>
struct MakeMethodEntry< boost::function<R (A1, A2, A3, A4, A5)> >
{
    typedef R (Mptr)(A1, A2, A3, A4, A5);
    typedef boost::function<Mptr> M;

    template <class I, class C> static M boostptr(Mptr C::*method, I instance) {
        return boost::bind(method, instance, _1, _2, _3, _4, _5);
    }

    static const bool asynchronous = DBusResult5<A1, A2, A3, A4, A5>::asynchronous;

    static DBusMessage *methodFunction(DBusConnection *conn,
                                       DBusMessage *msg, void *data)
    {
        try {
            typename dbus_traits<R>::host_type r;
            typename dbus_traits<A1>::host_type a1;
            typename dbus_traits<A2>::host_type a2;
            typename dbus_traits<A3>::host_type a3;
            typename dbus_traits<A4>::host_type a4;
            typename dbus_traits<A5>::host_type a5;

            DBusMessageIter iter;
            dbus_message_iter_init(msg, &iter);
            dbus_traits<A1>::get(conn, msg, iter, a1);
            dbus_traits<A2>::get(conn, msg, iter, a2);
            dbus_traits<A3>::get(conn, msg, iter, a3);
            dbus_traits<A4>::get(conn, msg, iter, a4);
            dbus_traits<A5>::get(conn, msg, iter, a5);

            r = (*static_cast<M *>(data))(a1, a2, a3, a4, a5);

            if (asynchronous) {
                return NULL;
            }

            DBusMessage *reply = dbus_message_new_method_return(msg);
            if (!reply)
                return NULL;
            dbus_message_iter_init_append(reply, &iter);
            dbus_traits<R>::append_retval(iter, r);
            dbus_traits<A1>::append(iter, a1);
            dbus_traits<A2>::append(iter, a2);
            dbus_traits<A3>::append(iter, a3);
            dbus_traits<A4>::append(iter, a4);
            dbus_traits<A5>::append(iter, a5);
            return reply;
        } catch (...) {
            return handleException(msg);
        }
    }

    static void destroyFunction(void *user_data)
    {
        BDBusMethodTable *entry = static_cast<BDBusMethodTable *>(user_data);
        delete static_cast<M *>(entry->method_data);
    }

    static BDBusMethodTable make(const char *name, BDBusMethodFlags flags, const M &m)
    {
        BDBusMethodTable entry;
        entry.name = strdup(name);
        std::string buffer;
        buffer += dbus_traits<A1>::getSignature();
        buffer += dbus_traits<A2>::getSignature();
        buffer += dbus_traits<A3>::getSignature();
        buffer += dbus_traits<A4>::getSignature();
        buffer += dbus_traits<A5>::getSignature();
        entry.signature = strdup(buffer.c_str());
        buffer.clear();
        buffer += dbus_traits<R>::getReply();
        buffer += dbus_traits<A1>::getReply();
        buffer += dbus_traits<A2>::getReply();
        buffer += dbus_traits<A3>::getReply();
        buffer += dbus_traits<A4>::getReply();
        buffer += dbus_traits<A5>::getReply();
        entry.reply = strdup(buffer.c_str());
        entry.function = methodFunction;
        entry.destroy = destroyFunction;
        entry.flags = BDBusMethodFlags(flags | G_DBUS_METHOD_FLAG_METHOD_DATA |
                                       (asynchronous ? G_DBUS_METHOD_FLAG_ASYNC : 0));
        entry.method_data = new M(m);
        return entry;
    }
};

/** ===> 5 parameters */
template <class A1, class A2, class A3, class A4, class A5>
struct MakeMethodEntry< boost::function<void (A1, A2, A3, A4, A5)> >
{
    typedef void (Mptr)(A1, A2, A3, A4, A5);
    typedef boost::function<Mptr> M;

    template <class I, class C> static M boostptr(Mptr C::*method, I instance) {
        return boost::bind(method, instance, _1, _2, _3, _4, _5);
    }

    static const bool asynchronous = DBusResult5<A1, A2, A3, A4, A5>::asynchronous;

    static DBusMessage *methodFunction(DBusConnection *conn,
                                       DBusMessage *msg, void *data)
    {
        try {
            typename dbus_traits<A1>::host_type a1;
            typename dbus_traits<A2>::host_type a2;
            typename dbus_traits<A3>::host_type a3;
            typename dbus_traits<A4>::host_type a4;
            typename dbus_traits<A5>::host_type a5;

            DBusMessageIter iter;
            dbus_message_iter_init(msg, &iter);
            dbus_traits<A1>::get(conn, msg, iter, a1);
            dbus_traits<A2>::get(conn, msg, iter, a2);
            dbus_traits<A3>::get(conn, msg, iter, a3);
            dbus_traits<A4>::get(conn, msg, iter, a4);
            dbus_traits<A5>::get(conn, msg, iter, a5);

            (*static_cast<M *>(data))(a1, a2, a3, a4, a5);

            if (asynchronous) {
                return NULL;
            }

            DBusMessage *reply = dbus_message_new_method_return(msg);
            if (!reply)
                return NULL;
            dbus_message_iter_init_append(reply, &iter);
            dbus_traits<A1>::append(iter, a1);
            dbus_traits<A2>::append(iter, a2);
            dbus_traits<A3>::append(iter, a3);
            dbus_traits<A4>::append(iter, a4);
            dbus_traits<A5>::append(iter, a5);
            return reply;
        } catch (...) {
            return handleException(msg);
        }
    }

    static void destroyFunction(void *user_data)
    {
        BDBusMethodTable *entry = static_cast<BDBusMethodTable *>(user_data);
        delete static_cast<M *>(entry->method_data);
    }

    static BDBusMethodTable make(const char *name, BDBusMethodFlags flags, const M &m)
    {
        BDBusMethodTable entry;
        entry.name = strdup(name);
        std::string buffer;
        buffer += dbus_traits<A1>::getSignature();
        buffer += dbus_traits<A2>::getSignature();
        buffer += dbus_traits<A3>::getSignature();
        buffer += dbus_traits<A4>::getSignature();
        buffer += dbus_traits<A5>::getSignature();
        entry.signature = strdup(buffer.c_str());
        buffer.clear();
        buffer += dbus_traits<A1>::getReply();
        buffer += dbus_traits<A2>::getReply();
        buffer += dbus_traits<A3>::getReply();
        buffer += dbus_traits<A4>::getReply();
        buffer += dbus_traits<A5>::getReply();
        entry.reply = strdup(buffer.c_str());
        entry.function = methodFunction;
        entry.destroy = destroyFunction;
        entry.flags = BDBusMethodFlags(flags | G_DBUS_METHOD_FLAG_METHOD_DATA |
                                       (asynchronous ? G_DBUS_METHOD_FLAG_ASYNC : 0));
        entry.method_data = new M(m);
        return entry;
    }
};

/** 4 arguments, 1 return value */
template <class R,
          class A1, class A2, class A3, class A4>
struct MakeMethodEntry< boost::function<R (A1, A2, A3, A4)> >
{
    typedef R (Mptr)(A1, A2, A3, A4);
    typedef boost::function<Mptr> M;

    template <class I, class C> static M boostptr(Mptr C::*method, I instance) {
        return boost::bind(method, instance, _1, _2, _3, _4);
    }

    static const bool asynchronous = DBusResult4<A1, A2, A3, A4>::asynchronous;

    static DBusMessage *methodFunction(DBusConnection *conn,
                                       DBusMessage *msg, void *data)
    {
        try {
            typename dbus_traits<R>::host_type r;
            typename dbus_traits<A1>::host_type a1;
            typename dbus_traits<A2>::host_type a2;
            typename dbus_traits<A3>::host_type a3;
            typename dbus_traits<A4>::host_type a4;

            DBusMessageIter iter;
            dbus_message_iter_init(msg, &iter);
            dbus_traits<A1>::get(conn, msg, iter, a1);
            dbus_traits<A2>::get(conn, msg, iter, a2);
            dbus_traits<A3>::get(conn, msg, iter, a3);
            dbus_traits<A4>::get(conn, msg, iter, a4);

            r = (*static_cast<M *>(data))(a1, a2, a3, a4);

            if (asynchronous) {
                return NULL;
            }

            DBusMessage *reply = dbus_message_new_method_return(msg);
            if (!reply)
                return NULL;
            dbus_message_iter_init_append(reply, &iter);
            dbus_traits<R>::append_retval(iter, r);
            dbus_traits<A1>::append(iter, a1);
            dbus_traits<A2>::append(iter, a2);
            dbus_traits<A3>::append(iter, a3);
            dbus_traits<A4>::append(iter, a4);
            return reply;
        } catch (...) {
            return handleException(msg);
        }
    }

    static void destroyFunction(void *user_data)
    {
        BDBusMethodTable *entry = static_cast<BDBusMethodTable *>(user_data);
        delete static_cast<M *>(entry->method_data);
    }

    static BDBusMethodTable make(const char *name, BDBusMethodFlags flags, const M &m)
    {
        BDBusMethodTable entry;
        entry.name = strdup(name);
        std::string buffer;
        buffer += dbus_traits<A1>::getSignature();
        buffer += dbus_traits<A2>::getSignature();
        buffer += dbus_traits<A3>::getSignature();
        buffer += dbus_traits<A4>::getSignature();
        entry.signature = strdup(buffer.c_str());
        buffer.clear();
        buffer += dbus_traits<R>::getReply();
        buffer += dbus_traits<A1>::getReply();
        buffer += dbus_traits<A2>::getReply();
        buffer += dbus_traits<A3>::getReply();
        buffer += dbus_traits<A4>::getReply();
        entry.reply = strdup(buffer.c_str());
        entry.function = methodFunction;
        entry.destroy = destroyFunction;
        entry.flags = BDBusMethodFlags(flags | G_DBUS_METHOD_FLAG_METHOD_DATA |
                                       (asynchronous ? G_DBUS_METHOD_FLAG_ASYNC : 0));
        entry.method_data = new M(m);
        return entry;
    }
};

/** ===> 4 parameters */
template <class A1, class A2, class A3, class A4>
struct MakeMethodEntry< boost::function<void (A1, A2, A3, A4)> >
{
    typedef void (Mptr)(A1, A2, A3, A4);
    typedef boost::function<Mptr> M;

    template <class I, class C> static M boostptr(Mptr C::*method, I instance) {
        return boost::bind(method, instance, _1, _2, _3, _4);
    }

    static const bool asynchronous = DBusResult4<A1, A2, A3, A4>::asynchronous;

    static DBusMessage *methodFunction(DBusConnection *conn,
                                       DBusMessage *msg, void *data)
    {
        try {
            typename dbus_traits<A1>::host_type a1;
            typename dbus_traits<A2>::host_type a2;
            typename dbus_traits<A3>::host_type a3;
            typename dbus_traits<A4>::host_type a4;

            DBusMessageIter iter;
            dbus_message_iter_init(msg, &iter);
            dbus_traits<A1>::get(conn, msg, iter, a1);
            dbus_traits<A2>::get(conn, msg, iter, a2);
            dbus_traits<A3>::get(conn, msg, iter, a3);
            dbus_traits<A4>::get(conn, msg, iter, a4);

            (*static_cast<M *>(data))(a1, a2, a3, a4);

            if (asynchronous) {
                return NULL;
            }

            DBusMessage *reply = dbus_message_new_method_return(msg);
            if (!reply)
                return NULL;
            dbus_message_iter_init_append(reply, &iter);
            dbus_traits<A1>::append(iter, a1);
            dbus_traits<A2>::append(iter, a2);
            dbus_traits<A3>::append(iter, a3);
            dbus_traits<A4>::append(iter, a4);
            return reply;
        } catch (...) {
            return handleException(msg);
        }
    }

    static void destroyFunction(void *user_data)
    {
        BDBusMethodTable *entry = static_cast<BDBusMethodTable *>(user_data);
        delete static_cast<M *>(entry->method_data);
    }

    static BDBusMethodTable make(const char *name, BDBusMethodFlags flags, const M &m)
    {
        BDBusMethodTable entry;
        entry.name = strdup(name);
        std::string buffer;
        buffer += dbus_traits<A1>::getSignature();
        buffer += dbus_traits<A2>::getSignature();
        buffer += dbus_traits<A3>::getSignature();
        buffer += dbus_traits<A4>::getSignature();
        entry.signature = strdup(buffer.c_str());
        buffer.clear();
        buffer += dbus_traits<A1>::getReply();
        buffer += dbus_traits<A2>::getReply();
        buffer += dbus_traits<A3>::getReply();
        buffer += dbus_traits<A4>::getReply();
        entry.reply = strdup(buffer.c_str());
        entry.function = methodFunction;
        entry.destroy = destroyFunction;
        entry.flags = BDBusMethodFlags(flags | G_DBUS_METHOD_FLAG_METHOD_DATA |
                                       (asynchronous ? G_DBUS_METHOD_FLAG_ASYNC : 0));
        entry.method_data = new M(m);
        return entry;
    }
};

/** 3 arguments, 1 return value */
template <class R,
          class A1, class A2, class A3>
struct MakeMethodEntry< boost::function<R (A1, A2, A3)> >
{
    typedef R (Mptr)(A1, A2, A3);
    typedef boost::function<Mptr> M;

    template <class I, class C> static M boostptr(Mptr C::*method, I instance) {
        return boost::bind(method, instance, _1, _2, _3);
    }

    static const bool asynchronous = DBusResult3<A1, A2, A3>::asynchronous;

    static DBusMessage *methodFunction(DBusConnection *conn,
                                       DBusMessage *msg, void *data)
    {
        try {
            typename dbus_traits<R>::host_type r;
            typename dbus_traits<A1>::host_type a1;
            typename dbus_traits<A2>::host_type a2;
            typename dbus_traits<A3>::host_type a3;

            DBusMessageIter iter;
            dbus_message_iter_init(msg, &iter);
            dbus_traits<A1>::get(conn, msg, iter, a1);
            dbus_traits<A2>::get(conn, msg, iter, a2);
            dbus_traits<A3>::get(conn, msg, iter, a3);

            r = (*static_cast<M *>(data))(a1, a2, a3);

            if (asynchronous) {
                return NULL;
            }

            DBusMessage *reply = dbus_message_new_method_return(msg);
            if (!reply)
                return NULL;
            dbus_message_iter_init_append(reply, &iter);
            dbus_traits<R>::append_retval(iter, r);
            dbus_traits<A1>::append(iter, a1);
            dbus_traits<A2>::append(iter, a2);
            dbus_traits<A3>::append(iter, a3);
            return reply;
        } catch (...) {
            return handleException(msg);
        }
    }

    static void destroyFunction(void *user_data)
    {
        BDBusMethodTable *entry = static_cast<BDBusMethodTable *>(user_data);
        delete static_cast<M *>(entry->method_data);
    }

    static BDBusMethodTable make(const char *name, BDBusMethodFlags flags, const M &m)
    {
        BDBusMethodTable entry;
        entry.name = strdup(name);
        std::string buffer;
        buffer += dbus_traits<A1>::getSignature();
        buffer += dbus_traits<A2>::getSignature();
        buffer += dbus_traits<A3>::getSignature();
        entry.signature = strdup(buffer.c_str());
        buffer.clear();
        buffer += dbus_traits<R>::getReply();
        buffer += dbus_traits<A1>::getReply();
        buffer += dbus_traits<A2>::getReply();
        buffer += dbus_traits<A3>::getReply();
        entry.reply = strdup(buffer.c_str());
        entry.function = methodFunction;
        entry.destroy = destroyFunction;
        entry.flags = BDBusMethodFlags(flags | G_DBUS_METHOD_FLAG_METHOD_DATA |
                                       (asynchronous ? G_DBUS_METHOD_FLAG_ASYNC : 0));
        entry.method_data = new M(m);
        return entry;
    }
};

/** ===> 3 parameters */
template <class A1, class A2, class A3>
struct MakeMethodEntry< boost::function<void (A1, A2, A3)> >
{
    typedef void (Mptr)(A1, A2, A3);
    typedef boost::function<Mptr> M;

    template <class I, class C> static M boostptr(Mptr C::*method, I instance) {
        return boost::bind(method, instance, _1, _2, _3);
    }

    static const bool asynchronous = DBusResult3<A1, A2, A3>::asynchronous;

    static DBusMessage *methodFunction(DBusConnection *conn,
                                       DBusMessage *msg, void *data)
    {
        try {
            typename dbus_traits<A1>::host_type a1;
            typename dbus_traits<A2>::host_type a2;
            typename dbus_traits<A3>::host_type a3;

            DBusMessageIter iter;
            dbus_message_iter_init(msg, &iter);
            dbus_traits<A1>::get(conn, msg, iter, a1);
            dbus_traits<A2>::get(conn, msg, iter, a2);
            dbus_traits<A3>::get(conn, msg, iter, a3);

            (*static_cast<M *>(data))(a1, a2, a3);

            if (asynchronous) {
                return NULL;
            }

            DBusMessage *reply = dbus_message_new_method_return(msg);
            if (!reply)
                return NULL;
            dbus_message_iter_init_append(reply, &iter);
            dbus_traits<A1>::append(iter, a1);
            dbus_traits<A2>::append(iter, a2);
            dbus_traits<A3>::append(iter, a3);
            return reply;
        } catch (...) {
            return handleException(msg);
        }
    }

    static void destroyFunction(void *user_data)
    {
        BDBusMethodTable *entry = static_cast<BDBusMethodTable *>(user_data);
        delete static_cast<M *>(entry->method_data);
    }

    static BDBusMethodTable make(const char *name, BDBusMethodFlags flags, const M &m)
    {
        BDBusMethodTable entry;
        entry.name = strdup(name);
        std::string buffer;
        buffer += dbus_traits<A1>::getSignature();
        buffer += dbus_traits<A2>::getSignature();
        buffer += dbus_traits<A3>::getSignature();
        entry.signature = strdup(buffer.c_str());
        buffer.clear();
        buffer += dbus_traits<A1>::getReply();
        buffer += dbus_traits<A2>::getReply();
        buffer += dbus_traits<A3>::getReply();
        entry.reply = strdup(buffer.c_str());
        entry.function = methodFunction;
        entry.destroy = destroyFunction;
        entry.flags = BDBusMethodFlags(flags | G_DBUS_METHOD_FLAG_METHOD_DATA |
                                       (asynchronous ? G_DBUS_METHOD_FLAG_ASYNC : 0));
        entry.method_data = new M(m);
        return entry;
    }
};

/** 2 arguments, 1 return value */
template <class R,
          class A1, class A2>
struct MakeMethodEntry< boost::function<R (A1, A2)> >
{
    typedef R (Mptr)(A1, A2);
    typedef boost::function<Mptr> M;

    template <class I, class C> static M boostptr(Mptr C::*method, I instance) {
        return boost::bind(method, instance, _1, _2);
    }

    static const bool asynchronous = DBusResult2<A1, A2>::asynchronous;

    static DBusMessage *methodFunction(DBusConnection *conn,
                                       DBusMessage *msg, void *data)
    {
        try {
            typename dbus_traits<R>::host_type r;
            typename dbus_traits<A1>::host_type a1;
            typename dbus_traits<A2>::host_type a2;

            DBusMessageIter iter;
            dbus_message_iter_init(msg, &iter);
            dbus_traits<A1>::get(conn, msg, iter, a1);
            dbus_traits<A2>::get(conn, msg, iter, a2);

            r = (*static_cast<M *>(data))(a1, a2);

            if (asynchronous) {
                return NULL;
            }

            DBusMessage *reply = dbus_message_new_method_return(msg);
            if (!reply)
                return NULL;
            dbus_message_iter_init_append(reply, &iter);
            dbus_traits<R>::append_retval(iter, r);
            dbus_traits<A1>::append(iter, a1);
            dbus_traits<A2>::append(iter, a2);
            return reply;
        } catch (...) {
            return handleException(msg);
        }
    }

    static void destroyFunction(void *user_data)
    {
        BDBusMethodTable *entry = static_cast<BDBusMethodTable *>(user_data);
        delete static_cast<M *>(entry->method_data);
    }

    static BDBusMethodTable make(const char *name, BDBusMethodFlags flags, const M &m)
    {
        BDBusMethodTable entry;
        entry.name = strdup(name);
        std::string buffer;
        buffer += dbus_traits<A1>::getSignature();
        buffer += dbus_traits<A2>::getSignature();
        entry.signature = strdup(buffer.c_str());
        buffer.clear();
        buffer += dbus_traits<R>::getReply();
        buffer += dbus_traits<A1>::getReply();
        buffer += dbus_traits<A2>::getReply();
        entry.reply = strdup(buffer.c_str());
        entry.function = methodFunction;
        entry.destroy = destroyFunction;
        entry.flags = BDBusMethodFlags(flags | G_DBUS_METHOD_FLAG_METHOD_DATA |
                                       (asynchronous ? G_DBUS_METHOD_FLAG_ASYNC : 0));
        entry.method_data = new M(m);
        return entry;
    }
};

/** ===> 2 parameters */
template <class A1, class A2>
struct MakeMethodEntry< boost::function<void (A1, A2)> >
{
    typedef void (Mptr)(A1, A2);
    typedef boost::function<Mptr> M;

    template <class I, class C> static M boostptr(Mptr C::*method, I instance) {
        return boost::bind(method, instance, _1, _2);
    }

    static const bool asynchronous = DBusResult2<A1, A2>::asynchronous;

    static DBusMessage *methodFunction(DBusConnection *conn,
                                       DBusMessage *msg, void *data)
    {
        try {
            typename dbus_traits<A1>::host_type a1;
            typename dbus_traits<A2>::host_type a2;

            DBusMessageIter iter;
            dbus_message_iter_init(msg, &iter);
            dbus_traits<A1>::get(conn, msg, iter, a1);
            dbus_traits<A2>::get(conn, msg, iter, a2);

            (*static_cast<M *>(data))(a1, a2);

            if (asynchronous) {
                return NULL;
            }

            DBusMessage *reply = dbus_message_new_method_return(msg);
            if (!reply)
                return NULL;
            dbus_message_iter_init_append(reply, &iter);
            dbus_traits<A1>::append(iter, a1);
            dbus_traits<A2>::append(iter, a2);
            return reply;
        } catch (...) {
            return handleException(msg);
        }
    }

    static void destroyFunction(void *user_data)
    {
        BDBusMethodTable *entry = static_cast<BDBusMethodTable *>(user_data);
        delete static_cast<M *>(entry->method_data);
    }

    static BDBusMethodTable make(const char *name, BDBusMethodFlags flags, const M &m)
    {
        BDBusMethodTable entry;
        entry.name = strdup(name);
        std::string buffer;
        buffer += dbus_traits<A1>::getSignature();
        buffer += dbus_traits<A2>::getSignature();
        entry.signature = strdup(buffer.c_str());
        buffer.clear();
        buffer += dbus_traits<A1>::getReply();
        buffer += dbus_traits<A2>::getReply();
        entry.reply = strdup(buffer.c_str());
        entry.function = methodFunction;
        entry.destroy = destroyFunction;
        entry.flags = BDBusMethodFlags(flags | G_DBUS_METHOD_FLAG_METHOD_DATA |
                                       (asynchronous ? G_DBUS_METHOD_FLAG_ASYNC : 0));
        entry.method_data = new M(m);
        return entry;
    }
};

/** 1 argument, 1 return value */
template <class R,
          class A1>
struct MakeMethodEntry< boost::function<R (A1)> >
{
    typedef R (Mptr)(A1);
    typedef boost::function<Mptr> M;

    template <class I, class C> static M boostptr(Mptr C::*method, I instance) {
        return boost::bind(method, instance, _1);
    }

    static const bool asynchronous = DBusResult1<A1>::asynchronous;

    static DBusMessage *methodFunction(DBusConnection *conn,
                                       DBusMessage *msg, void *data)
    {
        try {
            typename dbus_traits<R>::host_type r;
            typename dbus_traits<A1>::host_type a1;

            DBusMessageIter iter;
            dbus_message_iter_init(msg, &iter);
            dbus_traits<A1>::get(conn, msg, iter, a1);

            r = (*static_cast<M *>(data))(a1);

            if (asynchronous) {
                return NULL;
            }

            DBusMessage *reply = dbus_message_new_method_return(msg);
            if (!reply)
                return NULL;
            dbus_message_iter_init_append(reply, &iter);
            dbus_traits<R>::append_retval(iter, r);
            dbus_traits<A1>::append(iter, a1);
            return reply;
        } catch (...) {
            return handleException(msg);
        }
    }

    static void destroyFunction(void *user_data)
    {
        BDBusMethodTable *entry = static_cast<BDBusMethodTable *>(user_data);
        delete static_cast<M *>(entry->method_data);
    }

    static BDBusMethodTable make(const char *name, BDBusMethodFlags flags, const M &m)
    {
        BDBusMethodTable entry;
        entry.name = strdup(name);
        std::string buffer;
        buffer += dbus_traits<A1>::getSignature();
        entry.signature = strdup(buffer.c_str());
        buffer.clear();
        buffer += dbus_traits<R>::getReply();
        buffer += dbus_traits<A1>::getReply();
        entry.reply = strdup(buffer.c_str());
        entry.function = methodFunction;
        entry.destroy = destroyFunction;
        entry.flags = BDBusMethodFlags(flags | G_DBUS_METHOD_FLAG_METHOD_DATA |
                                       (asynchronous ? G_DBUS_METHOD_FLAG_ASYNC : 0));
        entry.method_data = new M(m);
        return entry;
    }
};

/** ===> 1 parameter */
template <class A1>
struct MakeMethodEntry< boost::function<void (A1)> >
{
    typedef void (Mptr)(A1);
    typedef boost::function<void (A1)> M;

    template <class I, class C> static M boostptr(Mptr C::*method, I instance) {
        return boost::bind(method, instance, _1);
    }

    static const bool asynchronous = DBusResult1<A1>::asynchronous;

    static DBusMessage *methodFunction(DBusConnection *conn,
                                       DBusMessage *msg, void *data)
    {
        try {
            typename dbus_traits<A1>::host_type a1;

            DBusMessageIter iter;
            dbus_message_iter_init(msg, &iter);
            dbus_traits<A1>::get(conn, msg, iter, a1);

            (*static_cast<M *>(data))(a1);

            if (asynchronous) {
                return NULL;
            }

            DBusMessage *reply = dbus_message_new_method_return(msg);
            if (!reply)
                return NULL;
            dbus_message_iter_init_append(reply, &iter);
            dbus_traits<A1>::append(iter, a1);
            return reply;
        } catch (...) {
            return handleException(msg);
        }
    }

    static void destroyFunction(void *user_data)
    {
        BDBusMethodTable *entry = static_cast<BDBusMethodTable *>(user_data);
        delete static_cast<M *>(entry->method_data);
    }

    static BDBusMethodTable make(const char *name, BDBusMethodFlags flags, const M &m)
    {
        BDBusMethodTable entry;
        entry.name = strdup(name);
        std::string buffer;
        buffer += dbus_traits<A1>::getSignature();
        entry.signature = strdup(buffer.c_str());
        buffer.clear();
        buffer += dbus_traits<A1>::getReply();
        entry.reply = strdup(buffer.c_str());
        entry.function = methodFunction;
        entry.destroy = destroyFunction;
        entry.flags = BDBusMethodFlags(flags | G_DBUS_METHOD_FLAG_METHOD_DATA |
                                       (asynchronous ? G_DBUS_METHOD_FLAG_ASYNC : 0));
        entry.method_data = new M(m);
        return entry;
    }
};

/** 0 arguments, 1 return value */
template <class R>
struct MakeMethodEntry< boost::function<R ()> >
{
    typedef R (Mptr)();
    typedef boost::function<Mptr> M;

    template <class I, class C> static M boostptr(Mptr C::*method, I instance) {
        return boost::bind(method, instance);
    }

    static DBusMessage *methodFunction(DBusConnection *conn,
                                       DBusMessage *msg, void *data)
    {
        try {
            typename dbus_traits<R>::host_type r;

            r = (*static_cast<M *>(data))();

            DBusMessage *reply = dbus_message_new_method_return(msg);
            if (!reply)
                return NULL;
            DBusMessageIter iter;
            dbus_message_iter_init_append(reply, &iter);
            dbus_traits<R>::append_retval(iter, r);
            return reply;
        } catch (...) {
            return handleException(msg);
        }
    }
    static void destroyFunction(void *user_data)
    {
        BDBusMethodTable *entry = static_cast<BDBusMethodTable *>(user_data);
        delete static_cast<M *>(entry->method_data);
    }

    static BDBusMethodTable make(const char *name, BDBusMethodFlags flags, const M &m)
    {
        BDBusMethodTable entry;
        entry.name = strdup(name);
        std::string buffer;
        entry.signature = strdup(buffer.c_str());
        buffer.clear();
        buffer += dbus_traits<R>::getReply();
        entry.reply = strdup(buffer.c_str());
        entry.function = methodFunction;
        entry.destroy = destroyFunction;
        entry.flags = BDBusMethodFlags(flags | G_DBUS_METHOD_FLAG_METHOD_DATA);
        entry.method_data = new M(m);
        return entry;
    }
};

/** ===> 0 parameter */
template <>
struct MakeMethodEntry< boost::function<void ()> >
{
    typedef void (Mptr)();
    typedef boost::function<Mptr> M;

    template <class I, class C> static M boostptr(Mptr C::*method, I instance) {
        return boost::bind(method, instance);
    }

    static DBusMessage *methodFunction(DBusConnection *conn,
                                       DBusMessage *msg, void *data)
    {
        try {
            (*static_cast<M *>(data))();

            DBusMessage *reply = dbus_message_new_method_return(msg);
            if (!reply)
                return NULL;
            return reply;
        } catch (...) {
            return handleException(msg);
        }
    }

    static void destroyFunction(void *user_data)
    {
        BDBusMethodTable *entry = static_cast<BDBusMethodTable *>(user_data);
        delete static_cast<M *>(entry->method_data);
    }

    static BDBusMethodTable make(const char *name, BDBusMethodFlags flags, const M &m)
    {
        BDBusMethodTable entry;
        entry.name = strdup(name);
        std::string buffer;
        entry.signature = strdup(buffer.c_str());
        buffer.clear();
        entry.reply = strdup(buffer.c_str());
        entry.function = methodFunction;
        entry.destroy = destroyFunction;
        entry.flags = BDBusMethodFlags(flags | G_DBUS_METHOD_FLAG_METHOD_DATA);
        entry.method_data = new M(m);
        return entry;
    }
};

/**
 * interface to refer to a remote object
 */
class DBusRemoteObject : public DBusObject
{
public:
    virtual const char *getDestination() const = 0;
    virtual ~DBusRemoteObject() {}
};
/**
 * interface expected by DBusClient
 */
class DBusCallObject : public DBusRemoteObject
{
public:
    /* The method name for the calling dbus method */
    virtual const char *getMethod() const =0;
    virtual ~DBusCallObject() {}
};

template <class T>
class DBusClientCall
{
protected:
    const std::string m_destination;
    const std::string m_path;
    const std::string m_interface;
    const std::string m_method;
    const DBusConnectionPtr m_conn;

    typedef DBusPendingCallNotifyFunction DBusCallback;
    DBusCallback m_dbusCallback;

    /**
     * called by libdbus to free the user_data pointer set in 
     * dbus_pending_call_set_notify()
     */
    static void callDataUnref(void *user_data) {
        delete static_cast<CallbackData *>(user_data);
    }

    typedef T Callback_t;

public:
    struct CallbackData
    {
        //only keep connection, for DBusClientCall instance is absent when 'dbus client call' returns
        //suppose connection is available in the callback handler
        const DBusConnectionPtr m_conn;
        Callback_t m_callback;
        CallbackData(const DBusConnectionPtr &conn, const Callback_t &callback)
            :m_conn(conn), m_callback(callback)
        {}
    };

    DBusClientCall(const DBusCallObject &object, DBusCallback dbusCallback)
        :m_destination (object.getDestination()),
         m_path (object.getPath()),
         m_interface (object.getInterface()),
         m_method (object.getMethod()),
         m_conn (object.getConnection()),
         m_dbusCallback(dbusCallback)
    {
    }

    DBusClientCall(const DBusRemoteObject &object, const std::string &method, DBusCallback dbusCallback)
        :m_destination (object.getDestination()),
         m_path (object.getPath()),
         m_interface (object.getInterface()),
         m_method (method),
         m_conn (object.getConnection()),
         m_dbusCallback(dbusCallback)
    {
    }

    DBusConnection *getConnection() { return m_conn.get(); }

    void operator () (const Callback_t &callback)
    {
        DBusPendingCall *call;
        DBusMessagePtr msg(dbus_message_new_method_call(
                    m_destination.c_str(),
                    m_path.c_str(),
                    m_interface.c_str(),
                    m_method.c_str()));
        if (!msg) {
            throw std::runtime_error("dbus_message_new_method_call() failed");
        }

        //parameter marshaling (none)
        if (!dbus_connection_send_with_reply(m_conn.get(), msg.get(), &call, -1)) {
            throw std::runtime_error("dbus_connection_send failed");
        }

        DBusPendingCallPtr mCall (call);
        CallbackData *data = new CallbackData(m_conn, callback);
        dbus_pending_call_set_notify(mCall.get(),
                                     m_dbusCallback,
                                     data,
                                     callDataUnref);
    }

    template <class A1>
    void operator () (const A1 &a1, const Callback_t &callback)
    {
        DBusPendingCall *call;
        DBusMessagePtr msg(dbus_message_new_method_call(
                    m_destination.c_str(),
                    m_path.c_str(),
                    m_interface.c_str(),
                    m_method.c_str()));
        if (!msg) {
            throw std::runtime_error("dbus_message_new_method_call() failed");
        }
        append_retvals(msg, a1);

        //parameter marshaling (none)
        if (!dbus_connection_send_with_reply(m_conn.get(), msg.get(), &call, -1)) {
            throw std::runtime_error("dbus_connection_send failed");
        }

        DBusPendingCallPtr mCall (call);
        CallbackData *data = new CallbackData(m_conn, callback);
        dbus_pending_call_set_notify(mCall.get(),
                                     m_dbusCallback,
                                     data,
                                     callDataUnref);
    }

    template <class A1, class A2>
    void operator () (const A1 &a1, const A2 &a2, const Callback_t &callback)
    {
        DBusPendingCall *call;
        DBusMessagePtr msg(dbus_message_new_method_call(
                    m_destination.c_str(),
                    m_path.c_str(),
                    m_interface.c_str(),
                    m_method.c_str()));
        if (!msg) {
            throw std::runtime_error("dbus_message_new_method_call() failed");
        }
        append_retvals(msg, a1);
        append_retvals(msg, a2);

        //parameter marshaling (none)
        if (!dbus_connection_send_with_reply(m_conn.get(), msg.get(), &call, -1)) {
            throw std::runtime_error("dbus_connection_send failed");
        }

        DBusPendingCallPtr mCall (call);
        CallbackData *data = new CallbackData(m_conn, callback);
        dbus_pending_call_set_notify(mCall.get(),
                                     m_dbusCallback,
                                     data,
                                     callDataUnref);
    }

    template <class A1, class A2, class A3>
    void operator () (const A1 &a1, const A2 &a2, const A3 &a3, const Callback_t &callback)
    {
        DBusPendingCall *call;
        DBusMessagePtr msg(dbus_message_new_method_call(
                    m_destination.c_str(),
                    m_path.c_str(),
                    m_interface.c_str(),
                    m_method.c_str()));
        if (!msg) {
            throw std::runtime_error("dbus_message_new_method_call() failed");
        }
        append_retvals(msg, a1);
        append_retvals(msg, a2);
        append_retvals(msg, a3);

        //parameter marshaling (none)
        if (!dbus_connection_send_with_reply(m_conn.get(), msg.get(), &call, -1)) {
            throw std::runtime_error("dbus_connection_send failed");
        }

        DBusPendingCallPtr mCall (call);
        CallbackData *data = new CallbackData(m_conn, callback);
        dbus_pending_call_set_notify(mCall.get(),
                                     m_dbusCallback,
                                     data,
                                     callDataUnref);
    }
};

/*
 * A DBus Client Call object handling zero or more parameter and
 * zero return value.
 */
class DBusClientCall0 : public DBusClientCall<boost::function<void (const std::string &)> >
{
    /**
     * called when result of call is available or an error occurred (non-empty string)
     */
    typedef boost::function<void (const std::string &)> Callback_t;

    /** called by libdbus on error or completion of call */
    static void dbusCallback (DBusPendingCall *call, void *user_data)
    {
        CallbackData *data = static_cast<CallbackData *>(user_data);
        DBusMessagePtr reply = dbus_pending_call_steal_reply (call);
        const char* errname = dbus_message_get_error_name (reply.get());
        std::string error;
        if (errname) {
            error = errname;
        }
        //unmarshal the return results and call user callback
        (data->m_callback)(error);
    }

public:
    DBusClientCall0 (const DBusCallObject &object)
        : DBusClientCall<Callback_t>(object, &DBusClientCall0::dbusCallback) 
    {
    }

    DBusClientCall0 (const DBusRemoteObject &object, const std::string &method)
        : DBusClientCall<Callback_t>(object, method, &DBusClientCall0::dbusCallback)
    {
    }
};

/** 1 return value and 0 or more parameters */
template <class R1>
class DBusClientCall1 : public DBusClientCall<boost::function<void (const R1 &, const std::string &)> >
{
    /**
     * called when the call is returned or an error occurred (non-empty string)
     */
    typedef boost::function<void (const R1 &, const std::string &)> Callback_t;

    /** called by libdbus on error or completion of call */
    static void dbusCallback (DBusPendingCall *call, void *user_data)
    {
        typedef typename DBusClientCall<Callback_t>::CallbackData CallbackData;
        CallbackData *data = static_cast<CallbackData *>(user_data);
        DBusMessagePtr reply = dbus_pending_call_steal_reply (call);
        const char* errname = dbus_message_get_error_name (reply.get());
        std::string error;
        typename dbus_traits<R1>::host_type r;
        if (!errname) {
            DBusMessageIter iter;
            dbus_message_iter_init(reply.get(), &iter);
            dbus_traits<R1>::get(data->m_conn.get(), reply.get(), iter, r);
        } else {
            error = errname;
        }
        //unmarshal the return results and call user callback
        //(*static_cast <Callback_t *>(user_data))(r, error);
        (data->m_callback)(r, error);
    }

public:
    DBusClientCall1 (const DBusCallObject &object)
        : DBusClientCall<Callback_t>(object, &DBusClientCall1::dbusCallback) 
    {
    }

    DBusClientCall1 (const DBusRemoteObject &object, const std::string &method)
        : DBusClientCall<Callback_t>(object, method, &DBusClientCall1::dbusCallback)
    {
    }
};

/** 2 return value and 0 or more parameters */
template <class R1, class R2>
class DBusClientCall2 : public DBusClientCall<boost::function<
                               void (const R1 &, const R2 &, const std::string &)> >

{
    /**
     * called when the call is returned or an error occurred (non-empty string)
     */
    typedef boost::function<void (const R1 &, const R2 &, const std::string &)> Callback_t;

    /** called by libdbus on error or completion of call */
    static void dbusCallback (DBusPendingCall *call, void *user_data)
    {
        typedef typename DBusClientCall<Callback_t>::CallbackData CallbackData;
        CallbackData *data = static_cast<CallbackData *>(user_data);
        DBusMessagePtr reply = dbus_pending_call_steal_reply (call);
        const char* errname = dbus_message_get_error_name (reply.get());
        std::string error;
        typename dbus_traits<R1>::host_type r1;
        typename dbus_traits<R2>::host_type r2;
        if (!errname) {
            DBusMessageIter iter;
            dbus_message_iter_init(reply.get(), &iter);
            dbus_traits<R1>::get(data->m_conn.get(), reply.get(), iter, r1);
            dbus_traits<R2>::get(data->m_conn.get(), reply.get(), iter, r2);
        } else {
            error = errname;
        }
        //unmarshal the return results and call user callback
        (data->m_callback)(r1, r2, error);
    }

public:
    DBusClientCall2 (const DBusCallObject &object)
        : DBusClientCall<Callback_t>(object, &DBusClientCall2::dbusCallback) 
    {
    }

    DBusClientCall2 (const DBusRemoteObject &object, const std::string &method)
        : DBusClientCall<Callback_t>(object, method, &DBusClientCall2::dbusCallback)
    {
    }
};

/** 3 return value and 0 or more parameters */
template <class R1, class R2, class R3>
class DBusClientCall3 : public DBusClientCall<boost::function<
                               void (const R1 &, const R2 &, const R3 &, const std::string &)> >

{
    /**
     * called when the call is returned or an error occurred (non-empty string)
     */
    typedef boost::function<void (const R1 &, const R2 &, const R3 &, const std::string &)> Callback_t;

    /** called by libdbus on error or completion of call */
    static void dbusCallback (DBusPendingCall *call, void *user_data)
    {
        typedef typename DBusClientCall<Callback_t>::CallbackData CallbackData;
        CallbackData *data = static_cast<CallbackData *>(user_data);
        DBusMessagePtr reply = dbus_pending_call_steal_reply (call);
        const char* errname = dbus_message_get_error_name (reply.get());
        std::string error;
        typename dbus_traits<R1>::host_type r1;
        typename dbus_traits<R2>::host_type r2;
        typename dbus_traits<R3>::host_type r3;
        if (!errname) {
            DBusMessageIter iter;
            dbus_message_iter_init(reply.get(), &iter);
            dbus_traits<R1>::get(data->m_conn.get(), reply.get(), iter, r1);
            dbus_traits<R2>::get(data->m_conn.get(), reply.get(), iter, r2);
            dbus_traits<R3>::get(data->m_conn.get(), reply.get(), iter, r3);
        } else {
            error = errname;
        }
        //unmarshal the return results and call user callback
        (data->m_callback)(r1, r2, r3, error);
    }

public:
    DBusClientCall3 (const DBusCallObject &object)
        : DBusClientCall<Callback_t>(object, &DBusClientCall3::dbusCallback) 
    {
    }

    DBusClientCall3 (const DBusRemoteObject &object, const std::string &method)
        : DBusClientCall<Callback_t>(object, method, &DBusClientCall3::dbusCallback)
    {
    }
};

/**
 * Common functionality of all SignalWatch* classes.
 * @param T     boost::function with the right signature
 */
template <class T> class SignalWatch
{
 public:
    SignalWatch(const DBusRemoteObject &object,
                 const std::string &signal)
        : m_object(object), m_signal(signal)
    {
    }

    ~SignalWatch()
    {
        if (m_tag) {
            b_dbus_remove_watch(m_object.getConnection(), m_tag);
        }
    }

    typedef T Callback_t;
    const Callback_t &getCallback() const{ return m_callback; }

 protected:
    const DBusRemoteObject &m_object;
    std::string m_signal;
    guint m_tag;
    T m_callback;

    std::string makeSignalRule() {
        std::string rule;
        rule = "type='signal',path='";
        rule += m_object.getPath();
        rule += "',interface='";
        rule += m_object.getInterface();
        rule += "',member='";
        rule += m_signal;
        rule += "'";
        return rule;
    }

    static gboolean isMatched(DBusMessage *msg, void *data) {
        SignalWatch *watch = static_cast<SignalWatch*>(data);
        return dbus_message_has_path(msg, watch->m_object.getPath()) &&
                dbus_message_is_signal(msg, watch->m_object.getInterface(), watch->m_signal.c_str());
    }

    void activateInternal(const Callback_t &callback,
                          gboolean (*cb)(DBusConnection *, DBusMessage *, void *))
    {
        m_callback = callback;
        std::string rule = makeSignalRule();
        m_tag = b_dbus_add_signal_watch(m_object.getConnection(),
                                        rule.c_str(),
                                        cb,
                                        this,
                                        NULL);
    }
};

class SignalWatch0 : public SignalWatch< boost::function<void (void)> >
{
    typedef boost::function<void (void)> Callback_t;

 public:
    SignalWatch0(const DBusRemoteObject &object,
                 const std::string &signal)
        : SignalWatch<Callback_t>(object, signal)
    {
    }

    static gboolean internalCallback(DBusConnection *conn, DBusMessage *msg, void *data)
    {
        if(isMatched(msg, data) == FALSE) {
            return TRUE;
        }
        const Callback_t &cb = static_cast< SignalWatch<Callback_t> *>(data)->getCallback();
        cb();

        return TRUE;
    }

    void activate(const Callback_t &callback) { activateInternal(callback, internalCallback); }
};

template <typename A1>
class SignalWatch1 : public SignalWatch< boost::function<void (const A1 &)> >
{
    typedef boost::function<void (const A1 &)> Callback_t;

 public:
    SignalWatch1(const DBusRemoteObject &object,
                 const std::string &signal)
        : SignalWatch<Callback_t>(object, signal)
    {
    }

    static gboolean internalCallback(DBusConnection *conn, DBusMessage *msg, void *data)
    {
        if (SignalWatch<Callback_t>::isMatched(msg, data) == FALSE) {
            return TRUE;
        }
        const Callback_t &cb =static_cast< SignalWatch<Callback_t> *>(data)->getCallback();

        typename dbus_traits<A1>::host_type a1;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(conn, msg, iter, a1);
        cb(a1);

        return TRUE;
    }

    void activate(const Callback_t &callback) { activateInternal(callback, internalCallback); }
};

template <typename A1, typename A2>
class SignalWatch2 : public SignalWatch< boost::function<void (const A1 &, const A2 &)> >
{
    typedef boost::function<void (const A1 &, const A2 &)> Callback_t;

 public:
    SignalWatch2(const DBusRemoteObject &object,
                 const std::string &signal)
        : SignalWatch<Callback_t>(object, signal)
    {
    }

    static gboolean internalCallback(DBusConnection *conn, DBusMessage *msg, void *data)
    {
        if (SignalWatch<Callback_t>::isMatched(msg, data) == FALSE) {
            return TRUE;
        }
        const Callback_t &cb = static_cast< SignalWatch<Callback_t> *>(data)->getCallback();

        typename dbus_traits<A1>::host_type a1;
        typename dbus_traits<A2>::host_type a2;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(conn, msg, iter, a1);
        dbus_traits<A2>::get(conn, msg, iter, a2);
        cb(a1, a2);

        return TRUE;
    }

    void activate(const Callback_t &callback) { activateInternal(callback, internalCallback); }
};

template <typename A1, typename A2, typename A3>
class SignalWatch3 : public SignalWatch< boost::function<void (const A1 &, const A2 &, const A3 &)> >
{
    typedef boost::function<void (const A1 &, const A2 &, const A3 &)> Callback_t;

 public:
    SignalWatch3(const DBusRemoteObject &object,
                 const std::string &signal)
        : SignalWatch<Callback_t>(object, signal)
    {
    }

    static gboolean internalCallback(DBusConnection *conn, DBusMessage *msg, void *data)
    {
        if (SignalWatch<Callback_t>::isMatched(msg, data) == FALSE) {
            return TRUE;
        }
        const Callback_t &cb =static_cast< SignalWatch<Callback_t> *>(data)->getCallback();

        typename dbus_traits<A1>::host_type a1;
        typename dbus_traits<A2>::host_type a2;
        typename dbus_traits<A3>::host_type a3;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(conn, msg, iter, a1);
        dbus_traits<A2>::get(conn, msg, iter, a2);
        dbus_traits<A3>::get(conn, msg, iter, a3);
        cb(a1, a2, a3);

        return TRUE;
    }

    void activate(const Callback_t &callback) { activateInternal(callback, internalCallback); }
};

template <typename A1, typename A2, typename A3, typename A4>
class SignalWatch4 : public SignalWatch< boost::function<void (const A1 &, const A2 &, const A3 &, const A4 &)> >
{
    typedef boost::function<void (const A1 &, const A2 &, const A3 &, const A4 &)> Callback_t;

 public:
    SignalWatch4(const DBusRemoteObject &object,
                 const std::string &signal)
        : SignalWatch<Callback_t>(object, signal)
    {
    }

    static gboolean internalCallback(DBusConnection *conn, DBusMessage *msg, void *data)
    {
        if (SignalWatch<Callback_t>::isMatched(msg, data) == FALSE) {
            return TRUE;
        }
        const Callback_t &cb = static_cast< SignalWatch<Callback_t> *>(data)->getCallback();

        typename dbus_traits<A1>::host_type a1;
        typename dbus_traits<A2>::host_type a2;
        typename dbus_traits<A3>::host_type a3;
        typename dbus_traits<A4>::host_type a4;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(conn, msg, iter, a1);
        dbus_traits<A2>::get(conn, msg, iter, a2);
        dbus_traits<A3>::get(conn, msg, iter, a3);
        dbus_traits<A4>::get(conn, msg, iter, a4);
        cb(a1, a2, a3, a4);

        return TRUE;
    }

    void activate(const Callback_t &callback) { activateInternal(callback, internalCallback); }
};

template <typename A1, typename A2, typename A3, typename A4, typename A5>
class SignalWatch5 : public SignalWatch< boost::function<void (const A1 &, const A2 &, const A3 &, const A4 &, const A5 &)> >
{
    typedef boost::function<void (const A1 &, const A2 &, const A3 &, const A4 &, const A5 &)> Callback_t;

 public:
    SignalWatch5(const DBusRemoteObject &object,
                 const std::string &signal)
        : SignalWatch<Callback_t>(object, signal)
    {
    }

    static gboolean internalCallback(DBusConnection *conn, DBusMessage *msg, void *data)
    {
        if (SignalWatch<Callback_t>::isMatched(msg, data) == FALSE) {
            return TRUE;
        }
        const Callback_t &cb = static_cast< SignalWatch<Callback_t> *>(data)->getCallback();

        typename dbus_traits<A1>::host_type a1;
        typename dbus_traits<A2>::host_type a2;
        typename dbus_traits<A3>::host_type a3;
        typename dbus_traits<A4>::host_type a4;
        typename dbus_traits<A5>::host_type a5;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(conn, msg, iter, a1);
        dbus_traits<A2>::get(conn, msg, iter, a2);
        dbus_traits<A3>::get(conn, msg, iter, a3);
        dbus_traits<A4>::get(conn, msg, iter, a4);
        dbus_traits<A5>::get(conn, msg, iter, a5);
        cb(a1, a2, a3, a4, a5);

        return TRUE;
    }

    void activate(const Callback_t &callback) { activateInternal(callback, internalCallback); }
};

template <typename A1, typename A2, typename A3, typename A4, typename A5, typename A6>
class SignalWatch6 : public SignalWatch< boost::function<void (const A1 &, const A2 &, const A3 &, const A4 &, const A5 &, const A6 &)> >
{
    typedef boost::function<void (const A1 &, const A2 &, const A3 &, const A4 &, const A5 &, const A6 &)> Callback_t;


 public:
    SignalWatch6(const DBusRemoteObject &object,
                 const std::string &signal)
        : SignalWatch<Callback_t>(object, signal)
    {
    }

    static gboolean internalCallback(DBusConnection *conn, DBusMessage *msg, void *data)
    {
        if (SignalWatch<Callback_t>::isMatched(msg, data) == FALSE) {
            return TRUE;
        }
        const Callback_t &cb = static_cast< SignalWatch<Callback_t> *>(data)->getCallback();

        typename dbus_traits<A1>::host_type a1;
        typename dbus_traits<A2>::host_type a2;
        typename dbus_traits<A3>::host_type a3;
        typename dbus_traits<A4>::host_type a4;
        typename dbus_traits<A5>::host_type a5;
        typename dbus_traits<A6>::host_type a6;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(conn, msg, iter, a1);
        dbus_traits<A2>::get(conn, msg, iter, a2);
        dbus_traits<A3>::get(conn, msg, iter, a3);
        dbus_traits<A4>::get(conn, msg, iter, a4);
        dbus_traits<A5>::get(conn, msg, iter, a5);
        dbus_traits<A6>::get(conn, msg, iter, a6);
        cb(a1, a2, a3, a4, a5, a6);

        return TRUE;
    }

    void activate(const Callback_t &callback) { activateInternal(callback, internalCallback); }
};

#endif // INCL_BDBUS_CXX_BRIDGE
