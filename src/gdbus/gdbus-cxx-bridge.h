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


#ifndef INCL_GDBUS_CXX_BRIDGE
#define INCL_GDBUS_CXX_BRIDGE

#include "gdbus.h"
#include "gdbus-cxx.h"

#include <map>
#include <vector>

#include <boost/bind.hpp>
#include <boost/intrusive_ptr.hpp>

namespace boost {
    void intrusive_ptr_add_ref(DBusConnection *con) { dbus_connection_ref(con); }
    void intrusive_ptr_release(DBusConnection *con) { dbus_connection_unref(con); }
    void intrusive_ptr_add_ref(DBusMessage *msg) { dbus_message_ref(msg); }
    void intrusive_ptr_release(DBusMessage *msg) { dbus_message_unref(msg); }
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

/**
 * wrapper around DBusError which initializes
 * the struct automatically, then can be used to
 * throw an exception
 */
class DBusErrorCXX : public DBusError
{
 public:
    DBusErrorCXX() { dbus_error_init(this); }
    void throwFailure(const std::string &operation)
    {
        if (dbus_error_is_set(this)) {
            throw std::runtime_error(operation + ": " + message);
        } else {
            throw std::runtime_error(operation + " failed");
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

    static GDBusSignalTable makeSignalEntry(const std::string &signal,
                                            GDBusSignalFlags flags = (GDBusSignalFlags)0)
    {
        GDBusSignalTable entry;
        entry.name = strdup(signal.c_str());
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
        DBusMessageIter iter;
        dbus_message_iter_init_append(msg.get(), &iter);
        dbus_traits<A1>::append_retval(iter, a1);

        if (!dbus_connection_send(m_object.getConnection(), msg.get(), NULL)) {
            throw std::runtime_error("dbus_connection_send failed");
        }
    }

    static GDBusSignalTable makeSignalEntry(const std::string &signal,
                                            GDBusSignalFlags flags = (GDBusSignalFlags)0)
    {
        GDBusSignalTable entry;
        entry.name = strdup(signal.c_str());
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
        DBusMessageIter iter;
        dbus_message_iter_init_append(msg.get(), &iter);
        dbus_traits<A1>::append_retval(iter, a1);
        dbus_traits<A2>::append_retval(iter, a2);

        if (!dbus_connection_send(m_object.getConnection(), msg.get(), NULL)) {
            throw std::runtime_error("dbus_connection_send failed");
        }
    }

    static GDBusSignalTable makeSignalEntry(const std::string &signal,
                                            GDBusSignalFlags flags = (GDBusSignalFlags)0)
    {
        GDBusSignalTable entry;
        entry.name = strdup(signal.c_str());
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
        DBusMessageIter iter;
        dbus_message_iter_init_append(msg.get(), &iter);
        dbus_traits<A1>::append_retval(iter, a1);
        dbus_traits<A2>::append_retval(iter, a2);
        dbus_traits<A3>::append_retval(iter, a3);

        if (!dbus_connection_send(m_object.getConnection(), msg.get(), NULL)) {
            throw std::runtime_error("dbus_connection_send failed");
        }
    }

    static GDBusSignalTable makeSignalEntry(const std::string &signal,
                                            GDBusSignalFlags flags = (GDBusSignalFlags)0)
    {
        GDBusSignalTable entry;
        entry.name = strdup(signal.c_str());
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
        DBusMessageIter iter;
        dbus_message_iter_init_append(msg.get(), &iter);
        dbus_traits<A1>::append_retval(iter, a1);
        dbus_traits<A2>::append_retval(iter, a2);
        dbus_traits<A3>::append_retval(iter, a3);
        dbus_traits<A4>::append_retval(iter, a4);

        if (!dbus_connection_send(m_object.getConnection(), msg.get(), NULL)) {
            throw std::runtime_error("dbus_connection_send failed");
        }
    }

    static GDBusSignalTable makeSignalEntry(const std::string &signal,
                                            GDBusSignalFlags flags = (GDBusSignalFlags)0)
    {
        GDBusSignalTable entry;
        entry.name = strdup(signal.c_str());
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
        DBusMessageIter iter;
        dbus_message_iter_init_append(msg.get(), &iter);
        dbus_traits<A1>::append_retval(iter, a1);
        dbus_traits<A2>::append_retval(iter, a2);
        dbus_traits<A3>::append_retval(iter, a3);
        dbus_traits<A4>::append_retval(iter, a4);
        dbus_traits<A5>::append_retval(iter, a5);

        if (!dbus_connection_send(m_object.getConnection(), msg.get(), NULL)) {
            throw std::runtime_error("dbus_connection_send failed");
        }
    }

    static GDBusSignalTable makeSignalEntry(const std::string &signal,
                                            GDBusSignalFlags flags = (GDBusSignalFlags)0)
    {
        GDBusSignalTable entry;
        entry.name = strdup(signal.c_str());
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

/**
 * utility class for registering an interface
 */
class DBusObjectHelper : public DBusObject
{
    DBusConnectionPtr m_conn;
    std::string m_path;
    std::string m_interface;
    bool m_activated;

 public:
    DBusObjectHelper(DBusConnection *conn,
                     const std::string &path,
                     const std::string &interface) :
        m_conn(conn),
        m_path(path),
        m_interface(interface),
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

    void activate(GDBusMethodTable *methods,
                  GDBusSignalTable *signals,
                  GDBusPropertyTable *properties,
                  void *user_data) {
        if (!g_dbus_register_interface(getConnection(), getPath(), getInterface(),
                                       methods, signals, properties, user_data, NULL)) {
            throw std::runtime_error(std::string("g_dbus_register_interface() failed for ") + getPath() + " " + getInterface());
        }
        m_activated = true;
    }

    void deactivate()
    {
        if (m_activated) {
            if (!g_dbus_unregister_interface(getConnection(),
                                             getPath(),
                                             getInterface())) {
                throw std::runtime_error(std::string("g_dbus_unregister_interface() failed for ") + getPath() + " " + getInterface());
            }
            m_activated = false;
        }
    }
};


/**
 * to be used for plain parameters like int32_t:
 * treat as arguments which have to be extracted
 * from the D-Bus message and can be skipped when
 * encoding the reply
 */
template<class host, int dbus> struct basic_marshal
{
    /**
     * copy value from D-Bus iterator into variable
     */
    static void get(DBusMessageIter &iter, host &value)
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
    static void append(DBusMessageIter &iter, const host &value)
    {
        // nothing to do
    }

    /**
     * utility function to be used by derived classes which
     * need to copy a variable of this underlying type
     */
    static void append_retval(DBusMessageIter &iter, const host &value)
    {
        if (!dbus_message_iter_append_basic(&iter, dbus, &value)) {
            throw std::runtime_error("out of memory");
        }
    }

    typedef host host_type;
    typedef host arg_type;
};

template<> struct dbus_traits<int8_t> :
    public basic_marshal< int8_t, DBUS_TYPE_BYTE >
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

template<> struct dbus_traits<bool>
{
    static std::string getType() { return "b"; }
    static std::string getSignature() {return getType(); }
    static std::string getReply() { return ""; }
    static const int dbus = DBUS_TYPE_BOOLEAN;

    static void get(DBusMessageIter &iter, bool &value)
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

template<> struct dbus_traits<std::string>
{
    static std::string getType() { return "s"; }
    static std::string getSignature() {return getType(); }
    static std::string getReply() { return ""; }
    static const int dbus = DBUS_TYPE_STRING;

    static void get(DBusMessageIter &iter, std::string &value)
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

template <> struct dbus_traits<DBusObject_t>
{
    static std::string getType() { return "o"; }
    static std::string getSignature() {return getType(); }
    static std::string getReply() { return ""; }
    static const int dbus = DBUS_TYPE_OBJECT_PATH;

    static void get(DBusMessageIter &iter, DBusObject_t &value)
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
 * a std::map - treat it like a D-Bus dict
 */
template<class K, class V> struct dbus_traits< std::map<K, V> >
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

    static void get(DBusMessageIter &iter, host_type &dict)
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
            dbus_traits<K>::get(entry, key);
            dbus_traits<V>::get(entry, value);
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
template<class V> struct dbus_traits< std::vector<V> >
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

    static void get(DBusMessageIter &iter, host_type &array)
    {
        if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) {
            throw std::runtime_error("invalid argument");
        }
        DBusMessageIter sub;
        dbus_message_iter_recurse(&iter, &sub);
        while (dbus_message_iter_get_arg_type(&sub) != DBUS_TYPE_INVALID) {
            V value;
            dbus_traits<V>::get(sub, value);
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
    static void get(DBusMessageIter &iter, C &value) {}
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

static DBusMessage *handleException(DBusMessage *msg)
{
    try {
        throw;
    } catch (const dbus_error &ex) {
        return g_dbus_create_error(msg, ex.dbusName().c_str(), "%s", ex.what());
    } catch (const std::runtime_error &ex) {
        return g_dbus_create_error(msg, "org.syncevolution.Exception", "%s", ex.what());
    } catch (...) {
        return g_dbus_create_error(msg, "org.syncevolution.Exception", "unknown");
    }
}

/**
 * Check presence of a certain D-Bus client.
 */
class DBusWatch : public Watch
{
    const DBusConnectionPtr &m_conn;
    boost::function<void (void)> m_callback;
    bool m_called;
    guint m_watchID;

    static void disconnect(DBusConnection *connection,
                           void *user_data)
    {
        DBusWatch *watch = static_cast<DBusWatch *>(user_data);
        if (!watch->m_called) {
            watch->m_called = true;
            watch->m_callback();
        }
    }

 public:
    DBusWatch(const DBusConnectionPtr &conn,
              const boost::function<void (void)> &callback) :
        m_conn(conn),
        m_callback(callback),
        m_called(false),
        m_watchID(0)
    {
    }

    void activate(const char *peer)
    {
        if (!peer) {
            throw std::runtime_error("DBusWatch::activate(): no peer");
        }

        // Install watch first ...
        m_watchID = g_dbus_add_disconnect_watch(m_conn.get(),
                                                peer,
                                                disconnect,
                                                this,
                                                NULL);
        if (!m_watchID) {
            throw std::runtime_error("g_dbus_add_disconnect_watch() failed");
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
            if (!g_dbus_remove_watch(m_conn.get(), m_watchID)) {
                // this may happen because the watch is
                // removed automatically when it was triggered
            }
            m_watchID = 0;
        }
    }
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
        if (!g_dbus_send_error(m_conn.get(), m_msg.get(),
                               error.dbusName().c_str(),
                               "%s", error.what())) {
            throw std::runtime_error("g_dbus_send_error() failed");
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
        DBusMessagePtr reply(g_dbus_create_reply(m_msg.get(), DBUS_TYPE_INVALID));
        if (!reply) {
            throw std::runtime_error("no DBusMessage");
        }

        if (!dbus_connection_send(m_conn.get(), reply.get(), NULL)) {
            throw std::runtime_error("dbus_connection_send failed");
        }
    }
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
        DBusMessagePtr reply(g_dbus_create_reply(m_msg.get(), DBUS_TYPE_INVALID));
        if (!reply) {
            throw std::runtime_error("no DBusMessage");
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply.get(), &iter);
        dbus_traits<A1>::append_retval(iter, a1);

        if (!dbus_connection_send(m_conn.get(), reply.get(), NULL)) {
            throw std::runtime_error("dbus_connection_send failed");
        }
    }
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
        DBusMessagePtr reply(g_dbus_create_reply(m_msg.get(), DBUS_TYPE_INVALID));
        if (!reply) {
            throw std::runtime_error("no DBusMessage");
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply.get(), &iter);
        dbus_traits<A1>::append_retval(iter, a1);
        dbus_traits<A2>::append_retval(iter, a2);

        if (!dbus_connection_send(m_conn.get(), reply.get(), NULL)) {
            throw std::runtime_error("dbus_connection_send failed");
        }
    }
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
        DBusMessagePtr reply(g_dbus_create_reply(m_msg.get(), DBUS_TYPE_INVALID));
        if (!reply) {
            throw std::runtime_error("no DBusMessage");
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply.get(), &iter);
        dbus_traits<A1>::append_retval(iter, a1);
        dbus_traits<A2>::append_retval(iter, a2);
        dbus_traits<A3>::append_retval(iter, a3);

        if (!dbus_connection_send(m_conn.get(), reply.get(), NULL)) {
            throw std::runtime_error("dbus_connection_send failed");
        }
    }
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
        DBusMessagePtr reply(g_dbus_create_reply(m_msg.get(), DBUS_TYPE_INVALID));
        if (!reply) {
            throw std::runtime_error("no DBusMessage");
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply.get(), &iter);
        dbus_traits<A1>::append_retval(iter, a1);
        dbus_traits<A2>::append_retval(iter, a2);
        dbus_traits<A3>::append_retval(iter, a3);
        dbus_traits<A4>::append_retval(iter, a4);

        if (!dbus_connection_send(m_conn.get(), reply.get(), NULL)) {
            throw std::runtime_error("dbus_connection_send failed");
        }
    }
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
        DBusMessagePtr reply(g_dbus_create_reply(m_msg.get(), DBUS_TYPE_INVALID));
        if (!reply) {
            throw std::runtime_error("no DBusMessage");
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply.get(), &iter);
        dbus_traits<A1>::append_retval(iter, a1);
        dbus_traits<A2>::append_retval(iter, a2);
        dbus_traits<A3>::append_retval(iter, a3);
        dbus_traits<A4>::append_retval(iter, a4);
        dbus_traits<A5>::append_retval(iter, a5);

        if (!dbus_connection_send(m_conn.get(), reply.get(), NULL)) {
            throw std::runtime_error("dbus_connection_send failed");
        }
    }
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
        DBusMessagePtr reply(g_dbus_create_reply(m_msg.get(), DBUS_TYPE_INVALID));
        if (!reply) {
            throw std::runtime_error("no DBusMessage");
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply.get(), &iter);
        dbus_traits<A1>::append_retval(iter, a1);
        dbus_traits<A2>::append_retval(iter, a2);
        dbus_traits<A3>::append_retval(iter, a3);
        dbus_traits<A4>::append_retval(iter, a4);
        dbus_traits<A5>::append_retval(iter, a5);
        dbus_traits<A6>::append_retval(iter, a6);

        if (!dbus_connection_send(m_conn.get(), reply.get(), NULL)) {
            throw std::runtime_error("dbus_connection_send failed");
        }
    }
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
        DBusMessagePtr reply(g_dbus_create_reply(m_msg.get(), DBUS_TYPE_INVALID));
        if (!reply) {
            throw std::runtime_error("no DBusMessage");
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply.get(), &iter);
        dbus_traits<A1>::append_retval(iter, a1);
        dbus_traits<A2>::append_retval(iter, a2);
        dbus_traits<A3>::append_retval(iter, a3);
        dbus_traits<A4>::append_retval(iter, a4);
        dbus_traits<A5>::append_retval(iter, a5);
        dbus_traits<A6>::append_retval(iter, a6);
        dbus_traits<A7>::append_retval(iter, a7);

        if (!dbus_connection_send(m_conn.get(), reply.get(), NULL)) {
            throw std::runtime_error("dbus_connection_send failed");
        }
    }
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
        DBusMessagePtr reply(g_dbus_create_reply(m_msg.get(), DBUS_TYPE_INVALID));
        if (!reply) {
            throw std::runtime_error("no DBusMessage");
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply.get(), &iter);
        dbus_traits<A1>::append_retval(iter, a1);
        dbus_traits<A2>::append_retval(iter, a2);
        dbus_traits<A3>::append_retval(iter, a3);
        dbus_traits<A4>::append_retval(iter, a4);
        dbus_traits<A5>::append_retval(iter, a5);
        dbus_traits<A6>::append_retval(iter, a6);
        dbus_traits<A7>::append_retval(iter, a7);
        dbus_traits<A8>::append_retval(iter, a8);

        if (!dbus_connection_send(m_conn.get(), reply.get(), NULL)) {
            throw std::runtime_error("dbus_connection_send failed");
        }
    }
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
        DBusMessagePtr reply(g_dbus_create_reply(m_msg.get(), DBUS_TYPE_INVALID));
        if (!reply) {
            throw std::runtime_error("no DBusMessage");
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply.get(), &iter);
        dbus_traits<A1>::append_retval(iter, a1);
        dbus_traits<A2>::append_retval(iter, a2);
        dbus_traits<A3>::append_retval(iter, a3);
        dbus_traits<A4>::append_retval(iter, a4);
        dbus_traits<A5>::append_retval(iter, a5);
        dbus_traits<A6>::append_retval(iter, a6);
        dbus_traits<A7>::append_retval(iter, a7);
        dbus_traits<A8>::append_retval(iter, a8);
        dbus_traits<A9>::append_retval(iter, a9);

        if (!dbus_connection_send(m_conn.get(), reply.get(), NULL)) {
            throw std::runtime_error("dbus_connection_send failed");
        }
    }
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
        DBusMessagePtr reply(g_dbus_create_reply(m_msg.get(), DBUS_TYPE_INVALID));
        if (!reply) {
            throw std::runtime_error("no DBusMessage");
        }
        DBusMessageIter iter;
        dbus_message_iter_init_append(reply.get(), &iter);
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

        if (!dbus_connection_send(m_conn.get(), reply.get(), NULL)) {
            throw std::runtime_error("dbus_connection_send failed");
        }
    }
};

#if 0
/**
 * Method call with two parameters and one return code. All other
 * calls are variations of this, so this one is fully documented
 * to explain all tricks used in these templates. The actual
 * code without comments is below.
 */
template <class I, class R, class A1, class A2, R (I::*m)(A1, A2)>
DBusMessage *methodFunction(DBusConnection *conn,
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
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);

        // The data pointer given to g_dbus_register_interface()
        // must have been a pointer to an object of our interface
        // class. m is a method pointer passed in as template parameter.
        // Combining the two allows us to call the right method.
        // The compiler knows the exact method prototype and thus
        // can handle call-by-value and call-by-reference correctly.
        r = (static_cast<I *>(data)->*m)(a1, a2);

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
 * Creates a GDBusMethodTable entry.
 * The strings inside the entry are allocated
 * with strdup(). For technical reasons the return type
 * and parameter types have to be listed explicitly.
 * The name passed to the function is the D-Bus method name.
 * Valid flags are G_DBUS_METHOD_FLAG_DEPRECATED,
 * G_DBUS_METHOD_FLAG_NOREPLY (no reply message is sent),
 * G_DBUS_METHOD_FLAG_ASYNC (reply will be sent via
 * callback, method must accept Result parameter).
 */
template <class I, class R1, class A1, class A2, R1 (I::*m)(A1, A2)>
GDBusMethodTable makeMethodEntry(const char *name, GDBusMethodFlags flags = GDBusMethodFlags(0))
{
    GDBusMethodTable entry;
    entry.name = strdup(name);
    // same trick as before: only argument types
    // are added to the signature
    std::string buffer;
    buffer += dbus_traits<A1>::getSignature();
    buffer += dbus_traits<A2>::getSignature();
    entry.signature = strdup(buffer.c_str());
    // now the same for reply types
    buffer.clear();
    buffer += dbus_traits<A1>::getReply();
    buffer += dbus_traits<A2>::getReply();
    entry.reply = strdup(buffer.c_str());
    // this is the function template above
    entry.function = methodFunction<I, A1, A2, m>;
    entry.flags = flags;
    return entry;
}
#endif // 0

/** ===> 10 parameters */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8, class A9, class A10,
          void (I::*m)(A1, A2, A3, A4, A5, A6, A7, A8, A9, A10)>
DBusMessage *methodFunction(DBusConnection *conn,
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
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
        dbus_traits<A4>::get(iter, a4);
        dbus_traits<A5>::get(iter, a5);
        dbus_traits<A6>::get(iter, a6);
        dbus_traits<A7>::get(iter, a7);
        dbus_traits<A8>::get(iter, a8);
        dbus_traits<A9>::get(iter, a9);
        dbus_traits<A10>::get(iter, a10);

        (static_cast<I *>(data)->*m)(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10);

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

/** 9 arguments, 1 return value */
template <class I, class R,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8, class A9,
          R (I::*m)(A1, A2, A3, A4, A5, A6, A7, A8, A9)>
DBusMessage *methodFunction(DBusConnection *conn,
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
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
        dbus_traits<A4>::get(iter, a4);
        dbus_traits<A5>::get(iter, a5);
        dbus_traits<A6>::get(iter, a6);
        dbus_traits<A7>::get(iter, a7);
        dbus_traits<A8>::get(iter, a8);
        dbus_traits<A9>::get(iter, a9);

        r = (static_cast<I *>(data)->*m)(a1, a2, a3, a4, a5, a6, a7, a8, a9);

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

/** 10 arguments, 0 asynchronous result */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8, class A9, class A10,
          void (I::*m)(A1, A2, A3, A4, A5, A6, A7, A8, A9,
                       Result0 *)>
DBusMessage *methodFunction(DBusConnection *conn,
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
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
        dbus_traits<A4>::get(iter, a4);
        dbus_traits<A5>::get(iter, a5);
        dbus_traits<A6>::get(iter, a6);
        dbus_traits<A7>::get(iter, a7);
        dbus_traits<A8>::get(iter, a8);
        dbus_traits<A9>::get(iter, a9);
        dbus_traits<A10>::get(iter, a10);

        (static_cast<I *>(data)->*m)(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10,
                                     new DBusResult0(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 9 arguments, 1 asynchronous result */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8, class A9, class A10,
          void (I::*m)(A1, A2, A3, A4, A5, A6, A7, A8, A9,
                       Result1<typename dbus_traits<A10>::arg_type> *)>
DBusMessage *methodFunction(DBusConnection *conn,
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
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
        dbus_traits<A4>::get(iter, a4);
        dbus_traits<A5>::get(iter, a5);
        dbus_traits<A6>::get(iter, a6);
        dbus_traits<A7>::get(iter, a7);
        dbus_traits<A8>::get(iter, a8);
        dbus_traits<A9>::get(iter, a9);

        (static_cast<I *>(data)->*m)(a1, a2, a3, a4, a5, a6, a7, a8, a9,
                                     new DBusResult1<typename dbus_traits<A10>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 8 arguments, 2 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8, class A9, class A10,
          void (I::*m)(A1, A2, A3, A4, A5, A6, A7, A8,
                       Result2<typename dbus_traits<A9>::arg_type,
                               typename dbus_traits<A10>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
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
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
        dbus_traits<A4>::get(iter, a4);
        dbus_traits<A5>::get(iter, a5);
        dbus_traits<A6>::get(iter, a6);
        dbus_traits<A7>::get(iter, a7);
        dbus_traits<A8>::get(iter, a8);

        (static_cast<I *>(data)->*m)(a1, a2, a3, a4, a5, a6, a7, a8,
                                     new DBusResult2<typename dbus_traits<A9>::arg_type,
                                                     typename dbus_traits<A10>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 7 arguments, 3 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8, class A9, class A10,
          void (I::*m)(A1, A2, A3, A4, A5, A6, A7,
                       Result3<typename dbus_traits<A8>::arg_type,
                               typename dbus_traits<A9>::arg_type,
                               typename dbus_traits<A10>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
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
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
        dbus_traits<A4>::get(iter, a4);
        dbus_traits<A5>::get(iter, a5);
        dbus_traits<A6>::get(iter, a6);
        dbus_traits<A7>::get(iter, a7);

        (static_cast<I *>(data)->*m)(a1, a2, a3, a4, a5, a6, a7,
                                     new DBusResult3<typename dbus_traits<A8>::arg_type,
                                                     typename dbus_traits<A9>::arg_type,
                                                     typename dbus_traits<A10>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 6 arguments, 4 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8, class A9, class A10,
          void (I::*m)(A1, A2, A3, A4, A5, A6,
                       Result4<typename dbus_traits<A7>::arg_type,
                               typename dbus_traits<A8>::arg_type,
                               typename dbus_traits<A9>::arg_type,
                               typename dbus_traits<A10>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
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
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
        dbus_traits<A4>::get(iter, a4);
        dbus_traits<A5>::get(iter, a5);
        dbus_traits<A6>::get(iter, a6);

        (static_cast<I *>(data)->*m)(a1, a2, a3, a4, a5, a6,
                                     new DBusResult4<typename dbus_traits<A7>::arg_type,
                                                     typename dbus_traits<A8>::arg_type,
                                                     typename dbus_traits<A9>::arg_type,
                                                     typename dbus_traits<A10>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 5 arguments, 5 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8, class A9, class A10,
          void (I::*m)(A1, A2, A3, A4, A5,
                       Result5<typename dbus_traits<A6>::arg_type,
                               typename dbus_traits<A7>::arg_type,
                               typename dbus_traits<A8>::arg_type,
                               typename dbus_traits<A9>::arg_type,
                               typename dbus_traits<A10>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
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
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
        dbus_traits<A4>::get(iter, a4);
        dbus_traits<A5>::get(iter, a5);

        (static_cast<I *>(data)->*m)(a1, a2, a3, a4, a5,
                                     new DBusResult5<typename dbus_traits<A6>::arg_type,
                                                     typename dbus_traits<A7>::arg_type,
                                                     typename dbus_traits<A8>::arg_type,
                                                     typename dbus_traits<A9>::arg_type,
                                                     typename dbus_traits<A10>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 4 arguments, 6 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8, class A9, class A10,
          void (I::*m)(A1, A2, A3, A4,
                       Result6<typename dbus_traits<A5>::arg_type,
                               typename dbus_traits<A6>::arg_type,
                               typename dbus_traits<A7>::arg_type,
                               typename dbus_traits<A8>::arg_type,
                               typename dbus_traits<A9>::arg_type,
                               typename dbus_traits<A10>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<A1>::host_type a1;
        typename dbus_traits<A2>::host_type a2;
        typename dbus_traits<A3>::host_type a3;
        typename dbus_traits<A4>::host_type a4;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
        dbus_traits<A4>::get(iter, a4);

        (static_cast<I *>(data)->*m)(a1, a2, a3, a4,
                                     new DBusResult6<typename dbus_traits<A5>::arg_type,
                                                     typename dbus_traits<A6>::arg_type,
                                                     typename dbus_traits<A7>::arg_type,
                                                     typename dbus_traits<A8>::arg_type,
                                                     typename dbus_traits<A9>::arg_type,
                                                     typename dbus_traits<A10>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 3 arguments, 7 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8, class A9, class A10,
          void (I::*m)(A1, A2, A3,
                       Result7<typename dbus_traits<A4>::arg_type,
                               typename dbus_traits<A5>::arg_type,
                               typename dbus_traits<A6>::arg_type,
                               typename dbus_traits<A7>::arg_type,
                               typename dbus_traits<A8>::arg_type,
                               typename dbus_traits<A9>::arg_type,
                               typename dbus_traits<A10>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<A1>::host_type a1;
        typename dbus_traits<A2>::host_type a2;
        typename dbus_traits<A3>::host_type a3;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);

        (static_cast<I *>(data)->*m)(a1, a2, a3,
                                     new DBusResult7<typename dbus_traits<A4>::arg_type,
                                                     typename dbus_traits<A5>::arg_type,
                                                     typename dbus_traits<A6>::arg_type,
                                                     typename dbus_traits<A7>::arg_type,
                                                     typename dbus_traits<A8>::arg_type,
                                                     typename dbus_traits<A9>::arg_type,
                                                     typename dbus_traits<A10>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 2 arguments, 8 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8, class A9, class A10,
          void (I::*m)(A1, A2,
                       Result8<typename dbus_traits<A3>::arg_type,
                               typename dbus_traits<A4>::arg_type,
                               typename dbus_traits<A5>::arg_type,
                               typename dbus_traits<A6>::arg_type,
                               typename dbus_traits<A7>::arg_type,
                               typename dbus_traits<A8>::arg_type,
                               typename dbus_traits<A9>::arg_type,
                               typename dbus_traits<A10>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<A1>::host_type a1;
        typename dbus_traits<A2>::host_type a2;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);

        (static_cast<I *>(data)->*m)(a1, a2,
                                     new DBusResult8<typename dbus_traits<A3>::arg_type,
                                                     typename dbus_traits<A4>::arg_type,
                                                     typename dbus_traits<A5>::arg_type,
                                                     typename dbus_traits<A6>::arg_type,
                                                     typename dbus_traits<A7>::arg_type,
                                                     typename dbus_traits<A8>::arg_type,
                                                     typename dbus_traits<A9>::arg_type,
                                                     typename dbus_traits<A10>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 1 arguments, 9 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8, class A9, class A10,
          void (I::*m)(A1,
                       Result9<typename dbus_traits<A2>::arg_type,
                               typename dbus_traits<A3>::arg_type,
                               typename dbus_traits<A4>::arg_type,
                               typename dbus_traits<A5>::arg_type,
                               typename dbus_traits<A6>::arg_type,
                               typename dbus_traits<A7>::arg_type,
                               typename dbus_traits<A8>::arg_type,
                               typename dbus_traits<A9>::arg_type,
                               typename dbus_traits<A10>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<A1>::host_type a1;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(iter, a1);

        (static_cast<I *>(data)->*m)(a1,
                                     new DBusResult9<typename dbus_traits<A2>::arg_type,
                                                     typename dbus_traits<A3>::arg_type,
                                                     typename dbus_traits<A4>::arg_type,
                                                     typename dbus_traits<A5>::arg_type,
                                                     typename dbus_traits<A6>::arg_type,
                                                     typename dbus_traits<A7>::arg_type,
                                                     typename dbus_traits<A8>::arg_type,
                                                     typename dbus_traits<A9>::arg_type,
                                                     typename dbus_traits<A10>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 0 arguments, 10 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8, class A9, class A10,
          void (I::*m)(Result10<typename dbus_traits<A1>::arg_type,
                                typename dbus_traits<A2>::arg_type,
                                typename dbus_traits<A3>::arg_type,
                                typename dbus_traits<A4>::arg_type,
                                typename dbus_traits<A5>::arg_type,
                                typename dbus_traits<A6>::arg_type,
                                typename dbus_traits<A7>::arg_type,
                                typename dbus_traits<A8>::arg_type,
                                typename dbus_traits<A9>::arg_type,
                                typename dbus_traits<A10>::arg_type>
                                *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        (static_cast<I *>(data)->*m)(new DBusResult10<typename dbus_traits<A1>::arg_type,
                                                      typename dbus_traits<A2>::arg_type,
                                                      typename dbus_traits<A3>::arg_type,
                                                      typename dbus_traits<A4>::arg_type,
                                                      typename dbus_traits<A5>::arg_type,
                                                      typename dbus_traits<A6>::arg_type,
                                                      typename dbus_traits<A7>::arg_type,
                                                      typename dbus_traits<A8>::arg_type,
                                                      typename dbus_traits<A9>::arg_type,
                                                      typename dbus_traits<A10>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** ===> 9 parameters */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8, class A9,
          void (I::*m)(A1, A2, A3, A4, A5, A6, A7, A8, A9)>
DBusMessage *methodFunction(DBusConnection *conn,
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
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
        dbus_traits<A4>::get(iter, a4);
        dbus_traits<A5>::get(iter, a5);
        dbus_traits<A6>::get(iter, a6);
        dbus_traits<A7>::get(iter, a7);
        dbus_traits<A8>::get(iter, a8);
        dbus_traits<A9>::get(iter, a9);

        (static_cast<I *>(data)->*m)(a1, a2, a3, a4, a5, a6, a7, a8, a9);

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

/** 8 arguments, 1 return value */
template <class I, class R,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8,
          R (I::*m)(A1, A2, A3, A4, A5, A6, A7, A8)>
DBusMessage *methodFunction(DBusConnection *conn,
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
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
        dbus_traits<A4>::get(iter, a4);
        dbus_traits<A5>::get(iter, a5);
        dbus_traits<A6>::get(iter, a6);
        dbus_traits<A7>::get(iter, a7);
        dbus_traits<A8>::get(iter, a8);

        r = (static_cast<I *>(data)->*m)(a1, a2, a3, a4, a5, a6, a7, a8);

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

/** 9 arguments, 0 asynchronous result */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8, class A9,
          void (I::*m)(A1, A2, A3, A4, A5, A6, A7, A8, A9,
                       Result0 *)>
DBusMessage *methodFunction(DBusConnection *conn,
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
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
        dbus_traits<A4>::get(iter, a4);
        dbus_traits<A5>::get(iter, a5);
        dbus_traits<A6>::get(iter, a6);
        dbus_traits<A7>::get(iter, a7);
        dbus_traits<A8>::get(iter, a8);
        dbus_traits<A9>::get(iter, a9);

        (static_cast<I *>(data)->*m)(a1, a2, a3, a4, a5, a6, a7, a8, a9,
                                     new DBusResult0(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 8 arguments, 1 asynchronous result */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8, class A9,
          void (I::*m)(A1, A2, A3, A4, A5, A6, A7, A8,
                       Result1<typename dbus_traits<A9>::arg_type> *)>
DBusMessage *methodFunction(DBusConnection *conn,
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
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
        dbus_traits<A4>::get(iter, a4);
        dbus_traits<A5>::get(iter, a5);
        dbus_traits<A6>::get(iter, a6);
        dbus_traits<A7>::get(iter, a7);
        dbus_traits<A8>::get(iter, a8);

        (static_cast<I *>(data)->*m)(a1, a2, a3, a4, a5, a6, a7, a8,
                                     new DBusResult1<typename dbus_traits<A9>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 7 arguments, 2 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8, class A9,
          void (I::*m)(A1, A2, A3, A4, A5, A6, A7,
                       Result2<typename dbus_traits<A8>::arg_type,
                               typename dbus_traits<A9>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
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
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
        dbus_traits<A4>::get(iter, a4);
        dbus_traits<A5>::get(iter, a5);
        dbus_traits<A6>::get(iter, a6);
        dbus_traits<A7>::get(iter, a7);

        (static_cast<I *>(data)->*m)(a1, a2, a3, a4, a5, a6, a7,
                                     new DBusResult2<typename dbus_traits<A8>::arg_type,
                                                     typename dbus_traits<A9>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 7 arguments, 3 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8, class A9,
          void (I::*m)(A1, A2, A3, A4, A5, A6,
                       Result3<typename dbus_traits<A7>::arg_type,
                               typename dbus_traits<A8>::arg_type,
                               typename dbus_traits<A9>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
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
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
        dbus_traits<A4>::get(iter, a4);
        dbus_traits<A5>::get(iter, a5);
        dbus_traits<A6>::get(iter, a6);

        (static_cast<I *>(data)->*m)(a1, a2, a3, a4, a5, a6,
                                     new DBusResult3<typename dbus_traits<A7>::arg_type,
                                                     typename dbus_traits<A8>::arg_type,
                                                     typename dbus_traits<A9>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 5 arguments, 4 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8, class A9,
          void (I::*m)(A1, A2, A3, A4, A5,
                       Result4<typename dbus_traits<A6>::arg_type,
                               typename dbus_traits<A7>::arg_type,
                               typename dbus_traits<A8>::arg_type,
                               typename dbus_traits<A9>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
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
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
        dbus_traits<A4>::get(iter, a4);
        dbus_traits<A5>::get(iter, a5);

        (static_cast<I *>(data)->*m)(a1, a2, a3, a4, a5,
                                     new DBusResult4<typename dbus_traits<A6>::arg_type,
                                                     typename dbus_traits<A7>::arg_type,
                                                     typename dbus_traits<A8>::arg_type,
                                                     typename dbus_traits<A9>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 4 arguments, 5 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8, class A9,
          void (I::*m)(A1, A2, A3, A4,
                       Result5<typename dbus_traits<A5>::arg_type,
                               typename dbus_traits<A6>::arg_type,
                               typename dbus_traits<A7>::arg_type,
                               typename dbus_traits<A8>::arg_type,
                               typename dbus_traits<A9>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<A1>::host_type a1;
        typename dbus_traits<A2>::host_type a2;
        typename dbus_traits<A3>::host_type a3;
        typename dbus_traits<A4>::host_type a4;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
        dbus_traits<A4>::get(iter, a4);

        (static_cast<I *>(data)->*m)(a1, a2, a3, a4,
                                     new DBusResult5<typename dbus_traits<A5>::arg_type,
                                                     typename dbus_traits<A6>::arg_type,
                                                     typename dbus_traits<A7>::arg_type,
                                                     typename dbus_traits<A8>::arg_type,
                                                     typename dbus_traits<A9>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 3 arguments, 6 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8, class A9,
          void (I::*m)(A1, A2, A3,
                       Result6<typename dbus_traits<A4>::arg_type,
                               typename dbus_traits<A5>::arg_type,
                               typename dbus_traits<A6>::arg_type,
                               typename dbus_traits<A7>::arg_type,
                               typename dbus_traits<A8>::arg_type,
                               typename dbus_traits<A9>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<A1>::host_type a1;
        typename dbus_traits<A2>::host_type a2;
        typename dbus_traits<A3>::host_type a3;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);

        (static_cast<I *>(data)->*m)(a1, a2, a3,
                                     new DBusResult6<typename dbus_traits<A4>::arg_type,
                                                     typename dbus_traits<A5>::arg_type,
                                                     typename dbus_traits<A6>::arg_type,
                                                     typename dbus_traits<A7>::arg_type,
                                                     typename dbus_traits<A8>::arg_type,
                                                     typename dbus_traits<A9>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 2 arguments, 7 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8, class A9,
          void (I::*m)(A1, A2,
                       Result7<typename dbus_traits<A3>::arg_type,
                               typename dbus_traits<A4>::arg_type,
                               typename dbus_traits<A5>::arg_type,
                               typename dbus_traits<A6>::arg_type,
                               typename dbus_traits<A7>::arg_type,
                               typename dbus_traits<A8>::arg_type,
                               typename dbus_traits<A9>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<A1>::host_type a1;
        typename dbus_traits<A2>::host_type a2;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);

        (static_cast<I *>(data)->*m)(a1, a2,
                                     new DBusResult7<typename dbus_traits<A3>::arg_type,
                                                     typename dbus_traits<A4>::arg_type,
                                                     typename dbus_traits<A5>::arg_type,
                                                     typename dbus_traits<A6>::arg_type,
                                                     typename dbus_traits<A7>::arg_type,
                                                     typename dbus_traits<A8>::arg_type,
                                                     typename dbus_traits<A9>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 1 argument, 8 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8, class A9,
          void (I::*m)(A1,
                       Result8<typename dbus_traits<A2>::arg_type,
                               typename dbus_traits<A3>::arg_type,
                               typename dbus_traits<A4>::arg_type,
                               typename dbus_traits<A5>::arg_type,
                               typename dbus_traits<A6>::arg_type,
                               typename dbus_traits<A7>::arg_type,
                               typename dbus_traits<A8>::arg_type,
                               typename dbus_traits<A9>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<A1>::host_type a1;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(iter, a1);

        (static_cast<I *>(data)->*m)(a1,
                                     new DBusResult8<typename dbus_traits<A2>::arg_type,
                                                     typename dbus_traits<A3>::arg_type,
                                                     typename dbus_traits<A4>::arg_type,
                                                     typename dbus_traits<A5>::arg_type,
                                                     typename dbus_traits<A6>::arg_type,
                                                     typename dbus_traits<A7>::arg_type,
                                                     typename dbus_traits<A8>::arg_type,
                                                     typename dbus_traits<A9>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 0 argument, 9 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8, class A9,
          void (I::*m)(Result9<typename dbus_traits<A1>::arg_type,
                               typename dbus_traits<A2>::arg_type,
                               typename dbus_traits<A3>::arg_type,
                               typename dbus_traits<A4>::arg_type,
                               typename dbus_traits<A5>::arg_type,
                               typename dbus_traits<A6>::arg_type,
                               typename dbus_traits<A7>::arg_type,
                               typename dbus_traits<A8>::arg_type,
                               typename dbus_traits<A9>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        (static_cast<I *>(data)->*m)(new DBusResult9<typename dbus_traits<A1>::arg_type,
                                                     typename dbus_traits<A2>::arg_type,
                                                     typename dbus_traits<A3>::arg_type,
                                                     typename dbus_traits<A4>::arg_type,
                                                     typename dbus_traits<A5>::arg_type,
                                                     typename dbus_traits<A6>::arg_type,
                                                     typename dbus_traits<A7>::arg_type,
                                                     typename dbus_traits<A8>::arg_type,
                                                     typename dbus_traits<A9>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** ===> 8 parameters */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8,
          void (I::*m)(A1, A2, A3, A4, A5, A6, A7, A8)>
DBusMessage *methodFunction(DBusConnection *conn,
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
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
        dbus_traits<A4>::get(iter, a4);
        dbus_traits<A5>::get(iter, a5);
        dbus_traits<A6>::get(iter, a6);
        dbus_traits<A7>::get(iter, a7);
        dbus_traits<A8>::get(iter, a8);

        (static_cast<I *>(data)->*m)(a1, a2, a3, a4, a5, a6, a7, a8);

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

/** 7 arguments, 1 return value */
template <class I, class R,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7,
          R (I::*m)(A1, A2, A3, A4, A5, A6, A7)>
DBusMessage *methodFunction(DBusConnection *conn,
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
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
        dbus_traits<A4>::get(iter, a4);
        dbus_traits<A5>::get(iter, a5);
        dbus_traits<A6>::get(iter, a6);
        dbus_traits<A7>::get(iter, a7);

        r = (static_cast<I *>(data)->*m)(a1, a2, a3, a4, a5, a6, a7);

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

/** 8 arguments, 0 asynchronous result */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8,
          void (I::*m)(A1, A2, A3, A4, A5, A6, A7, A8,
                       Result0 *)>
DBusMessage *methodFunction(DBusConnection *conn,
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
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
        dbus_traits<A4>::get(iter, a4);
        dbus_traits<A5>::get(iter, a5);
        dbus_traits<A6>::get(iter, a6);
        dbus_traits<A7>::get(iter, a7);
        dbus_traits<A8>::get(iter, a8);

        (static_cast<I *>(data)->*m)(a1, a2, a3, a4, a5, a6, a7, a8,
                                     new DBusResult0(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 7 arguments, 1 asynchronous result */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8,
          void (I::*m)(A1, A2, A3, A4, A5, A6, A7,
                       Result1<typename dbus_traits<A8>::arg_type> *)>
DBusMessage *methodFunction(DBusConnection *conn,
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
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
        dbus_traits<A4>::get(iter, a4);
        dbus_traits<A5>::get(iter, a5);
        dbus_traits<A6>::get(iter, a6);
        dbus_traits<A7>::get(iter, a7);

        (static_cast<I *>(data)->*m)(a1, a2, a3, a4, a5, a6, a7,
                                     new DBusResult1<typename dbus_traits<A8>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 6 arguments, 2 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8,
          void (I::*m)(A1, A2, A3, A4, A5, A6,
                       Result2<typename dbus_traits<A7>::arg_type,
                               typename dbus_traits<A8>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
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
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
        dbus_traits<A4>::get(iter, a4);
        dbus_traits<A5>::get(iter, a5);
        dbus_traits<A6>::get(iter, a6);

        (static_cast<I *>(data)->*m)(a1, a2, a3, a4, a5, a6,
                                     new DBusResult2<typename dbus_traits<A7>::arg_type,
                                                     typename dbus_traits<A8>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 6 arguments, 3 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8,
          void (I::*m)(A1, A2, A3, A4, A5,
                       Result3<typename dbus_traits<A6>::arg_type,
                               typename dbus_traits<A7>::arg_type,
                               typename dbus_traits<A8>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
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
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
        dbus_traits<A4>::get(iter, a4);
        dbus_traits<A5>::get(iter, a5);

        (static_cast<I *>(data)->*m)(a1, a2, a3, a4, a5,
                                     new DBusResult3<typename dbus_traits<A6>::arg_type,
                                                     typename dbus_traits<A7>::arg_type,
                                                     typename dbus_traits<A8>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 4 arguments, 4 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8,
          void (I::*m)(A1, A2, A3, A4,
                       Result4<typename dbus_traits<A5>::arg_type,
                               typename dbus_traits<A6>::arg_type,
                               typename dbus_traits<A7>::arg_type,
                               typename dbus_traits<A8>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<A1>::host_type a1;
        typename dbus_traits<A2>::host_type a2;
        typename dbus_traits<A3>::host_type a3;
        typename dbus_traits<A4>::host_type a4;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
        dbus_traits<A4>::get(iter, a4);

        (static_cast<I *>(data)->*m)(a1, a2, a3, a4,
                                     new DBusResult4<typename dbus_traits<A5>::arg_type,
                                                     typename dbus_traits<A6>::arg_type,
                                                     typename dbus_traits<A7>::arg_type,
                                                     typename dbus_traits<A8>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 3 arguments, 5 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8,
          void (I::*m)(A1, A2, A3,
                       Result5<typename dbus_traits<A4>::arg_type,
                               typename dbus_traits<A5>::arg_type,
                               typename dbus_traits<A6>::arg_type,
                               typename dbus_traits<A7>::arg_type,
                               typename dbus_traits<A8>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<A1>::host_type a1;
        typename dbus_traits<A2>::host_type a2;
        typename dbus_traits<A3>::host_type a3;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);

        (static_cast<I *>(data)->*m)(a1, a2, a3,
                                     new DBusResult5<typename dbus_traits<A4>::arg_type,
                                                     typename dbus_traits<A5>::arg_type,
                                                     typename dbus_traits<A6>::arg_type,
                                                     typename dbus_traits<A7>::arg_type,
                                                     typename dbus_traits<A8>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 2 arguments, 6 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8,
          void (I::*m)(A1, A2,
                       Result6<typename dbus_traits<A3>::arg_type,
                               typename dbus_traits<A4>::arg_type,
                               typename dbus_traits<A5>::arg_type,
                               typename dbus_traits<A6>::arg_type,
                               typename dbus_traits<A7>::arg_type,
                               typename dbus_traits<A8>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<A1>::host_type a1;
        typename dbus_traits<A2>::host_type a2;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);

        (static_cast<I *>(data)->*m)(a1, a2,
                                     new DBusResult6<typename dbus_traits<A3>::arg_type,
                                                     typename dbus_traits<A4>::arg_type,
                                                     typename dbus_traits<A5>::arg_type,
                                                     typename dbus_traits<A6>::arg_type,
                                                     typename dbus_traits<A7>::arg_type,
                                                     typename dbus_traits<A8>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 1 arguments, 7 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8,
          void (I::*m)(A1,
                       Result7<typename dbus_traits<A2>::arg_type,
                               typename dbus_traits<A3>::arg_type,
                               typename dbus_traits<A4>::arg_type,
                               typename dbus_traits<A5>::arg_type,
                               typename dbus_traits<A6>::arg_type,
                               typename dbus_traits<A7>::arg_type,
                               typename dbus_traits<A8>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<A1>::host_type a1;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(iter, a1);

        (static_cast<I *>(data)->*m)(a1,
                                     new DBusResult7<typename dbus_traits<A2>::arg_type,
                                                     typename dbus_traits<A3>::arg_type,
                                                     typename dbus_traits<A4>::arg_type,
                                                     typename dbus_traits<A5>::arg_type,
                                                     typename dbus_traits<A6>::arg_type,
                                                     typename dbus_traits<A7>::arg_type,
                                                     typename dbus_traits<A8>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 0 arguments, 8 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8,
          void (I::*m)(Result8<typename dbus_traits<A1>::arg_type,
                               typename dbus_traits<A2>::arg_type,
                               typename dbus_traits<A3>::arg_type,
                               typename dbus_traits<A4>::arg_type,
                               typename dbus_traits<A5>::arg_type,
                               typename dbus_traits<A6>::arg_type,
                               typename dbus_traits<A7>::arg_type,
                               typename dbus_traits<A8>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        (static_cast<I *>(data)->*m)(new DBusResult8<typename dbus_traits<A1>::arg_type,
                                                     typename dbus_traits<A2>::arg_type,
                                                     typename dbus_traits<A3>::arg_type,
                                                     typename dbus_traits<A4>::arg_type,
                                                     typename dbus_traits<A5>::arg_type,
                                                     typename dbus_traits<A6>::arg_type,
                                                     typename dbus_traits<A7>::arg_type,
                                                     typename dbus_traits<A8>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** ===> 7 parameters */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7,
          void (I::*m)(A1, A2, A3, A4, A5, A6, A7)>
DBusMessage *methodFunction(DBusConnection *conn,
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
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
        dbus_traits<A4>::get(iter, a4);
        dbus_traits<A5>::get(iter, a5);
        dbus_traits<A6>::get(iter, a6);
        dbus_traits<A7>::get(iter, a7);

        (static_cast<I *>(data)->*m)(a1, a2, a3, a4, a5, a6, a7);

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

/** 6 arguments, 1 return value */
template <class I, class R,
          class A1, class A2, class A3, class A4, class A5,
          class A6,
          R (I::*m)(A1, A2, A3, A4, A5, A6)>
DBusMessage *methodFunction(DBusConnection *conn,
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
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
        dbus_traits<A4>::get(iter, a4);
        dbus_traits<A5>::get(iter, a5);
        dbus_traits<A6>::get(iter, a6);

        r = (static_cast<I *>(data)->*m)(a1, a2, a3, a4, a5, a6);

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

/** 7 arguments, 0 asynchronous result */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7,
          void (I::*m)(A1, A2, A3, A4, A5, A6, A7,
                       Result0 *)>
DBusMessage *methodFunction(DBusConnection *conn,
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
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
        dbus_traits<A4>::get(iter, a4);
        dbus_traits<A5>::get(iter, a5);
        dbus_traits<A6>::get(iter, a6);
        dbus_traits<A7>::get(iter, a7);

        (static_cast<I *>(data)->*m)(a1, a2, a3, a4, a5, a6, a7,
                                     new DBusResult0(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 6 arguments, 1 asynchronous result */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7,
          void (I::*m)(A1, A2, A3, A4, A5, A6,
                       Result1<typename dbus_traits<A7>::arg_type> *)>
DBusMessage *methodFunction(DBusConnection *conn,
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
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
        dbus_traits<A4>::get(iter, a4);
        dbus_traits<A5>::get(iter, a5);
        dbus_traits<A6>::get(iter, a6);

        (static_cast<I *>(data)->*m)(a1, a2, a3, a4, a5, a6,
                                     new DBusResult1<typename dbus_traits<A7>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 5 arguments, 2 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7,
          void (I::*m)(A1, A2, A3, A4, A5,
                       Result2<typename dbus_traits<A6>::arg_type,
                               typename dbus_traits<A7>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
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
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
        dbus_traits<A4>::get(iter, a4);
        dbus_traits<A5>::get(iter, a5);
 
        (static_cast<I *>(data)->*m)(a1, a2, a3, a4, a5,
                                     new DBusResult2<typename dbus_traits<A6>::arg_type,
                                                     typename dbus_traits<A7>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 4 arguments, 3 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7,
          void (I::*m)(A1, A2, A3, A4,
                       Result3<typename dbus_traits<A5>::arg_type,
                               typename dbus_traits<A6>::arg_type,
                               typename dbus_traits<A7>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<A1>::host_type a1;
        typename dbus_traits<A2>::host_type a2;
        typename dbus_traits<A3>::host_type a3;
        typename dbus_traits<A4>::host_type a4;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
        dbus_traits<A4>::get(iter, a4);

        (static_cast<I *>(data)->*m)(a1, a2, a3, a4,
                                     new DBusResult3<typename dbus_traits<A5>::arg_type,
                                                     typename dbus_traits<A6>::arg_type,
                                                     typename dbus_traits<A7>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 3 arguments, 4 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7,
          void (I::*m)(A1, A2, A3,
                       Result4<typename dbus_traits<A4>::arg_type,
                               typename dbus_traits<A5>::arg_type,
                               typename dbus_traits<A6>::arg_type,
                               typename dbus_traits<A7>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<A1>::host_type a1;
        typename dbus_traits<A2>::host_type a2;
        typename dbus_traits<A3>::host_type a3;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);

        (static_cast<I *>(data)->*m)(a1, a2, a3,
                                     new DBusResult4<typename dbus_traits<A4>::arg_type,
                                                     typename dbus_traits<A5>::arg_type,
                                                     typename dbus_traits<A6>::arg_type,
                                                     typename dbus_traits<A7>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 2 arguments, 5 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7,
          void (I::*m)(A1, A2,
                       Result5<typename dbus_traits<A3>::arg_type,
                               typename dbus_traits<A4>::arg_type,
                               typename dbus_traits<A5>::arg_type,
                               typename dbus_traits<A6>::arg_type,
                               typename dbus_traits<A7>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<A1>::host_type a1;
        typename dbus_traits<A2>::host_type a2;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);

        (static_cast<I *>(data)->*m)(a1, a2,
                                     new DBusResult5<typename dbus_traits<A3>::arg_type,
                                                     typename dbus_traits<A4>::arg_type,
                                                     typename dbus_traits<A5>::arg_type,
                                                     typename dbus_traits<A6>::arg_type,
                                                     typename dbus_traits<A7>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 1 argument, 6 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7,
          void (I::*m)(A1,
                       Result6<typename dbus_traits<A2>::arg_type,
                               typename dbus_traits<A3>::arg_type,
                               typename dbus_traits<A4>::arg_type,
                               typename dbus_traits<A5>::arg_type,
                               typename dbus_traits<A6>::arg_type,
                               typename dbus_traits<A7>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<A1>::host_type a1;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(iter, a1);

        (static_cast<I *>(data)->*m)(a1,
                                     new DBusResult6<typename dbus_traits<A2>::arg_type,
                                                     typename dbus_traits<A3>::arg_type,
                                                     typename dbus_traits<A4>::arg_type,
                                                     typename dbus_traits<A5>::arg_type,
                                                     typename dbus_traits<A6>::arg_type,
                                                     typename dbus_traits<A7>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 0 argument, 7 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7,
          void (I::*m)(Result7<typename dbus_traits<A1>::arg_type,
                               typename dbus_traits<A2>::arg_type,
                               typename dbus_traits<A3>::arg_type,
                               typename dbus_traits<A4>::arg_type,
                               typename dbus_traits<A5>::arg_type,
                               typename dbus_traits<A6>::arg_type,
                               typename dbus_traits<A7>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        (static_cast<I *>(data)->*m)(new DBusResult7<typename dbus_traits<A1>::arg_type,
                                                     typename dbus_traits<A2>::arg_type,
                                                     typename dbus_traits<A3>::arg_type,
                                                     typename dbus_traits<A4>::arg_type,
                                                     typename dbus_traits<A5>::arg_type,
                                                     typename dbus_traits<A6>::arg_type,
                                                     typename dbus_traits<A7>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** ===> 6 parameters */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6,
          void (I::*m)(A1, A2, A3, A4, A5, A6)>
DBusMessage *methodFunction(DBusConnection *conn,
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
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
        dbus_traits<A4>::get(iter, a4);
        dbus_traits<A5>::get(iter, a5);
        dbus_traits<A6>::get(iter, a6);

        (static_cast<I *>(data)->*m)(a1, a2, a3, a4, a5, a6);

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

/** 5 arguments, 1 return value */
template <class I, class R,
          class A1, class A2, class A3, class A4, class A5,
          R (I::*m)(A1, A2, A3, A4, A5)>
DBusMessage *methodFunction(DBusConnection *conn,
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
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
        dbus_traits<A4>::get(iter, a4);
        dbus_traits<A5>::get(iter, a5);

        r = (static_cast<I *>(data)->*m)(a1, a2, a3, a4, a5);

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

/** 6 arguments, 0 asynchronous result */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6,
          void (I::*m)(A1, A2, A3, A4, A5, A6,
                       Result0 *)>
DBusMessage *methodFunction(DBusConnection *conn,
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
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
        dbus_traits<A4>::get(iter, a4);
        dbus_traits<A5>::get(iter, a5);
        dbus_traits<A6>::get(iter, a6);

        (static_cast<I *>(data)->*m)(a1, a2, a3, a4, a5, a6,
                                     new DBusResult0(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 5 arguments, 1 asynchronous result */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6,
          void (I::*m)(A1, A2, A3, A4, A5,
                       Result1<typename dbus_traits<A6>::arg_type> *)>
DBusMessage *methodFunction(DBusConnection *conn,
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
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
        dbus_traits<A4>::get(iter, a4);
        dbus_traits<A5>::get(iter, a5);

        (static_cast<I *>(data)->*m)(a1, a2, a3, a4, a5,
                                     new DBusResult1<typename dbus_traits<A6>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 4 arguments, 2 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6,
          void (I::*m)(A1, A2, A3, A4,
                       Result2<typename dbus_traits<A5>::arg_type,
                               typename dbus_traits<A6>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<A1>::host_type a1;
        typename dbus_traits<A2>::host_type a2;
        typename dbus_traits<A3>::host_type a3;
        typename dbus_traits<A4>::host_type a4;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
        dbus_traits<A4>::get(iter, a4);
 
        (static_cast<I *>(data)->*m)(a1, a2, a3, a4,
                                     new DBusResult2<typename dbus_traits<A5>::arg_type,
                                                     typename dbus_traits<A6>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 3 arguments, 3 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6,
          void (I::*m)(A1, A2, A3,
                       Result3<typename dbus_traits<A4>::arg_type,
                               typename dbus_traits<A5>::arg_type,
                               typename dbus_traits<A6>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<A1>::host_type a1;
        typename dbus_traits<A2>::host_type a2;
        typename dbus_traits<A3>::host_type a3;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);

        (static_cast<I *>(data)->*m)(a1, a2, a3,
                                     new DBusResult3<typename dbus_traits<A4>::arg_type,
                                                     typename dbus_traits<A5>::arg_type,
                                                     typename dbus_traits<A6>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 2 arguments, 4 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6,
          void (I::*m)(A1, A2,
                       Result4<typename dbus_traits<A3>::arg_type,
                               typename dbus_traits<A4>::arg_type,
                               typename dbus_traits<A5>::arg_type,
                               typename dbus_traits<A6>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<A1>::host_type a1;
        typename dbus_traits<A2>::host_type a2;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);

        (static_cast<I *>(data)->*m)(a1, a2,
                                     new DBusResult4<typename dbus_traits<A3>::arg_type,
                                                     typename dbus_traits<A4>::arg_type,
                                                     typename dbus_traits<A5>::arg_type,
                                                     typename dbus_traits<A6>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 1 argument, 5 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6,
          void (I::*m)(A1,
                       Result5<typename dbus_traits<A2>::arg_type,
                               typename dbus_traits<A3>::arg_type,
                               typename dbus_traits<A4>::arg_type,
                               typename dbus_traits<A5>::arg_type,
                               typename dbus_traits<A6>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<A1>::host_type a1;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(iter, a1);

        (static_cast<I *>(data)->*m)(a1,
                                     new DBusResult5<typename dbus_traits<A2>::arg_type,
                                                     typename dbus_traits<A3>::arg_type,
                                                     typename dbus_traits<A4>::arg_type,
                                                     typename dbus_traits<A5>::arg_type,
                                                     typename dbus_traits<A6>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 0 arguments, 6 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6,
          void (I::*m)(Result6<typename dbus_traits<A1>::arg_type,
                               typename dbus_traits<A2>::arg_type,
                               typename dbus_traits<A3>::arg_type,
                               typename dbus_traits<A4>::arg_type,
                               typename dbus_traits<A5>::arg_type,
                               typename dbus_traits<A6>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        (static_cast<I *>(data)->*m)(new DBusResult6<typename dbus_traits<A1>::arg_type,
                                                     typename dbus_traits<A2>::arg_type,
                                                     typename dbus_traits<A3>::arg_type,
                                                     typename dbus_traits<A4>::arg_type,
                                                     typename dbus_traits<A5>::arg_type,
                                                     typename dbus_traits<A6>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** ===> 5 parameters */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          void (I::*m)(A1, A2, A3, A4, A5)>
DBusMessage *methodFunction(DBusConnection *conn,
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
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
        dbus_traits<A4>::get(iter, a4);
        dbus_traits<A5>::get(iter, a5);

        (static_cast<I *>(data)->*m)(a1, a2, a3, a4, a5);

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

/** 4 arguments, 1 return value */
template <class I, class R,
          class A1, class A2, class A3, class A4,
          R (I::*m)(A1, A2, A3, A4)>
DBusMessage *methodFunction(DBusConnection *conn,
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
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
        dbus_traits<A4>::get(iter, a4);

        r = (static_cast<I *>(data)->*m)(a1, a2, a3, a4);

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

/** 5 arguments, 0 asynchronous result */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          void (I::*m)(A1, A2, A3, A4, A5,
                       Result0 *)>
DBusMessage *methodFunction(DBusConnection *conn,
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
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
        dbus_traits<A4>::get(iter, a4);
        dbus_traits<A5>::get(iter, a5);

        (static_cast<I *>(data)->*m)(a1, a2, a3, a4, a5,
                                     new DBusResult0(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 4 arguments, 1 asynchronous result */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          void (I::*m)(A1, A2, A3, A4,
                       Result1<typename dbus_traits<A5>::arg_type> *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<A1>::host_type a1;
        typename dbus_traits<A2>::host_type a2;
        typename dbus_traits<A3>::host_type a3;
        typename dbus_traits<A4>::host_type a4;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
        dbus_traits<A4>::get(iter, a4);

        (static_cast<I *>(data)->*m)(a1, a2, a3, a4,
                                     new DBusResult1<typename dbus_traits<A5>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 3 arguments, 2 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          void (I::*m)(A1, A2, A3,
                       Result2<typename dbus_traits<A4>::arg_type,
                               typename dbus_traits<A5>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<A1>::host_type a1;
        typename dbus_traits<A2>::host_type a2;
        typename dbus_traits<A3>::host_type a3;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
 
        (static_cast<I *>(data)->*m)(a1, a2, a3,
                                     new DBusResult2<typename dbus_traits<A4>::arg_type,
                                                     typename dbus_traits<A5>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 2 arguments, 3 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          void (I::*m)(A1, A2,
                       Result3<typename dbus_traits<A3>::arg_type,
                               typename dbus_traits<A4>::arg_type,
                               typename dbus_traits<A5>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<A1>::host_type a1;
        typename dbus_traits<A2>::host_type a2;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);

        (static_cast<I *>(data)->*m)(a1, a2,
                                     new DBusResult3<typename dbus_traits<A3>::arg_type,
                                                     typename dbus_traits<A4>::arg_type,
                                                     typename dbus_traits<A5>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 1 argument, 4 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          void (I::*m)(A1,
                       Result4<typename dbus_traits<A2>::arg_type,
                               typename dbus_traits<A3>::arg_type,
                               typename dbus_traits<A4>::arg_type,
                               typename dbus_traits<A5>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<A1>::host_type a1;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(iter, a1);

        (static_cast<I *>(data)->*m)(a1,
                                     new DBusResult4<typename dbus_traits<A2>::arg_type,
                                                     typename dbus_traits<A3>::arg_type,
                                                     typename dbus_traits<A4>::arg_type,
                                                     typename dbus_traits<A5>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 0 arguments, 5 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4, class A5,
          void (I::*m)(Result5<typename dbus_traits<A1>::arg_type,
                               typename dbus_traits<A2>::arg_type,
                               typename dbus_traits<A3>::arg_type,
                               typename dbus_traits<A4>::arg_type,
                               typename dbus_traits<A5>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        (static_cast<I *>(data)->*m)(new DBusResult5<typename dbus_traits<A1>::arg_type,
                                                     typename dbus_traits<A2>::arg_type,
                                                     typename dbus_traits<A3>::arg_type,
                                                     typename dbus_traits<A4>::arg_type,
                                                     typename dbus_traits<A5>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** ===> 4 parameters */
template <class I,
          class A1, class A2, class A3, class A4,
          void (I::*m)(A1, A2, A3, A4)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<A1>::host_type a1;
        typename dbus_traits<A2>::host_type a2;
        typename dbus_traits<A3>::host_type a3;
        typename dbus_traits<A4>::host_type a4;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
        dbus_traits<A4>::get(iter, a4);

        (static_cast<I *>(data)->*m)(a1, a2, a3, a4);

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

/** 3 arguments, 1 return value */
template <class I, class R,
          class A1, class A2, class A3,
          R (I::*m)(A1, A2, A3)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<R>::host_type r;
        typename dbus_traits<A1>::host_type a1;
        typename dbus_traits<A2>::host_type a2;
        typename dbus_traits<A3>::host_type a3;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);

        r = (static_cast<I *>(data)->*m)(a1, a2, a3);

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

/** 4 arguments, 0 asynchronous result */
template <class I,
          class A1, class A2, class A3, class A4,
          void (I::*m)(A1, A2, A3, A4,
                       Result0 *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<A1>::host_type a1;
        typename dbus_traits<A2>::host_type a2;
        typename dbus_traits<A3>::host_type a3;
        typename dbus_traits<A4>::host_type a4;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);
        dbus_traits<A4>::get(iter, a4);

        (static_cast<I *>(data)->*m)(a1, a2, a3, a4,
                                     new DBusResult0(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 3 arguments, 1 asynchronous result */
template <class I,
          class A1, class A2, class A3, class A4,
          void (I::*m)(A1, A2, A3,
                       Result1<typename dbus_traits<A4>::arg_type> *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<A1>::host_type a1;
        typename dbus_traits<A2>::host_type a2;
        typename dbus_traits<A3>::host_type a3;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);

        (static_cast<I *>(data)->*m)(a1, a2, a3,
                                     new DBusResult1<typename dbus_traits<A4>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 2 arguments, 2 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4,
          void (I::*m)(A1, A2,
                       Result2<typename dbus_traits<A3>::arg_type,
                               typename dbus_traits<A4>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<A1>::host_type a1;
        typename dbus_traits<A2>::host_type a2;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
 
        (static_cast<I *>(data)->*m)(a1, a2,
                                     new DBusResult2<typename dbus_traits<A3>::arg_type,
                                                     typename dbus_traits<A4>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 1 argument, 3 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4,
          void (I::*m)(A1,
                       Result3<typename dbus_traits<A2>::arg_type,
                               typename dbus_traits<A3>::arg_type,
                               typename dbus_traits<A4>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<A1>::host_type a1;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(iter, a1);

        (static_cast<I *>(data)->*m)(a1,
                                     new DBusResult3<typename dbus_traits<A2>::arg_type,
                                                     typename dbus_traits<A3>::arg_type,
                                                     typename dbus_traits<A4>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 0 arguments, 4 asynchronous results */
template <class I,
          class A1, class A2, class A3, class A4,
          void (I::*m)(Result4<typename dbus_traits<A1>::arg_type,
                               typename dbus_traits<A2>::arg_type,
                               typename dbus_traits<A3>::arg_type,
                               typename dbus_traits<A4>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        (static_cast<I *>(data)->*m)(new DBusResult4<typename dbus_traits<A1>::arg_type,
                                                     typename dbus_traits<A2>::arg_type,
                                                     typename dbus_traits<A3>::arg_type,
                                                     typename dbus_traits<A4>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** ===> 3 parameters */
template <class I,
          class A1, class A2, class A3,
          void (I::*m)(A1, A2, A3)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<A1>::host_type a1;
        typename dbus_traits<A2>::host_type a2;
        typename dbus_traits<A3>::host_type a3;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);

        (static_cast<I *>(data)->*m)(a1, a2, a3);

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

/** 2 arguments, 1 return value */
template <class I, class R,
          class A1, class A2,
          R (I::*m)(A1, A2)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<R>::host_type r;
        typename dbus_traits<A1>::host_type a1;
        typename dbus_traits<A2>::host_type a2;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);

        r = (static_cast<I *>(data)->*m)(a1, a2);

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

/** 3 arguments, 0 asynchronous result */
template <class I,
          class A1, class A2, class A3,
          void (I::*m)(A1, A2, A3,
                       Result0 *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<A1>::host_type a1;
        typename dbus_traits<A2>::host_type a2;
        typename dbus_traits<A3>::host_type a3;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);
        dbus_traits<A3>::get(iter, a3);

        (static_cast<I *>(data)->*m)(a1, a2, a3,
                                     new DBusResult0(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 2 arguments, 1 asynchronous result */
template <class I,
          class A1, class A2, class A3,
          void (I::*m)(A1, A2,
                       Result1<typename dbus_traits<A3>::arg_type> *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<A1>::host_type a1;
        typename dbus_traits<A2>::host_type a2;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);

        (static_cast<I *>(data)->*m)(a1, a2,
                                     new DBusResult1<typename dbus_traits<A3>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 1 argument, 2 asynchronous results */
template <class I,
          class A1, class A2, class A3,
          void (I::*m)(A1,
                       Result2<typename dbus_traits<A2>::arg_type,
                               typename dbus_traits<A3>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<A1>::host_type a1;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(iter, a1);
 
        (static_cast<I *>(data)->*m)(a1,
                                     new DBusResult2<typename dbus_traits<A2>::arg_type,
                                                     typename dbus_traits<A3>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 0 arguments, 3 asynchronous results */
template <class I,
          class A1, class A2, class A3,
          void (I::*m)(Result3<typename dbus_traits<A1>::arg_type,
                               typename dbus_traits<A2>::arg_type,
                               typename dbus_traits<A3>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        (static_cast<I *>(data)->*m)(new DBusResult3<typename dbus_traits<A1>::arg_type,
                                                     typename dbus_traits<A2>::arg_type,
                                                     typename dbus_traits<A3>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** ===> 2 parameters */
template <class I,
          class A1, class A2,
          void (I::*m)(A1, A2)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<A1>::host_type a1;
        typename dbus_traits<A2>::host_type a2;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);

        (static_cast<I *>(data)->*m)(a1, a2);

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

/** 1 argument, 1 return value */
template <class I, class R,
          class A1,
          R (I::*m)(A1)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<R>::host_type r;
        typename dbus_traits<A1>::host_type a1;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(iter, a1);

        r = (static_cast<I *>(data)->*m)(a1);

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

/** 2 arguments, 0 asynchronous result */
template <class I,
          class A1, class A2,
          void (I::*m)(A1, A2,
                       Result0 *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<A1>::host_type a1;
        typename dbus_traits<A2>::host_type a2;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(iter, a1);
        dbus_traits<A2>::get(iter, a2);

        (static_cast<I *>(data)->*m)(a1, a2,
                                     new DBusResult0(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 1 argument, 1 asynchronous result */
template <class I,
          class A1, class A2,
          void (I::*m)(A1,
                       Result1<typename dbus_traits<A2>::arg_type> *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<A1>::host_type a1;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(iter, a1);

        (static_cast<I *>(data)->*m)(a1,
                                     new DBusResult1<typename dbus_traits<A2>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 0 argument, 2 asynchronous results */
template <class I,
          class A1, class A2,
          void (I::*m)(Result2<typename dbus_traits<A1>::arg_type,
                               typename dbus_traits<A2>::arg_type>
                               *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        (static_cast<I *>(data)->*m)(new DBusResult2<typename dbus_traits<A1>::arg_type,
                                                     typename dbus_traits<A2>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** ===> 1 parameter */
template <class I,
          class A1,
          void (I::*m)(A1)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<A1>::host_type a1;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(iter, a1);

        (static_cast<I *>(data)->*m)(a1);

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

/** 0 arguments, 1 return value */
template <class I, class R,
          R (I::*m)()>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<R>::host_type r;

        r = (static_cast<I *>(data)->*m)();

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

/** 1 argument, 0 asynchronous result */
template <class I,
          class A1,
          void (I::*m)(A1,
                       Result0 *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        typename dbus_traits<A1>::host_type a1;

        DBusMessageIter iter;
        dbus_message_iter_init(msg, &iter);
        dbus_traits<A1>::get(iter, a1);

        (static_cast<I *>(data)->*m)(a1,
                                     new DBusResult0(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** 0 arguments, 1 asynchronous result */
template <class I,
          class A1,
          void (I::*m)(Result1<typename dbus_traits<A1>::arg_type> *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        (static_cast<I *>(data)->*m)(new DBusResult1<typename dbus_traits<A1>::arg_type>(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}

/** ===> 0 parameter */
template <class I,
          void (I::*m)()>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        (static_cast<I *>(data)->*m)();

        DBusMessage *reply = dbus_message_new_method_return(msg);
        if (!reply)
            return NULL;
        return reply;
    } catch (...) {
        return handleException(msg);
    }
}

/** 0 argument, 0 asynchronous result */
template <class I,
          void (I::*m)(Result0 *)>
DBusMessage *methodFunction(DBusConnection *conn,
                            DBusMessage *msg, void *data)
{
    try {
        (static_cast<I *>(data)->*m)(new DBusResult0(conn, msg));

        return NULL;
    } catch (...) {
        return handleException(msg);
    }
}


template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8, class A9, class A10,
          class M, M m>
GDBusMethodTable makeMethodEntry(const char *name, GDBusMethodFlags flags = GDBusMethodFlags(0))
{
    GDBusMethodTable entry;
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
    entry.function = methodFunction<I, A1, A2, A3, A4, A5, A6, A7, A8, A9, A10, m>;
    entry.flags = flags;
    return entry;
}

template <class I, class R,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8, class A9,
          R (I::*m)(A1, A2, A3, A4, A5, A6, A7, A8, A9)>
GDBusMethodTable makeMethodEntry(const char *name, GDBusMethodFlags flags = GDBusMethodFlags(0))
{
    GDBusMethodTable entry;
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
    entry.function = methodFunction<I, R, A1, A2, A3, A4, A5, A6, A7, A8, A9, m>;
    entry.flags = flags;
    return entry;
}

template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8, class A9,
          class M, M m>
GDBusMethodTable makeMethodEntry(const char *name, GDBusMethodFlags flags = GDBusMethodFlags(0))
{
    GDBusMethodTable entry;
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
    entry.function = methodFunction<I, A1, A2, A3, A4, A5, A6, A7, A8, A9, m>;
    entry.flags = flags;
    return entry;
}

template <class I, class R,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8,
          R (I::*m)(A1, A2, A3, A4, A5, A6, A7, A8)>
GDBusMethodTable makeMethodEntry(const char *name, GDBusMethodFlags flags = GDBusMethodFlags(0))
{
    GDBusMethodTable entry;
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
    entry.function = methodFunction<I, R, A1, A2, A3, A4, A5, A6, A7, A8, m>;
    entry.flags = flags;
    return entry;
}

template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7, class A8,
          class M, M m>
GDBusMethodTable makeMethodEntry(const char *name, GDBusMethodFlags flags = GDBusMethodFlags(0))
{
    GDBusMethodTable entry;
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
    entry.function = methodFunction<I, A1, A2, A3, A4, A5, A6, A7, A8, m>;
    entry.flags = flags;
    return entry;
}

template <class I, class R,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7,
          R (I::*m)(A1, A2, A3, A4, A5, A6, A7)>
GDBusMethodTable makeMethodEntry(const char *name, GDBusMethodFlags flags = GDBusMethodFlags(0))
{
    GDBusMethodTable entry;
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
    entry.function = methodFunction<I, R, A1, A2, A3, A4, A5, A6, A7, m>;
    entry.flags = flags;
    return entry;
}

template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6, class A7,
          class M, M m>
GDBusMethodTable makeMethodEntry(const char *name, GDBusMethodFlags flags = GDBusMethodFlags(0))
{
    GDBusMethodTable entry;
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
    entry.function = methodFunction<I, A1, A2, A3, A4, A5, A6, A7, m>;
    entry.flags = flags;
    return entry;
}

template <class I, class R,
          class A1, class A2, class A3, class A4, class A5,
          class A6,
          R (I::*m)(A1, A2, A3, A4, A5, A6)>
GDBusMethodTable makeMethodEntry(const char *name, GDBusMethodFlags flags = GDBusMethodFlags(0))
{
    GDBusMethodTable entry;
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
    entry.function = methodFunction<I, R, A1, A2, A3, A4, A5, A6, m>;
    entry.flags = flags;
    return entry;
}

template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class A6,
          class M, M m>
GDBusMethodTable makeMethodEntry(const char *name, GDBusMethodFlags flags = GDBusMethodFlags(0))
{
    GDBusMethodTable entry;
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
    entry.function = methodFunction<I, A1, A2, A3, A4, A5, A6, m>;
    entry.flags = flags;
    return entry;
}

template <class I, class R,
          class A1, class A2, class A3, class A4, class A5,
          R (I::*m)(A1, A2, A3, A4, A5)>
GDBusMethodTable makeMethodEntry(const char *name, GDBusMethodFlags flags = GDBusMethodFlags(0))
{
    GDBusMethodTable entry;
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
    entry.function = methodFunction<I, R, A1, A2, A3, A4, A5, m>;
    entry.flags = flags;
    return entry;
}

template <class I,
          class A1, class A2, class A3, class A4, class A5,
          class M, M m>
GDBusMethodTable makeMethodEntry(const char *name, GDBusMethodFlags flags = GDBusMethodFlags(0))
{
    GDBusMethodTable entry;
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
    entry.function = methodFunction<I, A1, A2, A3, A4, A5, m>;
    entry.flags = flags;
    return entry;
}

template <class I, class R,
          class A1, class A2, class A3, class A4,
          R (I::*m)(A1, A2, A3, A4)>
GDBusMethodTable makeMethodEntry(const char *name, GDBusMethodFlags flags = GDBusMethodFlags(0))
{
    GDBusMethodTable entry;
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
    entry.function = methodFunction<I, R, A1, A2, A3, A4, m>;
    entry.flags = flags;
    return entry;
}

template <class I,
          class A1, class A2, class A3, class A4,
          class M, M m>
GDBusMethodTable makeMethodEntry(const char *name, GDBusMethodFlags flags = GDBusMethodFlags(0))
{
    GDBusMethodTable entry;
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
    entry.function = methodFunction<I, A1, A2, A3, A4, m>;
    entry.flags = flags;
    return entry;
}

template <class I, class R,
          class A1, class A2, class A3,
          R (I::*m)(A1, A2, A3)>
GDBusMethodTable makeMethodEntry(const char *name, GDBusMethodFlags flags = GDBusMethodFlags(0))
{
    GDBusMethodTable entry;
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
    entry.function = methodFunction<I, R, A1, A2, A3, m>;
    entry.flags = flags;
    return entry;
}

template <class I,
          class A1, class A2, class A3,
          class M, M m>
GDBusMethodTable makeMethodEntry(const char *name, GDBusMethodFlags flags = GDBusMethodFlags(0))
{
    GDBusMethodTable entry;
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
    entry.function = methodFunction<I, A1, A2, A3, m>;
    entry.flags = flags;
    return entry;
}

template <class I, class R,
          class A1, class A2,
          R (I::*m)(A1, A2)>
GDBusMethodTable makeMethodEntry(const char *name, GDBusMethodFlags flags = GDBusMethodFlags(0))
{
    GDBusMethodTable entry;
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
    entry.function = methodFunction<I, R, A1, A2, m>;
    entry.flags = flags;
    return entry;
}

template <class I,
          class A1, class A2,
          class M, M m>
GDBusMethodTable makeMethodEntry(const char *name, GDBusMethodFlags flags = GDBusMethodFlags(0))
{
    GDBusMethodTable entry;
    entry.name = strdup(name);
    std::string buffer;
    buffer += dbus_traits<A1>::getSignature();
    buffer += dbus_traits<A2>::getSignature();
    entry.signature = strdup(buffer.c_str());
    buffer.clear();
    buffer += dbus_traits<A1>::getReply();
    buffer += dbus_traits<A2>::getReply();
    entry.reply = strdup(buffer.c_str());
    entry.function = methodFunction<I, A1, A2, m>;
    entry.flags = flags;
    return entry;
}

template <class I, class R,
          class A1,
          R (I::*m)(A1)>
GDBusMethodTable makeMethodEntry(const char *name, GDBusMethodFlags flags = GDBusMethodFlags(0))
{
    GDBusMethodTable entry;
    entry.name = strdup(name);
    std::string buffer;
    buffer += dbus_traits<A1>::getSignature();
    entry.signature = strdup(buffer.c_str());
    buffer.clear();
    buffer += dbus_traits<R>::getReply();
    buffer += dbus_traits<A1>::getReply();
    entry.reply = strdup(buffer.c_str());
    entry.function = methodFunction<I, R, A1, m>;
    entry.flags = flags;
    return entry;
}

template <class I,
          class A1,
          class M, M m>
GDBusMethodTable makeMethodEntry(const char *name, GDBusMethodFlags flags = GDBusMethodFlags(0))
{
    GDBusMethodTable entry;
    entry.name = strdup(name);
    std::string buffer;
    buffer += dbus_traits<A1>::getSignature();
    entry.signature = strdup(buffer.c_str());
    buffer.clear();
    buffer += dbus_traits<A1>::getReply();
    entry.reply = strdup(buffer.c_str());
    entry.function = methodFunction<I, A1, m>;
    entry.flags = flags;
    return entry;
}

template <class I, class R,
          R (I::*m)()>
GDBusMethodTable makeMethodEntry(const char *name, GDBusMethodFlags flags = GDBusMethodFlags(0))
{
    GDBusMethodTable entry;
    entry.name = strdup(name);
    std::string buffer;
    entry.signature = strdup(buffer.c_str());
    buffer.clear();
    buffer += dbus_traits<R>::getReply();
    entry.reply = strdup(buffer.c_str());
    entry.function = methodFunction<I, R, m>;
    entry.flags = flags;
    return entry;
}

template <class I,
          class M, M m>
GDBusMethodTable makeMethodEntry(const char *name, GDBusMethodFlags flags = GDBusMethodFlags(0))
{
    GDBusMethodTable entry;
    entry.name = strdup(name);
    std::string buffer;
    entry.signature = strdup(buffer.c_str());
    buffer.clear();
    entry.reply = strdup(buffer.c_str());
    entry.function = methodFunction<I, m>;
    entry.flags = flags;
    return entry;
}

#endif // INCL_GDBUS_CXX_BRIDGE
