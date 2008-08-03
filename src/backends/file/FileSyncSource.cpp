/*
 * Copyright (C) 2007 Patrick Ohly
 * Copyright (C) 2007 Funambol
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"

#ifdef ENABLE_FILE

#include "FileSyncSource.h"

// SyncEvolution includes a copy of Boost header files.
// They are safe to use without creating additional
// build dependencies. boost::filesystem requires a
// library and therefore is avoided here. Some
// utility functions from SyncEvolution are used
// instead, plus standard C/Posix functions.
#include <boost/algorithm/string/case_conv.hpp>

#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <SyncEvolutionUtil.h>

#include <sstream>
#include <fstream>

FileSyncSource::FileSyncSource(const EvolutionSyncSourceParams &params,
                               const string &dataformat) :
    TrackingSyncSource(params),
    m_entryCounter(0)
{
    if (dataformat.empty()) {
        throwError("a data format must be specified");
    }
    size_t sep = dataformat.find(':');
    if (sep == dataformat.npos) {
        throwError(string("data format not specified as <mime type>:<mime version>: " + dataformat));
    }
    m_mimeType.assign(dataformat, 0, sep);
    m_mimeVersion = dataformat.substr(sep + 1);
    m_supportedTypes = dataformat;
}

string FileSyncSource::fileSuffix() const
{
    // database dumps created by SyncEvolution use this file suffix
    return
        (m_mimeType == "text/vcard" || m_mimeType == "text/x-vcard") ? "vcf" :
        (m_mimeType == "text/calendar" || m_mimeType == "text/x-calendar") ? "ics" :
        (m_mimeType == "text/plain") ? "txt" :
        "dat";
}

const char *FileSyncSource::getMimeType() const
{
    return m_mimeType.c_str();
}

const char *FileSyncSource::getMimeVersion() const
{
    return m_mimeVersion.c_str();
}

const char *FileSyncSource::getSupportedTypes() const
{
    // comma separated list, like "text/vcard:3.0,text/x-vcard:2.1"
    return m_supportedTypes.c_str();
}

void FileSyncSource::open()
{
    const string &database = getDatabaseID();
    const string prefix("file://");
    string basedir;
    bool createDir = false;

    // file:// is optional. It indicates that the
    // directory is to be created.
    if (boost::starts_with(database, prefix)) {
        basedir = database.substr(prefix.size());
        createDir = true;
    } else {
        basedir = database;
    }

    // check and, if allowed and necessary, create it
    if (!isDir(basedir)) {
        if (errno == ENOENT && createDir) {
            mkdir_p(basedir.c_str());
        } else {
            throwError(basedir + ": " + strerror(errno));
        }
    }

    // success!
    m_basedir = basedir;
}

void FileSyncSource::close()
{
    m_basedir.clear();
}

FileSyncSource::Databases FileSyncSource::getDatabases()
{
    Databases result;

    result.push_back(Database("select database via directory path",
                              "[file://]<path>"));
    return result;
}

void FileSyncSource::listAllItems(RevisionMap_t &revisions)
{
    ReadDir dirContent(m_basedir);

    BOOST_FOREACH(const string &entry, dirContent) {
        string filename = createFilename(entry);
        string revision = getATimeString(filename);
        long entrynum = atoll(entry.c_str());
        if (entrynum >= m_entryCounter) {
            m_entryCounter = entrynum + 1;
        }
        revisions[entry] = revision;
    }
}

SyncItem *FileSyncSource::createItem(const string &uid)
{
    string filename = createFilename(uid);

    ifstream in;
    in.open(filename.c_str());
    ostringstream out;
    char buf[8192];
    do {
        in.read(buf, sizeof(buf));
        out.write(buf, in.gcount());
    } while(in);
    if (!in.good() && !in.eof()) {
        throwError(filename + ": reading failed");
    }

    string content = out.str();
    auto_ptr<SyncItem> item(new SyncItem(uid.c_str()));
    item->setData(content.c_str(), content.size());
    item->setDataType(getMimeType());
    // probably not even used by Funambol client library...
    item->setModificationTime(0);

    return item.release();
}

TrackingSyncSource::InsertItemResult FileSyncSource::insertItem(const string &uid, const SyncItem &item)
{
    string newuid = uid;
    string creationTime;
    string filename;

    // Inserting a new and updating an existing item often uses
    // very similar code. In this case only the code for determining
    // the filename differs.
    //
    // In other sync sources the database might also have limitations
    // for the content of different items, for example, only one
    // VCALENDAR:EVENT with a certain UID. If the server does not
    // recognize that and sends a new item which collides with an
    // existing one, then the existing one should be updated.

    if (uid.size()) {
        // valid local ID: update that file
        filename = createFilename(uid);
    } else {
        // no local ID: create new file
        while (true) {
            ostringstream buff;
            buff << m_entryCounter;
            filename = createFilename(buff.str());

            // only create and truncate if file does not
            // exist yet, otherwise retry with next counter
            struct stat dummy;
            if (stat(filename.c_str(), &dummy)) {
                if (errno == ENOENT) {
                    newuid = buff.str();
                    break;
                } else {
                    throwError(filename + ": " + strerror(errno));
                }
            }

            m_entryCounter++;
        }
    }

    ofstream out;
    out.open(filename.c_str());
    out.write((const char *)item.getData(), item.getDataSize());
    out.close();
    if (!out.good()) {
        throwError(filename + ": writing failed");
    }

    return InsertItemResult(newuid,
                            getATimeString(filename),
                            false /* true if adding item was turned into update */);
}


void FileSyncSource::deleteItem(const string &uid)
{
    string filename = createFilename(uid);

    if (unlink(filename.c_str())) {
        throwError(filename + ": " + strerror(errno));
    }
}

void FileSyncSource::flush()
{
    // Our change tracking is time based.
    // Don't let caller proceed without waiting for
    // one second to prevent being called again before
    // the modification time stamp is larger than it
    // is now.
    sleep(1);
}

void FileSyncSource::logItem(const string &uid, const string &info, bool debug)
{
    if (LOG.getLevel() >= (debug ? LOG_LEVEL_DEBUG : LOG_LEVEL_INFO)) {
        // If there was a good way to extract a short string identifying
        // the item with uid, we would use it here and log it like this:
        // (LOG.*(debug ? &Log::debug : &Log::info))("%s: %s %s",
        //                                           getName() /* out sync source name */,
        //                                           itemName,
        //                                           info.c_str());
        //
        // Alternatively we could just log the uid. EvolutionSyncSource::logItem()
        // is an utility function which extracts a short string from certain
        // well-known types (FN for vCard, SUMMARY for vCalendar, first line for
        // text, ...). We use it here although it requires reading the full item
        // first. Don't fail while reading, we'll trigger a real error later on
        // if necessary.
        
        string filename = createFilename(uid);

        ifstream in;
        in.open(filename.c_str());
        ostringstream out;
        char buf[8192];
        do {
            in.read(buf, sizeof(buf));
            out.write(buf, in.gcount());
        } while(in);
        logItemUtil(out.str(),
                    m_mimeType,
                    m_mimeVersion,
                    uid,
                    info,
                    debug);
    }
}

void FileSyncSource::logItem(const SyncItem &item, const string &info, bool debug)
{
    if (LOG.getLevel() >= (debug ? LOG_LEVEL_DEBUG : LOG_LEVEL_INFO)) {
        if (!item.getData()) {
            // operation on item without data, fall back to logging via uid
            logItem(string(item.getKey()), info, debug);
        } else {
            string data = (const char *)item.getData();

            logItemUtil(data,
                        m_mimeType,
                        m_mimeVersion,
                        item.getKey(),
                        info,
                        debug);
        }
    }
}

string FileSyncSource::getATimeString(const string &filename)
{
    struct stat buf;
    if (stat(filename.c_str(), &buf)) {
        throwError(filename + ": " + strerror(errno));
    }
    time_t mtime = buf.st_mtime;

    ostringstream revision;
    revision << mtime;

    return revision.str();
}

string FileSyncSource::createFilename(const string &entry)
{
    string filename = m_basedir + "/" + entry;
    return filename;
}

#endif /* ENABLE_FILE */

#ifdef ENABLE_MODULES
# include "FileSyncSourceRegister.cpp"
#endif
