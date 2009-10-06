/*
 * Copyright (C) 2008 Funambol, Inc.
 * Copyright (C) 2008-2009 Patrick Ohly <patrick.ohly@gmx.de>
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
 * This file is used by source files which depend on CPPUnit and thus
 * the STL => using it here is allowed.
 */

#ifndef INCL_TEST_H
#define INCL_TEST_H
/** @cond DEV */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <syncevo/declarations.h>

// These defines are set in config.h when using the autotools
// mechanism and by the project setup on Windows. They control whether
// tests inside the libraries source code (ENABLE_UNIT_TESTS) or of
// the final library are enabled (ENABLE_INTEGRATION_TESTS).
#if defined(ENABLE_UNIT_TESTS) || defined(ENABLE_INTEGRATION_TESTS)

// make common macros like CPPUNIT_TEST_ASSERT() available */
# include <cppunit/extensions/TestFactoryRegistry.h>
# include <cppunit/extensions/HelperMacros.h>
# include <string>

SE_BEGIN_CXX

// until a better solution is found use the helper function from TestMain.cpp
// to get the name of the currently running test; beware, will contain colons
extern const std::string &getCurrentTest();

// removes special characters like colons and slashes
extern void simplifyFilename(std::string &filename);

// redefine CPPUNIT_TEST so that we can filter tests
#undef CPPUNIT_TEST
#define CPPUNIT_TEST(testMethod) \
    CPPUNIT_TEST_SUITE_ADD_TEST( \
        ( FilterTest(new CPPUNIT_NS::TestCaller<TestFixtureType>( \
                     context.getTestNameFor( #testMethod), \
                     &TestFixtureType::testMethod, \
                     context.makeFixture() ) ) ) )

/**
 * replace test with dummy if filtered out via CLIENT_TEST_SKIP
 */
CppUnit::Test *FilterTest(CppUnit::Test *test);

SE_END_CXX

#endif // ENABLE_UNIT_TESTS

/** @endcond */

#endif // INCL_TEST_H
