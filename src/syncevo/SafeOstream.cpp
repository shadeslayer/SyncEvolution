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

#include <syncevo/SafeOstream.h>
#include <syncevo/SyncContext.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

SafeOstream::SafeOstream(const std::string filename) :
    m_filename(filename)
{
    size_t pos = filename.rfind('/');
    if (pos == filename.npos) {
        m_tmpFilename = ".#";
        m_tmpFilename += filename;
    } else {
        m_tmpFilename.assign(filename, 0, pos + 1);
        m_tmpFilename += ".#";
        m_tmpFilename += filename.substr(pos + 1);
    }
    open(m_tmpFilename.c_str());
}

SafeOstream::~SafeOstream()
{
    close();
    if (bad() ||
        rename(m_tmpFilename.c_str(), m_filename.c_str())) {
        SyncContext::throwError(m_tmpFilename, errno);
    }
}

SE_END_CXX

