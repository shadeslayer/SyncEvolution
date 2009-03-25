/*
 * Copyright (C) 2008 Funambol, Inc.
 * Copyright (C) 2008, 2009 Patrick Ohly <patrick.ohly.de>
 */

/**
 * This file is used by source files which depend on CPPUnit and thus
 * the STL => using it here is allowed.
 */

#ifndef INCL_TEST_H
#define INCL_TEST_H
/** @cond DEV */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

// These defines are set in config.h when using the autotools
// mechanism and by the project setup on Windows. They control whether
// tests inside the libraries source code (ENABLE_UNIT_TESTS) or of
// the final library are enabled (ENABLE_INTEGRATION_TESTS).
#if defined(ENABLE_UNIT_TESTS) || defined(ENABLE_INTEGRATION_TESTS)

// make common macros like CPPUNIT_TEST_ASSERT() available */
# include <cppunit/extensions/TestFactoryRegistry.h>
# include <cppunit/extensions/HelperMacros.h>
# include <string>

// use this macros instead of the plain CPPUNIT_ one to ensure
// that the autotools Makefile and Visual Studio helper script can find
// all tests and link them into the test binary
#define FUNAMBOL_TEST_SUITE_REGISTRATION( ATestFixtureType ) \
    CPPUNIT_TEST_SUITE_REGISTRATION( ATestFixtureType ); \
    extern "C" { int funambolAutoRegisterRegistry ## ATestFixtureType = 12345; }


// until a better solution is found use the helper function from TestMain.cpp
// to get the name of the currently running test; beware, will contain colons
extern const std::string &getCurrentTest();

// removes special characters like colons and slashes
extern void simplifyFilename(std::string &filename);

#endif // ENABLE_UNIT_TESTS

/** @endcond */
#endif // INCL_TEST_H
