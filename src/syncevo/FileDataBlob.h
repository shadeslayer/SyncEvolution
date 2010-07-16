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

#ifndef INCL_EVOLUTION_FILE_DATA_BLOB
# define INCL_EVOLUTION_FILE_DATA_BLOB

#include <syncevo/DataBlob.h>
#include <string>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

/**
 * Abstract base class for a chunk of data.
 * Can be opened for reading and writing.
 * Meant to be used for plain files and
 * for sections inside a larger file.
 */
class FileDataBlob : public DataBlob
{
    std::string m_path;
    std::string m_fileName;
    bool m_readonly;

 public:
    /**
     * @param path      directory name
     * @param fileName  name of file inside that directory
     * @param readonly  do not create or write file, it must exist;
     *                  write() will throw an exception
     */
    FileDataBlob(const std::string &path, const std::string &fileName, bool readonly);
    FileDataBlob(const std::string &fullpath, bool readonly);

    boost::shared_ptr<std::ostream> write();
    boost::shared_ptr<std::istream> read();

    virtual std::string getName() const;
    virtual bool exists() const;
    virtual bool isReadonly() const { return m_readonly; }
};

SE_END_CXX

#endif // INCL_EVOLUTION_FILE_DATA_BLOB
