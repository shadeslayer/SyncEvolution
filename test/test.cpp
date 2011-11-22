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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#if defined(ENABLE_INTEGRATION_TESTS) || defined(ENABLE_UNIT_TESTS)

#include "test.h"

#include <stdlib.h>
#include <iostream>

#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/foreach.hpp>

#include <pcrecpp.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

class SkipTest : public CppUnit::TestCase {
public:
    SkipTest(const std::string &name) :
        TestCase(name)
    {}
    void run (CppUnit::TestResult *result) {
        std::cerr << getName() << " *** skipped ***\n";
    }
};

CppUnit::Test *FilterTest(CppUnit::Test *test)
{
    static std::set<std::string> filter;
    static bool filterValid;

    if (!filterValid) {
        const char *str = getenv("CLIENT_TEST_SKIP");
        if (str) {
            boost::split(filter, str, boost::is_any_of(","));
        }
        filterValid = true;
    }

    std::string name = test->getName();
    BOOST_FOREACH (const std::string &re, filter) {
        if (pcrecpp::RE(re).FullMatch(name)) {
            delete test;
            return new SkipTest(name);
        }
    }

    return test;
}

SE_END_CXX

#endif // defined(ENABLE_INTEGRATION_TESTS) || defined(ENABLE_UNIT_TESTS)


