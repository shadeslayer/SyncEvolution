/*
 * Copyright (C) 2005 Patrick Ohly
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
#include <glib-object.h>
#include <libebook/e-book.h>

/**
 * a smart pointer implementation for objects for which
 * a unref() function exists;
 * trying to store a NULL pointer raises an exception,
 * unreferencing valid objects is done automatically
 */
template<class T, class base = T> class gptr {
    T *m_pointer;

    /** do not allow copy construction */
    gptr( const gptr &other) {};

    /** do not allow copying */
    void operator = ( const gptr &other ) {}

    void unref( char *pointer ) { free( pointer ); }
    void unref( GObject *pointer ) { g_object_unref( pointer ); }
    void unref( EBookQuery *pointer ) { e_book_query_unref( pointer ); }
#if 0
    void unref( EBook *pointer ) { g_object_unref( pointer ); }
    void unref( EContact *pointer ) { g_object_unref( pointer ); }
#endif

  public:
    /**
     * create a smart pointer that owns the given object;
     * passing a NULL pointer and a name for the object raises an error
     */
    gptr(T *pointer = NULL, const char *objectName = NULL) :
        m_pointer( pointer )
    {
        if (!pointer && objectName ) {
            string *str = new string("Error allocating ");
            *str += objectName;
            throw str;
        }
    };
    ~gptr()
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
            string *str = new string("Error allocating ");
            *str += objectName;
            throw str;
        }
        m_pointer = pointer;
    }

    gptr<T, base> &operator = ( T *pointer ) { set( pointer ); return *this; }
    T *operator-> () { return m_pointer; }
    T &operator* ()  { return *m_pointer; }
    operator T * () { return m_pointer; }
    operator void * () { return (void *)m_pointer; }
    operator bool () { return m_pointer != NULL; }
};

#endif
