/*
 * Copyright (C) 2008-2009 Patrick Ohly <patrick.ohly@gmx.de>
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

#ifndef INCL_EVOLUTION_DATA_BLOB
# define INCL_EVOLUTION_DATA_BLOB

#include <iostream>
#include <boost/shared_ptr.hpp>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

/**
 * Abstract base class for a chunk of data.
 * Can be opened for reading and writing.
 * Meant to be used for plain files and
 * for sections inside a larger file.
 */
class DataBlob
{
 public:
    virtual ~DataBlob() {}

    /**
     * Create stream for writing data.
     * Always overwrites old data.
     */
    virtual boost::shared_ptr<std::ostream> write() = 0;

    /**
     * Create stream for reading data.
     */
    virtual boost::shared_ptr<std::istream> read() = 0;

    /** some kind of user visible name for the data */
    virtual std::string getName() const = 0;

    /** true if the data exists already */
    virtual bool exists() const = 0;

    /** true if the data is read-only and write() will fail */
    virtual bool isReadonly() const = 0;
};

SE_END_CXX

#endif // INCL_EVOLUTION_DATA_BLOB
