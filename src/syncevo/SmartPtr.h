/*
 * Copyright (C) 2005-2009 Patrick Ohly <patrick.ohly@gmx.de>
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

#ifndef INCL_EVOLUTION_SMART_POINTER
#define INCL_EVOLUTION_SMART_POINTER

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <syncevo/eds_abi_wrapper.h>

#ifdef HAVE_GLIB
# include <glib-object.h>
#endif

#include <stdlib.h>
#include <stdexcept>
#include <string>
#include <memory>

#include <boost/scoped_array.hpp>
#include <boost/shared_ptr.hpp>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

template<class T> class UnrefFree {
 public:
    static void unref(T *pointer) { free(pointer); }
};

class Unref {
 public:
    /**
     * C character string - beware, Funambol C++ client library strings must
     * be returned with delete [], use boost::scoped_array
     */
    static void unref(char *pointer) { free(pointer); }

#ifdef HAVE_GLIB
    static void unref(GObject *pointer) { g_object_unref(pointer); }
    /** free a list of GObject and the objects */
    static void unref(GList *pointer) {
        if (pointer) {
            GList *next = pointer;
            do {
                g_object_unref(G_OBJECT(next->data));
                next = next->next;
            } while (next);
            g_list_free(pointer);
        }
    }
#ifdef ENABLE_EBOOK
    static void unref(EBookQuery *pointer) { e_book_query_unref(pointer); }
#endif // ENABLE_EBOOK
#endif // HAVE_GLIB
#ifdef ENABLE_ICAL
    static void unref(icalcomponent *pointer) { icalcomponent_free(pointer); }
    static void unref(icalproperty *pointer) { icalproperty_free(pointer); }
    static void unref(icalparameter *pointer) { icalparameter_free(pointer); }
    static void unref(icaltimezone *pointer) { icaltimezone_free(pointer, 1); }
#endif // ENABLE_ICAL
};

#ifdef HAVE_GLIB
class UnrefGLibEvent {
 public:
    static void unref(guint event) { g_source_remove(event); }
};
class UnrefGString {
 public:
    static void unref(gchar *ptr) { g_free(ptr); }
};
#endif // HAVE_GLIB

/**
 * a smart pointer implementation for objects for which
 * a unref() function exists inside R;
 * trying to store a NULL object raises an exception,
 * unreferencing valid objects is done automatically
 */
template<class T, class base = T, class R = Unref>
class SmartPtr
{
 protected:
    T m_pointer;
    
  public:
    /**
     * create a smart pointer that owns the given object;
     * passing a NULL pointer and a name for the object raises an error
     */
    SmartPtr(T pointer = 0, const char *objectName = NULL) :
        m_pointer( pointer )
    {
        if (!pointer && objectName ) {
            throw std::runtime_error(std::string("Error allocating ") + objectName);
        }
    };
    ~SmartPtr()
    {
        set(0);
    }

    /** assignment and copy construction transfer ownership to the copy */
    SmartPtr(SmartPtr &other) {
        m_pointer = other.m_pointer;
        other.m_pointer = 0;
    }
    SmartPtr & operator = (SmartPtr &other) {
        if (this != &other) {
            set (other.m_pointer);
            other.m_pointer = 0;
        }
        return *this;
    }

    /**
     * store another object in this pointer, replacing any which was
     * referenced there before;
     * passing a NULL pointer and a name for the object raises an error
     */
    void set( T pointer, const char *objectName = NULL )
    {
        if (m_pointer) {
            R::unref((base)m_pointer);
        }
        if (!pointer && objectName) {
            throw std::runtime_error(std::string("Error allocating ") + objectName);
        }
        m_pointer = pointer;
    }

    /**
     * transfer ownership over the pointer to caller and stop tracking it:
     * pointer tracked by smart pointer is set to NULL and the original
     * pointer is returned
     */
    T release() { T res = m_pointer; m_pointer = 0; return res; }

    SmartPtr<T, base, R> &operator = ( T pointer ) { set( pointer ); return *this; }
    T get() { return m_pointer; }
    T operator-> () { return m_pointer; }
    // T &operator* ()  { return *m_pointer; }
    operator T  () { return m_pointer; }
    operator void * () { return (void *)m_pointer; }
    operator bool () { return m_pointer != 0; }
};

template<class T, class base = T, class R = Unref > class eptr :
    public SmartPtr<T *, base *, R>
{
    typedef SmartPtr<T *, base *, R> base_t;
 public:
    eptr(T *pointer = NULL, const char *objectName = NULL) :
        base_t(pointer, objectName)
    {
    }

    eptr<T, base, R> &operator = ( T *pointer ) {
        base_t::set(pointer);
        return *this;
    }
};

template <class T> class CxxUnref {
 public:
    static void unref(T *pointer) { delete pointer; }
};

/** eptr for normal C++ objects */
template <class T> class cxxptr : public eptr<T, T, CxxUnref<T> > {
 public:
    cxxptr(T *pointer = NULL, const char *objectName = NULL) :
        eptr<T, T, CxxUnref<T> > (pointer, objectName)
    {
    };
};

template <class T> class ArrayUnref {
 public:
    static void unref(T *pointer) { delete [] pointer; }
};
    

/** eptr for array of objects or types */
template <class T> class arrayptr : public eptr<T, T, ArrayUnref<T> > {
 public:
    arrayptr(T *pointer = NULL, const char *objectName = NULL) :
        eptr<T, T, ArrayUnref<T> > (pointer, objectName)
    {
    };
};

#ifdef HAVE_GLIB
/** eptr for glib event handle - not reference counted, owned by at most one instance */
typedef SmartPtr<guint, guint, UnrefGLibEvent> GLibEvent;
typedef SmartPtr<gchar *, gchar *, UnrefGString> GStringPtr;
// for GMainLoop see GLibSupport.h
#endif

SE_END_CXX
#endif
