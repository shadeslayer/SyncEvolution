/*
 * Copyright (C) 2005-2006 Patrick Ohly
 * Copyright (C) 2007 Funambol
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef INCL_EVOLUTION_SMART_POINTER
# define INCL_EVOLUTION_SMART_POINTER

#include <stdlib.h>
#ifdef HAVE_EDS
#include <glib-object.h>
#ifdef ENABLE_EBOOK
#include <libebook/e-book.h>
#endif
#ifdef ENABLE_ECAL
#include <libecal/e-cal.h>
#endif
#endif

#include <stdexcept>
#include <string>

void inline unref( char *pointer ) { free( pointer ); }
#ifdef HAVE_EDS
void inline unref( GObject *pointer ) { g_object_unref( pointer ); }
#ifdef ENABLE_EBOOK
void inline unref( EBookQuery *pointer ) { e_book_query_unref( pointer ); }
#endif
#ifdef ENABLE_ECAL
void inline unref( icalcomponent *pointer ) { icalcomponent_free( pointer ); }
void inline unref( icaltimezone *pointer ) { icaltimezone_free( pointer, 1 ); }
#endif
#if 0
void inline unref( EBook *pointer ) { g_object_unref( pointer ); }
void inline unref( EContact *pointer ) { g_object_unref( pointer ); }
#endif
#endif
template <class T> void unref(T *pointer) { delete pointer; }

/**
 * a smart pointer implementation for objects for which
 * a unref() function exists;
 * trying to store a NULL pointer raises an exception,
 * unreferencing valid objects is done automatically
 */
template<class T, class base = T> class eptr {
    /** do not allow copy construction */
    eptr( const eptr &other) {};

    /** do not allow copying */
    void operator = ( const eptr &other ) {}

 protected:
    T *m_pointer;
    
  public:
    /**
     * create a smart pointer that owns the given object;
     * passing a NULL pointer and a name for the object raises an error
     */
    eptr(T *pointer = NULL, const char *objectName = NULL) :
        m_pointer( pointer )
    {
        if (!pointer && objectName ) {
            throw std::runtime_error(std::string("Error allocating ") + objectName);
        }
    };
    ~eptr()
    {
        set( NULL );
    }

    /**
     * store another object in this pointer, replacing any which was
     * referenced there before;
     * passing a NULL pointer and a name for the object raises an error
     */
    void set( T *pointer, const char *objectName = NULL )
    {
        if (m_pointer) {
            unref( (base *)m_pointer );
        }
        if (!pointer && objectName) {
            throw std::runtime_error(std::string("Error allocating ") + objectName);
        }
        m_pointer = pointer;
    }

    eptr<T, base> &operator = ( T *pointer ) { set( pointer ); return *this; }
    T *operator-> () { return m_pointer; }
    T &operator* ()  { return *m_pointer; }
    operator T * () { return m_pointer; }
    operator void * () { return (void *)m_pointer; }
    operator bool () { return m_pointer != NULL; }
};

/**
 * a eptr for C++ arrays: everything is unref'ed via delete []
 */
template <class T> class arrayptr : public eptr<T> {
    void unref( T *pointer) { delete [] pointer; }
    
  public:
    arrayptr(T *pointer = NULL, const char *objectName = NULL) :
        eptr<T>(pointer, objectName)
    {}
    ~arrayptr()
    {
        set(NULL);
    }
    arrayptr<T> &operator = ( T *pointer ) { set( pointer ); return *this; }

    /**
     * has to be duplicated, base class does not pick up our unref()
     * above otherwise
     */
    void set( T *pointer, const char *objectName = NULL )
    {
        if (this->m_pointer) {
            unref(this->m_pointer);
        }
        if (!pointer && objectName) {
            throw std::runtime_error(std::string("Error allocating ") + objectName);
        }
        this->m_pointer = pointer;
    }
        
};

#endif
