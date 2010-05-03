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

#ifndef INCL_EVOLUTION_SAFE_OSTREAM
# define INCL_EVOLUTION_SAFE_OSTREAM

#include <fstream>
#include <string>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

/**
 * Writes into temporary file (.# prefix) first, then renames to real
 * file only when no error encountered at the time of deleting the
 * instance. Once instantiated, the only way to safe the content of the
 * real file is to set the "fail" bit. In that sense it is similar to
 * instantiating a normal ofstream, which would directly overwrite
 * the file at creation time.
 */
class SafeOstream : public std::ofstream
{
    std::string m_filename;
    std::string m_tmpFilename;

 public:
    /**
     * @param filename    real filename, without the .# prefix
     */
    SafeOstream(const std::string filename);

    /**
     * on success, rename file
     */
   ~SafeOstream();
};

SE_END_CXX

#endif // INCL_EVOLUTION_SAFE_OSTREAM
