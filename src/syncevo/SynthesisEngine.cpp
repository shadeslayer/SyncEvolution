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

#include <syncevo/SynthesisEngine.h>
#include <syncevo/util.h>

#include <synthesis/SDK_util.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

void SharedEngine::Connect(const string &aEngineName,
                           sysync::CVersion aPrgVersion,
                           sysync::uInt16 aDebugFlags)
{
    sysync::TSyError err = m_engine->Connect(aEngineName, aPrgVersion, aDebugFlags);
    if (err) {
        throw BadSynthesisResult(std::string("cannot connect to engine '") + aEngineName + "'", static_cast<sysync::TSyErrorEnum>(err));
    }
}

void SharedEngine::Disconnect()
{
    sysync::TSyError err = m_engine->Disconnect();
    if (err) {
        throw BadSynthesisResult("cannot disconnect engine", static_cast<sysync::TSyErrorEnum>(err));
    }
}

void SharedEngine::InitEngineXML(const string &aConfigXML)
{
    sysync::TSyError err = m_engine->InitEngineXML(aConfigXML.c_str());
    if (err) {
        throw BadSynthesisResult("Synthesis XML config parser error", static_cast<sysync::TSyErrorEnum>(err));
    }
}

class FreeEngineItem {
    SharedEngine m_engine;
public:
    FreeEngineItem(const SharedEngine &engine) :
        m_engine(engine)
    {}
    void operator () (sysync::KeyH key) {
        m_engine.get()->CloseKey(key);
    }
    void operator () (sysync::SessionH session) {
        m_engine.get()->CloseSession(session);
    }
};


SharedSession SharedEngine::OpenSession(const string &aSessionID)
{
    sysync::SessionH sessionH = NULL;
    sysync::TSyError err = m_engine->OpenSession(sessionH, 0,
                                                 aSessionID.empty() ? NULL : aSessionID.c_str());
    if (err) {
        throw BadSynthesisResult("opening session failed", static_cast<sysync::TSyErrorEnum>(err));
    }
    return SharedSession(sessionH, FreeEngineItem(*this));
}

SharedKey SharedEngine::OpenSessionKey(SharedSession &aSessionH)
{
    sysync::KeyH key;
    sysync::TSyError err = m_engine->OpenSessionKey(aSessionH.get(), key, 0);
    if (err) {
        throw BadSynthesisResult("opening session key failed", static_cast<sysync::TSyErrorEnum>(err));
    }
    return SharedKey(key, FreeEngineItem(*this));
}

void SharedEngine::SessionStep(const SharedSession &aSessionH,
                               sysync::uInt16 &aStepCmd,
                               sysync::TEngineProgressInfo *aInfoP)
{
    sysync::TSyError err = m_engine->SessionStep(aSessionH.get(),
                                                 aStepCmd,
                                                 aInfoP);
    if (err) {
        throw BadSynthesisResult("proceeding with session failed", static_cast<sysync::TSyErrorEnum>(err));
    }
}

class FreeSyncMLBuffer {
    SharedEngine m_engine;
    SharedSession m_session;
    bool m_forSend;
    size_t m_size;
public:
    FreeSyncMLBuffer(const SharedEngine &engine,
                     const SharedSession &session,
                     bool forSend,
                     size_t size) :
        m_engine(engine),
        m_session(session),
        m_forSend(forSend),
        m_size(size)
    {}
    void operator () (char *buffer) {
        m_engine.get()->RetSyncMLBuffer(m_session.get(), m_forSend, m_size);
    }
};

SharedBuffer SharedEngine::GetSyncMLBuffer(const SharedSession &aSessionH, bool aForSend)
{
    sysync::appPointer buffer;
    sysync::memSize bufSize;
    sysync::TSyError err = m_engine->GetSyncMLBuffer(aSessionH.get(),
                                                     aForSend,
                                                     buffer, bufSize);
    if (err) {
        throw BadSynthesisResult("acquiring SyncML buffer failed", static_cast<sysync::TSyErrorEnum>(err));
    }

    return SharedBuffer((char *)buffer, (size_t)bufSize,
                        FreeSyncMLBuffer(*this, aSessionH, aForSend, bufSize));
}

void SharedEngine::WriteSyncMLBuffer(const SharedSession &aSessionH, const char *data, size_t len)
{
    sysync::TSyError err = m_engine->WriteSyncMLBuffer(aSessionH.get(), const_cast<char *>(data), len);
    if (err) {
        throw BadSynthesisResult("writing SyncML buffer failed", static_cast<sysync::TSyErrorEnum>(err));
    }
}

SharedKey SharedEngine::OpenKeyByPath(const SharedKey &aParentKeyH,
                                      const string &aPath,
                                      bool noThrow)
{
    sysync::KeyH key = NULL;
    sysync::TSyError err = m_engine->OpenKeyByPath(key, aParentKeyH.get(), aPath.c_str(), 0);
    if (err && noThrow) {
        return SharedKey();
    }
    if (err) {
        string what = "opening key ";
        what += aPath;
        if (err == sysync::DB_NoContent) {
            throw NoSuchKey(what);
        } else {
            throw BadSynthesisResult(what, static_cast<sysync::TSyErrorEnum>(err));
        }
    }
    return SharedKey(key, FreeEngineItem(*this));
}

SharedKey SharedEngine::OpenSubkey(const SharedKey &aParentKeyH,
                                   sysync::sInt32 aID,
                                   bool noThrow)
{
    sysync::KeyH key = NULL;
    sysync::TSyError err = m_engine->OpenSubkey(key, aParentKeyH.get(), aID, 0);
    if (err && noThrow) {
        return SharedKey();
    }
    if (err) {
        string what = "opening sub key";
        if (err == sysync::DB_NoContent) {
            throw NoSuchKey(what);
        } else {
            throw BadSynthesisResult(what, static_cast<sysync::TSyErrorEnum>(err));
        }
    }
    return SharedKey(key, FreeEngineItem(*this));
}

string SharedEngine::GetStrValue(const SharedKey &aKeyH, const string &aValName)
{
    std::string s;
    sysync::TSyError err = m_engine->GetStrValue(aKeyH.get(), aValName.c_str(), s);
    if (err) {
        throw BadSynthesisResult(string("error reading value ") + aValName, static_cast<sysync::TSyErrorEnum>(err));
    }
    return s;
}

void SharedEngine::SetStrValue(const SharedKey &aKeyH, const string &aValName, const string &aValue)
{
    sysync::TSyError err = m_engine->SetStrValue(aKeyH.get(), aValName.c_str(), aValue);
    if (err) {
        throw BadSynthesisResult(string("error writing value ") + aValName, static_cast<sysync::TSyErrorEnum>(err));
    }
}

sysync::sInt32 SharedEngine::GetInt32Value(const SharedKey &aKeyH, const string &aValName)
{
    sysync::sInt32 v;
    sysync::TSyError err = m_engine->GetInt32Value(aKeyH.get(), aValName.c_str(), v);
    if (err) {
        throw BadSynthesisResult(string("error reading value ") + aValName, static_cast<sysync::TSyErrorEnum>(err));
    }
    return v;
}

void SharedEngine::SetInt32Value(const SharedKey &aKeyH, const string &aValName, sysync::sInt32 aValue)
{
    sysync::TSyError err = m_engine->SetInt32Value(aKeyH.get(), aValName.c_str(), aValue);
    if (err) {
        throw BadSynthesisResult(string("error writing value ") + aValName, static_cast<sysync::TSyErrorEnum>(err));
    }
}

void SharedEngine::doDebug(Logger::Level level,
                           const char *prefix,
                           const char *file,
                           int line,
                           const char *function,
                           const char *format,
                           va_list args)
{
    std::string str = StringPrintfV(format, args);
    SySyncDebugPuts(m_engine->fCI, file, line, function,
                    level <= Logger::ERROR ? DBG_ERROR :
                    level <= Logger::INFO ? DBG_HOT :
                    0, prefix,
                    str.c_str());
}


sysync::TSyError SDKInterface::setValue(sysync::KeyH aItemKey,
                                        const std::string &field,
                                        const char *data,
                                        size_t datalen)
{
    return this->ui.SetValue(this,
                             aItemKey,
                             field.c_str(),
                             sysync::VALTYPE_TEXT,
                             data,
                             datalen);
}

sysync::TSyError SDKInterface::getValue(sysync::KeyH aItemKey,
                                        const std::string &field,
                                        SharedBuffer &data)
{
    sysync::memSize len;
    TSyError res =
        this->ui.GetValue(this,
                          aItemKey,
                          field.c_str(),
                          sysync::VALTYPE_TEXT,
                          NULL, 0,
                          &len);
    if (!res) {
        data = SharedBuffer(new char[len + 1], len);
        data[len] = 0;
        res = this->ui.GetValue(this,
                                aItemKey,
                                field.c_str(),
                                sysync::VALTYPE_TEXT,
                                data.get(), len + 1,
                                &len);
    }

    return res;
}

SE_END_CXX
