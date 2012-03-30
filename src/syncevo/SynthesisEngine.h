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

#include <syncevo/Logging.h>

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
#include <boost/scoped_array.hpp>
#include <stdexcept>

#include <syncevo/declarations.h>
#include <syncevo/util.h>
SE_BEGIN_CXX

typedef boost::shared_ptr<sysync::SessionType> SharedSession;
typedef boost::shared_ptr<sysync::KeyType> SharedKey;
class SharedBuffer : public boost::shared_array<char>
{
    size_t m_size;
 public:
    SharedBuffer() :
        m_size(0)
        {}

    /** transfers ownership */
    explicit SharedBuffer(char *p, size_t size):
    boost::shared_array<char>(p),
        m_size(size)
        {}

    /** transfers ownership with custom destructor */
    template <class D> SharedBuffer(char *p, size_t size, const D &d) :
    boost::shared_array<char>(p, d),
        m_size(size)
        {}

    /** copies memory */
    explicit SharedBuffer(const char *p, size_t size):
    boost::shared_array<char>(new char [size]),
        m_size(size)
        { memcpy(get(), p, size); }

    size_t size() const { return m_size; }
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
    SharedSession OpenSession(const string &aSessionID);
    SharedKey OpenSessionKey(SharedSession &aSessionH);

    void SessionStep(const SharedSession &aSessionH,
                     sysync::uInt16 &aStepCmd,
                     sysync::TEngineProgressInfo *aInfoP = NULL);
    SharedBuffer GetSyncMLBuffer(const SharedSession &aSessionH, bool aForSend);
    void WriteSyncMLBuffer(const SharedSession &aSessionH, const char *data, size_t len);
    SharedKey OpenKeyByPath(const SharedKey &aParentKeyH,
                            const string &aPath,
                            bool noThrow = false);
    SharedKey OpenSubkey(const SharedKey &aParentKeyH,
                         sysync::sInt32 aID,
                         bool noThrow = false);

    string GetStrValue(const SharedKey &aKeyH, const string &aValName);
    void SetStrValue(const SharedKey &aKeyH, const string &aValName, const string &aValue);

    sysync::sInt32 GetInt32Value(const SharedKey &aKeyH, const string &aValName);
    void SetInt32Value(const SharedKey &aKeyH, const string &aValName, sysync::sInt32 aValue);

    void doDebug(Logger::Level level,
                 const char *prefix,
                 const char *file,
                 int line,
                 const char *function,
                 const char *format,
                 va_list args);
};

/**
 * thrown when a function returns a non-okay error code
 */
class BadSynthesisResult : public StatusException
{
 public:
    BadSynthesisResult(const std::string &file,
                       int line,
                       const string &what,
                       sysync::TSyErrorEnum result) 
        : StatusException(file, line, what, SyncMLStatus(result))
    {}

    sysync::TSyErrorEnum result() const { return sysync::TSyErrorEnum(syncMLStatus()); }
};

/**
 * thrown when a key cannot be opened because it doesn't exist
 */
class NoSuchKey : public BadSynthesisResult
{
 public:
    NoSuchKey(const std::string &file, int line, const string &what) :
    BadSynthesisResult(file, line, what, sysync::DB_NoContent)
        {}
};

/**
 * A class which wraps the underlying sysync::SDK_InterfaceType
 * methods. Any sysync::SDK_InterfaceType pointer can be casted
 * into this class because this class doesn't add any virtual
 * functions or data.
 */
struct SDKInterface : public sysync::SDK_InterfaceType
{
    sysync::TSyError setValue(sysync::KeyH aItemKey,
                              const std::string &field,
                              const char *data,
                              size_t datalen);
    sysync::TSyError getValue(sysync::KeyH aItemKey,
                              const std::string &field,
                              SharedBuffer &data);
};


SE_END_CXX
#endif // INCL_SYNTHESISENGINE
