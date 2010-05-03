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

#include <syncevo/FileDataBlob.h>
#include <syncevo/SafeOstream.h>
#include <syncevo/util.h>

#include <unistd.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

FileDataBlob::FileDataBlob(const std::string &path, const std::string &fileName, bool readonly) :
    m_path(path),
    m_fileName(fileName),
    m_readonly(readonly)
{
}

FileDataBlob::FileDataBlob(const std::string &fullpath, bool readonly) :
    m_readonly(readonly)
{
    splitPath(fullpath, m_path, m_fileName);
}

boost::shared_ptr<std::ostream> FileDataBlob::write()
{
    if (m_readonly) {
        SE_THROW(getName() + ": internal error: attempt to write read-only FileDataBlob");
    }

    mkdir_p(m_path);

    boost::shared_ptr<std::ostream> file(new SafeOstream(getName()));
    return file;
}

boost::shared_ptr<std::istream> FileDataBlob::read()
{
    boost::shared_ptr<std::istream> file(new std::ifstream(getName().c_str()));
    return file;
}

std::string FileDataBlob::getName() const
{
    return m_path + "/" + m_fileName;
}

bool FileDataBlob::exists() const
{
    std::string fullname = getName();
    return !access(fullname.c_str(), F_OK);
}

SE_END_CXX
