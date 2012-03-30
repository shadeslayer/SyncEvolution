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

#ifndef SYNCEVO_EXCEPTIONS_H
#define SYNCEVO_EXCEPTIONS_H

#include <syncevo/util.h>
#include "config.h"

// Can't use the GDBusCXX::message_type typedef here because it's not
// defined till we include gdbus-cxx-bridge.h below.
#ifdef WITH_GIO_GDBUS
// Forward decleration can't be used here due to later typedef _GDBusMessage GDBusMessage.
#include <gio/gio.h>
#else
struct DBusMessage;
#endif

SE_BEGIN_CXX
#ifdef WITH_GIO_GDBUS
GDBusMessage *SyncEvoHandleException(GDBusMessage *msg);
#else
DBusMessage  *SyncEvoHandleException(DBusMessage *msg);
#endif
SE_END_CXX
// This needs to be defined before including gdbus-cxx-bridge.h!
#define DBUS_CXX_EXCEPTION_HANDLER SyncEvo::SyncEvoHandleException
#include "gdbus-cxx-bridge.h"

#include <syncevo/declarations.h>
SE_BEGIN_CXX

class DBusSyncException : public GDBusCXX::DBusCXXException, public Exception
{
 public:
    DBusSyncException(const std::string &file,
                      int line,
                      const std::string &what) : Exception(file, line, what)
    {}
    /**
     * get exception name, used to convert to dbus error name
     * subclasses should override it
     */
    virtual std::string getName() const { return "org.syncevolution.Exception"; }

    virtual const char* getMessage() const { return Exception::what(); }
};

/**
 * exceptions classes deriving from DBusException
 * org.syncevolution.NoSuchConfig
 */
class NoSuchConfig: public DBusSyncException
{
 public:
    NoSuchConfig(const std::string &file,
                 int line,
                 const std::string &error): DBusSyncException(file, line, error)
    {}
    virtual std::string getName() const { return "org.syncevolution.NoSuchConfig";}
};

/**
 * org.syncevolution.NoSuchSource
 */
class NoSuchSource : public DBusSyncException
{
 public:
    NoSuchSource(const std::string &file,
                 int line,
                 const std::string &error): DBusSyncException(file, line, error)
    {}
    virtual std::string getName() const { return "org.syncevolution.NoSuchSource";}
};

/**
 * org.syncevolution.InvalidCall
 */
class InvalidCall : public DBusSyncException
{
 public:
    InvalidCall(const std::string &file,
                 int line,
                 const std::string &error): DBusSyncException(file, line, error)
    {}
    virtual std::string getName() const { return "org.syncevolution.InvalidCall";}
};

/**
 * org.syncevolution.SourceUnusable
 * CheckSource will use this when the source cannot be used for whatever reason
 */
class SourceUnusable : public DBusSyncException
{
 public:
    SourceUnusable(const std::string &file,
                   int line,
                   const std::string &error): DBusSyncException(file, line, error)
    {}
    virtual std::string getName() const { return "org.syncevolution.SourceUnusable";}
};

SE_END_CXX

#endif // SYNCEVO_EXCEPTIONS_H
