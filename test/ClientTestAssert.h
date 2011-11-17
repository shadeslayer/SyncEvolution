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

#ifndef INCL_CLIENTTESTASSERT
#define INCL_CLIENTTESTASSERT

#include <cppunit/Exception.h>
#include <syncevo/util.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

/**
 * All of the macros below include the behavior of the corresponding
 * CPUNIT assertion. In addition they catch exceptions and turn them
 * into the following, extended CPPUnit exception by preserving the
 * information from the original exception and adding the current
 * source code line.
 *
 * Source information is listed as inner first (as part of the error
 * preamble), followed by error at that location and all locations
 * that the error passed through until caught at the top level of a
 * test.
 */
class CTException : public CppUnit::Exception
{

 public:
    CTException(const CppUnit::Message &message = CppUnit::Message(),
                const std::string &currentMessage = "",
                const CppUnit::SourceLine &currentSourceLine = CppUnit::SourceLine(),
                const CppUnit::SourceLine &previousSourceLine = CppUnit::SourceLine()) :
    CppUnit::Exception(message, previousSourceLine)
    {
        CppUnit::Message extendedMessage = message;
        if (!currentMessage.empty()) {
            extendedMessage.addDetail(currentMessage);
        }
        if (currentSourceLine.isValid()) {
            extendedMessage.addDetail(StringPrintf("%s:%d",
                                                   getBasename(currentSourceLine.fileName()).c_str(),
                                                   currentSourceLine.lineNumber()));
        }
        setMessage(extendedMessage);
    }
};

static void inline ClientTestExceptionHandle(const char *file, int line, const std::string &message = "")
{
    CppUnit::SourceLine here(file, line);
    try {
        throw;
    }  catch (const CTException &ex) {
        throw(CTException(ex.message(),
                          message,
                          here,
                          ex.sourceLine()));
    } catch (const CppUnit::Exception &ex) {
        if (ex.sourceLine() == CPPUNIT_SOURCELINE()) {
            /* failure in condition expression itself, already includes source info, pass through */
            throw;
        } else {
            throw(CTException(ex.message(),
                              message,
                              here,
                              ex.sourceLine()));
        }
    } catch (const Exception &ex) {
        throw(CTException(CppUnit::Message(ex.what()),
                          message,
                          here,
                          CppUnit::SourceLine(ex.m_file, ex.m_line)));
    } catch (const std::exception &ex) {
        CppUnit::Message msg(CPPUNIT_EXTRACT_EXCEPTION_TYPE_(ex,
                                                             "std::exception or derived"));
        msg.addDetail(std::string("What(): ") + ex.what());
        throw(CTException(msg,
                          message,
                          here));
    }
}

#define CT_WRAP_ASSERT(_file, _line, _assert) \
    do { \
       try { \
           SE_LOG_DEBUG(NULL, NULL, "%s:%d: starting %s", getBasename(_file).c_str(), _line, #_assert); \
           _assert; \
           SE_LOG_DEBUG(NULL, NULL, "%s:%d: ending %s", getBasename(_file).c_str(), _line, #_assert); \
       } catch (...) { \
           ClientTestExceptionHandle(_file, _line); \
       } \
    } while (false)

#define CT_WRAP_ASSERT_MESSAGE(_file, _line, _message, _assert)  \
    do { \
       try { \
           SE_LOG_DEBUG(NULL, NULL, "%s:%d: starting %s %s", \
                        getBasename(_file).c_str(), _line, \
                        std::string(_message).c_str(), \
                        #_assert); \
           _assert; \
           SE_LOG_DEBUG(NULL, NULL, "%s:%d: ending %s", getBasename(_file).c_str(), _line, #_assert); \
       } catch (...) { \
           ClientTestExceptionHandle(_file, _line, _message); \
       } \
    } while (false)

#define CT_ASSERT(condition) CT_WRAP_ASSERT(__FILE__, __LINE__, CPPUNIT_ASSERT(condition))
#define CT_ASSERT_NO_THROW(expression) CT_WRAP_ASSERT(__FILE__, __LINE__, expression)
#define CT_ASSERT_NO_THROW_MESSAGE(message, expression) CT_WRAP_ASSERT_MESSAGE(__FILE__, __LINE__, message, expression)
#define CT_ASSERT_MESSAGE(message,condition) CT_WRAP_ASSERT(__FILE__, __LINE__, CPPUNIT_ASSERT_MESSAGE(message,condition))
#define CT_FAIL(message) CPPUNIT_FAIL(message)
#define CT_ASSERT_EQUAL(expected,actual) CT_WRAP_ASSERT(__FILE__, __LINE__, CPPUNIT_ASSERT_EQUAL(expected,actual))
#define CT_ASSERT_EQUAL_MESSAGE(message,expected,actual) CT_WRAP_ASSERT(__FILE__, __LINE__, CPPUNIT_ASSERT_EQUAL_MESSAGE(message,expected,actual))
#define CT_ASSERT_DOUBLES_EQUAL(expected,actual,delta) CT_WRAP_ASSERT(__FILE__, __LINE__, CPPUNIT_ASSERT_DOUBLES_EQUAL(expected,actual,delta))
#define CT_ASSERT_DOUBLES_EQUAL_MESSAGE(message,expected,actual,delta) CT_WRAP_ASSERT(__FILE__, __LINE__, CPPUNIT_ASSERT_DOUBLES_EQUAL_MESSAGE(__FILE__, __LINE__, message,expected,actual,delta))

SE_END_CXX

#endif  // INCL_CLIENTTESTASSERT
