/*
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

#include <syncevo/lcs.h>
#include <syncevo/util.h>
#include <test.h>

#include <list>
#include <vector>
#include <algorithm>
#include <iterator>
#include <iostream>
#include <fstream>
#include <string>
#include <utility>
#include <sstream>

#include <config.h>
#include <syncevo/declarations.h>
SE_BEGIN_CXX

#ifdef ENABLE_UNIT_TESTS

template <class IT> void readlines(std::istream &in,
                                   IT out)
{
    std::string line;

    while (std::getline(in, line)) {
        *out++ = line;
    }
}

/**
 * implements the assignment of EnumerateChunks
 */
template <class IT, class C = int> class EnumerateChunksProxy {
public:
    EnumerateChunksProxy(const std::string &keyword, const IT &out, const C count) :
        m_keyword(keyword),
        m_out(out),
        m_count(count)
    {}

    EnumerateChunksProxy &operator=(const std::string &rhs)
    {
        if (!rhs.compare(0, m_keyword.size(), m_keyword)) {
            m_count++;
        }
        m_out = std::make_pair(rhs, m_count);
        return *this;
    }

protected:
    std::string m_keyword;
    IT m_out;
    C m_count;    
};

/**
 * Output iterator which identifies related chunks
 * by incrementing a running count each time a certain
 * keyword is found at the beginning of the line.
 * Writes the line,count pair to another output
 * iterator.
 */
template <class IT, class C = int> class EnumerateChunks : private EnumerateChunksProxy<IT, C> {
public:
    EnumerateChunks(const std::string &keyword, const IT &out, const C count) :
        EnumerateChunksProxy<IT, C>(keyword, out, count)
    {}

    EnumerateChunksProxy<IT, C> &operator*() { return *this; }
    EnumerateChunks &operator++() { this->m_out++; return *this; }
    EnumerateChunks &operator++(int) { ++this->m_out; return *this; }
};

template <class IT, class C>
class EnumerateChunks<IT, C>
make_enumerate_chunks(const std::string &keyword, IT out, C count)
{
    return EnumerateChunks<IT, C>(keyword, out, count);
}           

class LCSTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(LCSTest);
    CPPUNIT_TEST(lcs);
    CPPUNIT_TEST_SUITE_END();
 
public:
    void lcs()
    {
        std::ifstream file1("testcases/lcs/file1.txt");
        std::ifstream file2("testcases/lcs/file2.txt");
        typedef std::vector< std::pair<std::string, int> > content;
        content content1, content2;
        readlines(file1, make_enumerate_chunks("begin", std::back_inserter(content1), 0));
        readlines(file2, make_enumerate_chunks("begin", std::back_inserter(content2), 0));

        std::vector< LCS::Entry<std::string> > result;
        LCS::lcs(content1, content2, std::back_inserter(result), LCS::accessor<content>());

        std::ostringstream out;
        std::copy(result.begin(), result.end(), std::ostream_iterator< LCS::Entry<std::string> >(out));
        CPPUNIT_ASSERT_EQUAL(std::string("1, 4: begin\n"
                                         "2, 5: item1\n"
                                         "3, 6: end\n"),
                             out.str());
        CPPUNIT_ASSERT_EQUAL((size_t)3, result.size());
    }
};

SYNCEVOLUTION_TEST_SUITE_REGISTRATION(LCSTest);

#ifdef MAIN
int main(int argc, char **argv)
{
    if (argc != 3) {
        std::cerr << "Usage: lcs file1 file2" << std::endl;
        return 1;
    }

    std::ifstream file1(argv[1]);
    std::ifstream file2(argv[2]);
    typedef std::vector< std::pair<std::string, int> > content;
    content content1, content2;
    readlines(file1, make_enumerate_chunks("begin", std::back_inserter(content1), 0));
    readlines(file2, make_enumerate_chunks("begin", std::back_inserter(content2), 0));

    std::vector< LCS::Entry<std::string> > result;
    LCS::lcs(content1, content2, std::back_inserter(result), LCS::accessor<content>());

    std::copy(result.begin(), result.end(), std::ostream_iterator< LCS::Entry<std::string> >(std::cout));
    std::cout << "Length: " << result.size() << std::endl;

    return 0;
}
#endif // MAIN

#endif // ENABLE_UNIT_TESTS

SE_END_CXX
