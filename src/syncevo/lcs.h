/*
 * Copyright (C) 2008-2009 Patrick Ohly <patrick.ohly@gmx.de>
 * Copyright (C) 2010 Intel Corporation
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

#ifndef INCL_SYNCEVOLUTION_LCS
# define INCL_SYNCEVOLUTION_LCS

#include <vector>
#include <list>
#include <ostream>

// for size_t and ssize_t
#include <unistd.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX
namespace LCS {

enum Choice {
    /** default value for empty subsequences */
    NONE,
    /** i,j pair matches in both subsequences */
    MATCH,
    /** entry j is skipped in second sequence */
    LEFT,
    /** entry i is skipped in first sequence */
    UP
};

/**
 * utility struct for the lcs algorithm: describes the optimal
 * solution for a subset of the full problem
 */
template <class C> struct Sub {
    /** how the algorithm decided for the last entries of each subsequence */
    Choice choice;
    /** number of matched entries in subsequences */
    size_t length;
    /** total cost for gaps */
    C cost;
};

template <class T> struct Entry {
    Entry(size_t i, size_t j, T e) :
        index_a(i), index_b(j), element(e) {}
    size_t index_a;
    size_t index_b;
    T element;
};

template <class T> std::ostream &operator<<(std::ostream &out, const Entry<T> &entry)
{
    out << entry.index_a << ", " << entry.index_b << ": " << entry.element << std::endl;
    return out;
}

/**
 * accessor which reads from std::vector< std:pair< entry, cost > >
 */
template <class T> class accessor {
public:
    typedef typename T::value_type::first_type F;
    typedef typename T::value_type::second_type C;

    /**
     * @param a        container holding sequence of items as passed to lcs()
     * @param start    index of first item in the gap, may be -1
     * @param end      index of the last item in the gap, may be one beyond end of sequence, always >= start
     * @return cost    0 for start == end, > 0 for start < end
     */
    static C cost(const T &a, ssize_t start, size_t end) {
        return a.empty() ? 0 :
            ((end >= a.size() ? a[a.size() - 1].second  : a[end].second) -
             (start < 0 ? a[0].second : a[start].second));
    }
    /**
     * @param index    valid index (>= 0, < a.size())
     * @return entry at index
     */
    static const F &entry_at(const T &a, size_t index) { return a[index].first; }
};

/**
 * accessor which reads from an arbitrary random-access sequence,
 * using a zero cost function (to be used for original LCS)
 */
template <class T> class accessor_sequence {
public:
    typedef typename T::value_type F;
    typedef unsigned char C;

    static C cost(const T &a, ssize_t start, size_t end) { return 0; }
    static const F &entry_at(const T &a, size_t index) { return a[index]; }
};


/**
 * Calculates the longest common subsequence (LCS) of two
 * sequences stored in vectors. The result specifies the common
 * elements of that type and their positions in the two input
 * sequences and is stored in a output sequence.
 *
 * In contrast to the generic LCS algorithm from "Introduction
 * to Algorithms" (Cormen, Leiserson, Rivest), this extended
 * algorithm tries to pick "better" LCSes when more than one
 * exists.
 *
 * When the two sequences contain chunks of related entries, then
 * a "better" LCS is one where gaps go across less chunks. For
 * example, when "begin b end" is inserted in front of "begin a
 * end", then this LCS:
 *
 * begin | begin
 *       > b
 *       > end
 *       > begin
 * a     | a
 * end   | end
 *
 * is worse than:
 *
 *       > begin
 *       > b
 *       > end
 * begin | begin
 * a     | a
 * end   | end
 *
 * A monotonically increasing cost number has to be assigned to each
 * entry by the caller. The "cost" of a gap is calculated by
 * "substracting" the cost number at the beginning of the gap from the
 * cost number at the end. Both cost number and substraction are
 * template parameters.
 */
template <class T, class ITO, class A>
void lcs(const T &a, const T &b, ITO out, A access)
{
    // reserve two-dimensonal array for sub-problem solutions,
    // adding rows as we go
    typedef typename A::C C;
    std::vector< std::vector< Sub<C> > > sub;
    sub.resize(a.size() + 1);
    for (size_t i = 0; i <= a.size(); i++) {
        sub[i].resize(b.size() + 1);
        for (size_t j = 0; j <= b.size(); j++) {
            if (i == 0 || j == 0) {
                sub[i][j].choice = NONE;
                sub[i][j].length = 0;
                sub[i][j].cost = 0;
            } else if (access.entry_at(a, i - 1) == access.entry_at(b, j - 1)) {
                Choice choice = MATCH;
                size_t length = sub[i-1][j-1].length + 1;
                C cost = sub[i-1][j-1].cost;
                C cost_left = sub[i][j-1].cost += access.cost(b, j-1, j);
                C cost_up = sub[i-1][j].cost += access.cost(a, i-1, i);

                /*
                 * We may decide to not match at i,j if the
                 * alternatives have the same length but lower
                 * cost. Matching is the default.
                 */
                if (sub[i][j-1].length > sub[i-1][j].length &&
                    length == sub[i][j-1].length &&
                    cost > cost_left) {
                    /* skipping j is cheaper */
                    choice = LEFT;
                    cost = cost_left;
                } else if (sub[i][j-1].length < sub[i-1][j].length &&
                           length == sub[i-1][j].length &&
                           cost > cost_up) {
                    /* skipping i is cheaper */
                    choice = UP;
                    cost = cost_up;
                } else if (sub[i][j-1].length == sub[i-1][j].length &&
                           length == sub[i-1][j].length) {
                    if (cost_left < cost_up) {
                        choice = LEFT;
                        cost = cost_left;
                    } else {
                        choice = UP;
                        cost = cost_up;
                    }
                }
                sub[i][j].choice = choice;
                sub[i][j].length = length;
                sub[i][j].cost = cost;
            } else if (sub[i][j-1].length > sub[i-1][j].length) {
                sub[i][j].choice = LEFT;
                sub[i][j].length = sub[i][j-1].length;
                sub[i][j].cost = sub[i][j-1].cost + access.cost(b, j-1, j);
            } else if (sub[i][j-1].length < sub[i-1][j].length) {
                sub[i][j].choice = UP;
                sub[i][j].length = sub[i-1][j].length;
                sub[i][j].cost = sub[i-1][j].cost + access.cost(a, i-1, i);
            } else {
                // tie: decide based on cost
                C cost_left = sub[i][j-1].cost += access.cost(b, j-1, j);
                C cost_up = sub[i-1][j].cost += access.cost(a, i-1, i);

                if (cost_left < cost_up) {
                    sub[i][j].choice = LEFT;
                    sub[i][j].length = sub[i][j-1].length;
                    sub[i][j].cost = cost_left;
                } else {
                    sub[i][j].choice = UP;
                    sub[i][j].length = sub[i-1][j].length;
                    sub[i][j].cost = cost_up;
                }
            }
        }
    }

    // copy result (using intermediate list instead of recursive function call)
    typedef std::list< std::pair<size_t, size_t> > indexlist;
    std::list< std::pair<size_t, size_t> > indices;
    size_t i = a.size(), j = b.size();
    while (i > 0 && j > 0) {
        switch (sub[i][j].choice) {
        case MATCH:
            indices.push_front(std::make_pair(i, j));
            i--;
            j--;
            break;
        case LEFT:
            j--;
            break;
        case UP:
            i--;
            break;
        case NONE:
            // not reached
            break;
        }
    }
    
    for (indexlist::iterator it = indices.begin();
         it != indices.end();
         it++) {
        *out++ = Entry<typename A::F>(it->first, it->second, access.entry_at(a, it->first - 1));
    }
}

} // namespace lcs
SE_END_CXX

#endif // INCL_SYNCEVOLUTION_LCS
