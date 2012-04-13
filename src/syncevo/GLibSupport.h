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

#ifndef INCL_GLIB_SUPPORT
# define INCL_GLIB_SUPPORT

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <syncevo/util.h>

#ifdef HAVE_GLIB
# include <glib-object.h>
# include <gio/gio.h>
#else
typedef void *GMainLoop;
#endif

#include <boost/intrusive_ptr.hpp>
#include <boost/utility.hpp>
#include <boost/foreach.hpp>

#include <iterator>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

enum {
    GLIB_SELECT_NONE = 0,
    GLIB_SELECT_READ = 1,
    GLIB_SELECT_WRITE = 2
};

enum GLibSelectResult {
    GLIB_SELECT_TIMEOUT,      /**< returned because not ready after given amount of time */
    GLIB_SELECT_READY,        /**< fd is ready */
    GLIB_SELECT_QUIT          /**< something else caused the loop to quit, return to caller immediately */
};

/**
 * Waits for one particular file descriptor to become ready for reading
 * and/or writing. Keeps the given loop running while waiting.
 *
 * @param  loop       loop to keep running; must not be NULL
 * @param  fd         file descriptor to watch, -1 for none
 * @param  direction  read, write, both, or none (then fd is ignored)
 * @param  timeout    timeout in seconds + nanoseconds from now, NULL for no timeout, empty value for immediate return
 * @return see GLibSelectResult
 */
GLibSelectResult GLibSelect(GMainLoop *loop, int fd, int direction, Timespec *timeout);

#ifdef HAVE_GLIB

/**
 * Defines a shared pointer for a GObject-based type, with intrusive
 * reference counting. Use *outside* of SyncEvolution namespace
 * (i.e. outside of SE_BEGIN/END_CXX. This is necessary because some
 * functions must be put into the boost namespace. The type itself is
 * *inside* the SyncEvolution namespace.
 *
 * Example:
 * SE_GOBJECT_TYPE(GFile);
 * SE_BEGIN_CXX
 * {
 *   // reference normally increased during construction,
 *   // steal() avoids that
 *   GFileCXX filecxx = GFileCXX::steal(g_file_new_for_path("foo"));
 *   GFile *filec = filecxx.get(); // does not increase reference count
 *   // file freed here as filecxx gets destroyed
 * }
 * SE_END_CXX
 */
#define SE_GOBJECT_TYPE(_x) \
    void inline intrusive_ptr_add_ref(_x *ptr) { g_object_ref(ptr); } \
    void inline intrusive_ptr_release(_x *ptr) { g_object_unref(ptr); } \
    SE_BEGIN_CXX \
    class _x ## CXX : public boost::intrusive_ptr<_x> { \
    public: \
         _x ## CXX(_x *ptr, bool add_ref = true) : boost::intrusive_ptr<_x>(ptr, add_ref) {} \
         _x ## CXX() {} \
         _x ## CXX(const _x ## CXX &other) : boost::intrusive_ptr<_x>(other) {} \
\
         static  _x ## CXX steal(_x *ptr) { return _x ## CXX(ptr, false); } \
    }; \
    SE_END_CXX \

SE_END_CXX

SE_GOBJECT_TYPE(GFile)
SE_GOBJECT_TYPE(GFileMonitor)

void inline intrusive_ptr_add_ref(GMainLoop *ptr) { g_main_loop_ref(ptr); }
void inline intrusive_ptr_release(GMainLoop *ptr) { g_main_loop_unref(ptr); }
SE_BEGIN_CXX
typedef boost::intrusive_ptr<GMainLoop> GMainLoopCXX;
SE_END_CXX

SE_BEGIN_CXX

/**
 * Wrapper around g_file_monitor_file().
 * Not copyable because monitor is tied to specific callback
 * via memory address.
 */
class GLibNotify : public boost::noncopyable
{
 public:
    typedef boost::function<void (GFile *, GFile *, GFileMonitorEvent)> callback_t;

    GLibNotify(const char *file, 
               const callback_t &callback);
 private:
    GFileMonitorCXX m_monitor;
    callback_t m_callback;
};

/**
 * always throws an exception, including information from GError if available:
 * <action>: <error message>|failure
 *
 * Takes ownership of the error and frees it.
 *
 * Deprecated. Better use GErrorCXX.
 */
void GLibErrorException(const std::string &action, GError *error);


/**
 * Wraps GError. Where a GError** is expected, simply pass
 * a GErrorCXX instance.
 */
struct GErrorCXX {
    GError *m_gerror;

    /** empty error, NULL pointer */
    GErrorCXX() : m_gerror(NULL) {}

    /** copies error content */
    GErrorCXX(const GErrorCXX &other) : m_gerror(g_error_copy(other.m_gerror)) {}
    GErrorCXX &operator =(const GErrorCXX &other) {
        if (this != &other) {
            if (m_gerror) {
                g_clear_error(&m_gerror);
            }
            m_gerror = g_error_copy(other.m_gerror);
        }
        return *this;
    }

    /** error description, with fallback if not set (not expected, so not localized) */
    operator const char * () { return m_gerror ? m_gerror->message : "<<no error>>"; }

    /** clear error */
    ~GErrorCXX() { g_clear_error(&m_gerror); }

    /** clear error if any is set */
    void clear() { g_clear_error(&m_gerror); }

    /** checks whether the current error is the one passed as parameters */
    bool matches(GQuark domain, gint code) { return g_error_matches(m_gerror, domain, code); }

    /**
     * Use this when passing GErrorCXX instance to C functions which need to set it.
     * Make sure the pointer isn't set yet (new GErrorCXX instance, reset if
     * an error was encountered before) or the GNOME functions will complain
     * when overwriting the existing error.
     */
    operator GError ** () { return &m_gerror; }

    /** true if error set */
    operator bool () { return m_gerror != NULL; }

    /**
     * always throws an exception, including information from GError if available:
     * <action>: <error message>|failure
     */
    void throwError(const std::string &action);
};

template<class T> void NoopDestructor(T *) {}
template<class T> void GFreeDestructor(T *ptr) { g_free(static_cast<void *>(ptr)); }

/**
 * Copies strings from a collection into a newly allocated, NULL
 * terminated array. Copying the strings is optional. Suggested
 * usage is:
 *
 * C collection;
 * collection.push_back(...);
 * boost::scoped_array<char *> array(AllocStringArray(collection));
 *
 */
template<typename T> char **AllocStringArray(const T &strings,
                                             const char **(*allocArray)(size_t) = NULL,
                                             void (*freeArray)(const char **) = NULL,
                                             const char *(*copyString)(const char *) = NULL,
                                             const void (*freeString)(char *) = NULL)
{
    size_t arraySize = strings.size() + 1;
    const char **array = NULL;
    array = allocArray ? allocArray(arraySize) : new const char *[arraySize];
    if (!array) {
        throw std::bad_alloc();
    }
    try {
        memset(array, 0, sizeof(*array) * arraySize);
        size_t i = 0;
        BOOST_FOREACH(const std::string &str, strings) {
            array[i] = copyString ? copyString(str.c_str()) : str.c_str();
            if (!array[i]) {
                throw std::bad_alloc();
            }
            i++;
        }
    } catch (...) {
        if (freeString) {
            for (const char **ptr = array;
                 *ptr;
                 ptr++) {
                freeString(const_cast<char *>(*ptr));
            }
        }
        if (freeArray) {
            freeArray(array);
        }
        throw;
    }
    return const_cast<char **>(array);
}


/**
 * Wraps a G[S]List of pointers to a specific type.
 * Can be used with boost::FOREACH and provides forward iterators
 * (two-way iterators and reverse iterators also possible, but not implemented).
 * Frees the list and optionally (not turned on by default) also frees
 * the data contained in it, using the provided destructor class.
 * Use GObjectDestructor for GObject instances.
 *
 * @param T    the type of the instances pointed to inside the list
 * @param L    GList or GSList
 * @param D    destructor function freeing a T instance
 */
template< class T, class L, void (*D)(T*) = NoopDestructor<T> > struct GListCXX : boost::noncopyable {
    L *m_list;

    static void listFree(GSList *l) { g_slist_free(l); }
    static void listFree(GList *l) { g_list_free(l); }

    static GSList *listPrepend(GSList *list, T *entry) { return g_slist_prepend(list, static_cast<gpointer>(entry)); }
    static GList *listPrepend(GList *list, T *entry) { return g_list_prepend(list, static_cast<gpointer>(entry)); }

    static GSList *listAppend(GSList *list, T *entry) { return g_slist_append(list, static_cast<gpointer>(entry)); }
    static GList *listAppend(GList *list, T *entry) { return g_list_append(list, static_cast<gpointer>(entry)); }

 public:
    typedef T * value_type;

    /** by default initialize an empty list; if parameter is not NULL,
        owership is transferred to the new instance of GListCXX */
    GListCXX(L *list = NULL) : m_list(list) {}

    /** free list */
    ~GListCXX() { clear(); }

    bool empty() { return m_list == NULL; }

    /** clear error if any is set */
    void clear() {
#if 1
        BOOST_FOREACH(T *entry, *this) {
            D(entry);
        }
#else
        for (iterator it = begin();
             it != end();
             ++it) {
            D(*it);
        }
#endif
        listFree(m_list);
        m_list = NULL;
    }

    /**
     * Use this when passing GListCXX instance to C functions which need to set it.
     * Make sure the pointer isn't set yet (new GListCXX instance or cleared).
     */
    operator L ** () { return &m_list; }

    /**
     * Cast to plain G[S]List, for use in functions which do not modify the list.
     */
    operator L * () { return m_list; }

    class iterator : public std::iterator<std::forward_iterator_tag, T *> {
        L *m_entry;
    public:
        iterator(L *list) : m_entry(list) {}
        iterator(const iterator &other) : m_entry(other.m_entry) {}
        /**
         * boost::foreach needs a reference as return code here,
         * which forces us to do type casting on the address of the void * pointer,
         * then dereference the pointer. The reason is that typecasting the
         * pointer value directly yields an rvalue, which can't be used to initialize
         * the reference return value.
         */
        T * &operator -> () const { return *getEntryPtr(); }
        T * &operator * () const { return *getEntryPtr(); }
        iterator & operator ++ () { m_entry = m_entry->next; return *this; }
        iterator operator ++ (int) { return iterator(m_entry->next); }
        bool operator == (const iterator &other) { return m_entry == other.m_entry; }
        bool operator != (const iterator &other) { return m_entry != other.m_entry; }

    private:
        /**
         * Used above, necessary to hide the fact that we do type
         * casting tricks. Otherwise the compiler will complain about
         * *(T **)&m_entry->data with "dereferencing type-punned
         * pointer will break strict-aliasing rules".
         *
         * That warning is about breaking assumptions that the compiler
         * uses for optimizations. The hope is that those optimzations
         * aren't done here, and/or are disabled by using a function.
         */
        T** getEntryPtr() const { return (T **)&m_entry->data; }
    };
    iterator begin() { return iterator(m_list); }
    iterator end() { return iterator(NULL); }

    class const_iterator : public std::iterator<std::forward_iterator_tag, T *> {
        L *m_entry;
        T *m_value;

    public:
        const_iterator(L *list) : m_entry(list) {}
        const_iterator(const const_iterator &other) : m_entry(other.m_entry) {}
        T * &operator -> () const { return *getEntryPtr(); }
        T * &operator * () const { return *getEntryPtr(); }
        const_iterator & operator ++ () { m_entry = m_entry->next; return *this; }
        const_iterator operator ++ (int) { return iterator(m_entry->next); }
        bool operator == (const const_iterator &other) { return m_entry == other.m_entry; }
        bool operator != (const const_iterator &other) { return m_entry != other.m_entry; }

    private:
        T** getEntryPtr() const { return (T **)&m_entry->data; }
    };

    const_iterator begin() const { return const_iterator(m_list); }
    const_iterator end() const { return const_iterator(NULL); }

    void push_back(T *entry) { m_list = listAppend(m_list, entry); }
    void push_front(T *entry) { m_list = listPrepend(m_list, entry); }
};

/** use this for a list which owns the strings it points to */
typedef GListCXX<char, GList, GFreeDestructor<char> > GStringListFreeCXX;
/** use this for a list which does not own the strings it points to */
typedef GListCXX<char, GList> GStringListNoFreeCXX;

#endif

SE_END_CXX

#endif // INCL_GLIB_SUPPORT

