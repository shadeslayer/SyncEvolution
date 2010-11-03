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

#ifndef INCL_EVOLUTION_STRING_DATA_BLOB
# define INCL_EVOLUTION_STRING_DATA_BLOB

#include <syncevo/DataBlob.h>
#include <boost/shared_ptr.hpp>
#include <string>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

/**
 * Stores data chunk in memory.
 * Ownership of that memory is shared.
 */
class StringDataBlob : public DataBlob
{
    std::string m_name;
    boost::shared_ptr<std::string> m_data;
    bool m_readonly;

 public:
    /**
     * @param name      name for the data blob
     * @param data      reference to string holding data, NULL pointer if it doesn't exist
     * @param readonly  true if write() is meant to fail
     */
    StringDataBlob(const std::string &name,
                   const boost::shared_ptr<std::string> &data,
                   bool readonly);

    /** writing ends and data is updated when the ostream pointer is destructed */
    virtual boost::shared_ptr<std::ostream> write();
    virtual boost::shared_ptr<std::istream> read();

    virtual boost::shared_ptr<std::string> getData() { return m_data; }
    virtual std::string getName() const { return m_name; }
    virtual bool exists() const { return m_data; }
    virtual bool isReadonly() const { return m_readonly; }
};

SE_END_CXX

#endif // INCL_EVOLUTION_STRING_DATA_BLOB
