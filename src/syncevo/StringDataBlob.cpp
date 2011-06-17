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

#include <syncevo/StringDataBlob.h>
#include <syncevo/util.h>

#include <sstream>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

class FinalizeWrite {
    boost::shared_ptr<std::string> m_data;
public:
    FinalizeWrite(const boost::shared_ptr<std::string> &data) :
        m_data(data)
    {}

    void operator() (std::ostringstream *stream)
    {
        if (stream) {
            m_data->assign(stream->str());
            delete stream;
        }
    }
};

StringDataBlob::StringDataBlob(const std::string &name,
                               const boost::shared_ptr<std::string> &data,
                               bool readonly) :
    m_name(name),
    m_data(data),
    m_readonly(readonly)
{
}
 
boost::shared_ptr<std::ostream> StringDataBlob::write()
{
    return boost::shared_ptr<std::ostringstream>(new std::ostringstream, FinalizeWrite(m_data));
}

boost::shared_ptr<std::istream> StringDataBlob::read()
{
    return boost::shared_ptr<std::istream>(new std::istringstream(m_data ? *m_data : ""));
}

SE_END_CXX
