/*
 * Copyright (C) 2005-2008 Patrick Ohly <patrick.ohly@gmx.de>
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

#ifndef INCL_SYNTHESISENGINE
#define INCL_SYNTHESISENGINE

#include <config.h>

#include <synthesis/generic_types.h>
#include <synthesis/sync_declarations.h>
#include <synthesis/engine_defs.h>
#include <synthesis/syerror.h>

// TODO: remove dependency on header file.
// Currently required because shared_ptr
// checks that type is completely defined.
#include <synthesis/enginemodulebase.h>

#include <boost/shared_ptr.hpp>
#include <boost/shared_array.hpp>
#include <stdexcept>

typedef boost::shared_ptr<sysync::SessionType> SharedSession;
typedef boost::shared_ptr<sysync::KeyType> SharedKey;
class SharedBuffer : public boost::shared_array<char>
{
    size_t m_size;
 public:
    SharedBuffer() :
        m_size(0)
        {}

    explicit SharedBuffer(char *p, size_t size):
    boost::shared_array<char>(p),
        m_size(size)
        {}

    template <class D> SharedBuffer(char *p, size_t size, const D &d) :
    boost::shared_array<char>(p, d),
        m_size(size)
        {}

    size_t size() { return m_size; }
};

/**
 * Wrapper around a class which is derived from
 * TEngineModuleBase. Provides a C++
 * interface which uses Boost smart pointers and
 * exceptions derived from std::runtime_error to track
 * resources/report errors.
 */
class SharedEngine {
    boost::shared_ptr<sysync::TEngineModuleBase> m_engine;

 public:
    SharedEngine(sysync::TEngineModuleBase *engine = NULL): m_engine(engine) {}

    sysync::TEngineModuleBase *get() { return m_engine.get(); }

    void Connect(const string &aEngineName,
                 sysync::CVersion aPrgVersion = 0,
                 sysync::uInt16 aDebugFlags = 0);
    void Disconnect();

    void InitEngineXML(const string &aConfigXML);
    SharedSession OpenSession();
    SharedKey OpenSessionKey(SharedSession &aSessionH);

    void SessionStep(const SharedSession &aSessionH,
                     sysync::uInt16 &aStepCmd,
                     sysync::TEngineProgressInfo *aInfoP = NULL);
    SharedBuffer GetSyncMLBuffer(const SharedSession &aSessionH, bool aForSend);
    void WriteSyncMLBuffer(const SharedSession &aSessionH, const char *data, size_t len);
    SharedKey OpenKeyByPath(const SharedKey &aParentKeyH,
                            const string &aPath);
    SharedKey OpenSubkey(const SharedKey &aParentKeyH,
                         sysync::sInt32 aID);

    string GetStrValue(const SharedKey &aKeyH, const string &aValName);
    void SetStrValue(const SharedKey &aKeyH, const string &aValName, const string &aValue);

    sysync::sInt32 GetInt32Value(const SharedKey &aKeyH, const string &aValName);
    void SetInt32Value(const SharedKey &aKeyH, const string &aValName, sysync::sInt32 aValue);
};

/**
 * thrown when a function returns a non-okay error code
 */
class BadSynthesisResult : public std::runtime_error
{
    sysync::TSyErrorEnum m_result;

 public:
    BadSynthesisResult(const string &what, sysync::TSyErrorEnum result) :
    std::runtime_error(what),
    m_result(result)
        {}

    sysync::TSyErrorEnum result() const { return m_result; }
};

/**
 * thrown when a key cannot be opened because it doesn't exist
 */
class NoSuchKey : public BadSynthesisResult
{
 public:
    NoSuchKey(const string &what) :
    BadSynthesisResult(what, sysync::DB_NoContent)
        {}
};

#endif // INCL_SYNTHESISENGINE
