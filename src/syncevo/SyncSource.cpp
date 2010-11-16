/*
 * Copyright (C) 2005-2009 Patrick Ohly <patrick.ohly@gmx.de>
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

#ifndef _GNU_SOURCE
# define _GNU_SOURCE 1
#endif
#include <dlfcn.h>

#include <syncevo/SyncSource.h>
#include <syncevo/SyncContext.h>
#include <syncevo/util.h>

#include <syncevo/SynthesisEngine.h>
#include <synthesis/SDK_util.h>
#include <synthesis/sync_dbapidef.h>

#include <boost/bind.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <ctype.h>
#include <errno.h>
#include <unistd.h>

#include <fstream>
#include <iostream>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

void SyncSourceBase::throwError(const string &action, int error)
{
    throwError(action + ": " + strerror(error));
}

void SyncSourceBase::throwError(const string &failure)
{
    SyncContext::throwError(string(getName()) + ": " + failure);
}

SyncMLStatus SyncSourceBase::handleException()
{
    SyncMLStatus res = Exception::handle(this);
    return res == STATUS_FATAL ?
        STATUS_DATASTORE_FAILURE :
        res;
}

void SyncSourceBase::messagev(Level level,
                              const char *prefix,
                              const char *file,
                              int line,
                              const char *function,
                              const char *format,
                              va_list args)
{
    string newprefix = getName();
    if (prefix) {
        newprefix += ": ";
        newprefix += prefix;
    }
    LoggerBase::instance().messagev(level, newprefix.c_str(),
                                    file, line, function,
                                    format, args);
}

void SyncSourceBase::getDatastoreXML(string &xml, XMLConfigFragments &fragments)
{
    stringstream xmlstream;
    SynthesisInfo info;

    getSynthesisInfo(info, fragments);

    xmlstream <<
        "      <plugin_module>SyncEvolution</plugin_module>\n"
        "      <plugin_datastoreadmin>" <<
        (serverModeEnabled() ? "yes" : "no") <<
        "</plugin_datastoreadmin>\n"
        "      <fromremoteonlysupport> yes </fromremoteonlysupport>\n"
        "\n"
        "      <!-- General datastore settings for all DB types -->\n"
        "\n"
        "      <!-- if this is set to 'yes', SyncML clients can only read\n"
        "           from the database, but make no modifications -->\n"
        "      <readonly>no</readonly>\n"
        "\n"
        "      <!-- conflict strategy: Newer item wins\n"
        "           You can set 'server-wins' or 'client-wins' as well\n"
        "           if you want to give one side precedence\n"
        "      -->\n"
        "      <conflictstrategy>newer-wins</conflictstrategy>\n"
        "\n"
        "      <!-- on slowsync: do not duplicate items even if not fully equal\n"
        "           You can set this to 'duplicate' to avoid possible data loss\n"
        "           resulting from merging\n"
        "      -->\n"
        "      <slowsyncstrategy>newer-wins</slowsyncstrategy>\n"
        "\n"
        "      <!-- text db plugin is designed for UTF-8, make sure data is passed as UTF-8 (and not the ISO-8859-1 default) -->\n"
        "      <datacharset>UTF-8</datacharset>\n"
        "      <!-- use C-language (unix style) linefeeds (\n, 0x0A) -->\n"
        "      <datalineends>unix</datalineends>\n"
        "\n"
        "      <!-- set this to 'UTC' if time values should be stored in UTC into the database\n"
        "           rather than local time. 'SYSTEM' denotes local server time zone. -->\n"
        "      <datatimezone>SYSTEM</datatimezone>\n"
        "\n"
        "      <!-- plugin DB may have its own identifiers to determine the point in time of changes, so\n"
        "           we must make sure this identifier is stored (and not only the sync time) -->\n"
        "      <storesyncidentifiers>yes</storesyncidentifiers>\n"
        "\n";
    
    xmlstream <<
        "      <!-- Mapping of the fields to the fieldlist -->\n"
        "      <fieldmap fieldlist='" << info.m_fieldlist << "'>\n";
    if (!info.m_profile.empty()) {
        xmlstream <<
            "        <initscript><![CDATA[\n"
            "           string itemdata;\n"
            "        ]]></initscript>\n"
            "        <beforewritescript><![CDATA[\n";
        if(!info.m_beforeWriteScript.empty()) {
            xmlstream << 
                "           " << info.m_beforeWriteScript << "\n";
        }
        xmlstream <<
            "           itemdata = MAKETEXTWITHPROFILE(" << info.m_profile << ", \"" << info.m_backendRule << "\");\n"
            "        ]]></beforewritescript>\n"
            "        <afterreadscript><![CDATA[\n"
            "           PARSETEXTWITHPROFILE(itemdata, " << info.m_profile << ", \"" << info.m_backendRule << "\");\n";
        if(!info.m_afterReadScript.empty()) {
            xmlstream << 
                "           " << info.m_afterReadScript<< "\n";
        }
        xmlstream <<
            "        ]]></afterreadscript>\n"
            "        <map name='data' references='itemdata' type='string'/>\n";
    }
    xmlstream << 
        "        <automap/>\n"
        "      </fieldmap>\n"
        "\n";

    xmlstream <<
        "      <!-- datatypes supported by this datastore -->\n"
        "      <typesupport>\n" <<
        info.m_datatypes <<
        "      </typesupport>\n";

    xml = xmlstream.str();
}

string SyncSourceBase::getNativeDatatypeName()
{
    SynthesisInfo info;
    XMLConfigFragments fragments;
    getSynthesisInfo(info, fragments);
    return info.m_native;
}

SDKInterface *SyncSource::getSynthesisAPI() const
{
    return m_synthesisAPI.empty() ?
        NULL :
        static_cast<SDKInterface *>(m_synthesisAPI[m_synthesisAPI.size() - 1]);
}

void SyncSource::pushSynthesisAPI(sysync::SDK_InterfaceType *synthesisAPI)
{
    m_synthesisAPI.push_back(synthesisAPI);
}

void SyncSource::popSynthesisAPI() {
    m_synthesisAPI.pop_back();
}

SourceRegistry &SyncSource::getSourceRegistry()
{
    static SourceRegistry sourceRegistry;
    return sourceRegistry;
}

RegisterSyncSource::RegisterSyncSource(const string &shortDescr,
                                       bool enabled,
                                       Create_t create,
                                       const string &typeDescr,
                                       const Values &typeValues) :
    m_shortDescr(shortDescr),
    m_enabled(enabled),
    m_create(create),
    m_typeDescr(typeDescr),
    m_typeValues(typeValues)
{
    SourceRegistry &registry(SyncSource::getSourceRegistry());

    // insert sorted by description to have deterministic ordering
    for(SourceRegistry::iterator it = registry.begin();
        it != registry.end();
        ++it) {
        if ((*it)->m_shortDescr > shortDescr) {
            registry.insert(it, this);
            return;
        }
    }
    registry.push_back(this);
}

SyncSource *const RegisterSyncSource::InactiveSource = (SyncSource *)1;

TestRegistry &SyncSource::getTestRegistry()
{
    static TestRegistry testRegistry;
    return testRegistry;
}

RegisterSyncSourceTest::RegisterSyncSourceTest(const string &configName, const string &testCaseName) :
    m_configName(configName),
    m_testCaseName(testCaseName)
{
    SyncSource::getTestRegistry().push_back(this);
}

static class ScannedModules {
public:
    ScannedModules() {
#ifdef ENABLE_MODULES
        list<pair <string, boost::shared_ptr<ReadDir> > > dirs;
        /* If enviroment variable SYNCEVOLUTION_BACKEND_DIR is set, will search
        backends from this path instead. */
        string backend_dir (SYNCEVO_BACKEND);
        char *backend_env = getenv("SYNCEVOLUTION_BACKEND_DIR");
        if (backend_env && strlen(backend_env)){
            backend_dir = backend_env;
        }
        boost::shared_ptr<ReadDir> dir (new ReadDir (backend_dir, false));
        string dirpath (backend_dir);
        // scan directories for matching module names
        do {
            debug<<"Scanning backend libraries in " <<dirpath <<endl;
            BOOST_FOREACH (const string &entry, *dir) {
                void *dlhandle;
                if (isDir (dirpath + '/' + entry)) {
                    /* This is a 2-level dir, this corresponds to loading
                     * backends from current building directory. The library
                     * should reside in .libs sub directory.*/
                    string path = dirpath + '/' + entry +"/.libs";
                    if (isDir (path)) {
                        boost::shared_ptr<ReadDir> subdir (new ReadDir (path, false));
                        dirs.push_back (make_pair(path, subdir));
                    }
                    continue;
                }
                if (entry.rfind(".so") == entry.length()-3){

                    // Open the shared object so that backend can register
                    // itself. We keep that pointer, so never close the
                    // module!
                    string fullpath = dirpath + '/' + entry;
                    fullpath = normalizePath(fullpath);
                    dlhandle = dlopen(fullpath.c_str(), RTLD_NOW|RTLD_GLOBAL);
                    // remember which modules were found and which were not
                    if (dlhandle) {
                        debug<<"Loading backend library "<<entry<<endl;
                        info<<"Loading backend library "<<fullpath<<endl;
                        m_available.push_back(entry);
                    } else {
                        debug<<"Loading backend library "<<entry<<"failed "<< dlerror()<<endl;
                    }
                }
            }
            if (!dirs.empty()){
                dirpath = dirs.front().first;
                dir = dirs.front().second;
                dirs.pop_front();
            } else {
                break;
            }
        } while (true);
#endif
    }
    list<string> m_available;
    std::ostringstream debug, info;
} scannedModules;

string SyncSource::backendsInfo() {
    return scannedModules.info.str();
}
string SyncSource::backendsDebug() {
    return scannedModules.debug.str();
}

SyncSource *SyncSource::createSource(const SyncSourceParams &params, bool error, SyncConfig *config)
{
    string sourceTypeString = getSourceTypeString(params.m_nodes);
    SourceType sourceType = getSourceType(params.m_nodes);

    if (sourceType.m_backend == "virtual") {
        SyncSource *source = NULL;
        source = new VirtualSyncSource(params, config);
        if (error && !source) {
            SyncContext::throwError(params.m_name + ": virtual source cannot be instantiated");
        }
        return source;
    }

    const SourceRegistry &registry(getSourceRegistry());
    BOOST_FOREACH(const RegisterSyncSource *sourceInfos, registry) {
        SyncSource *source = sourceInfos->m_create(params);
        if (source) {
            if (source == RegisterSyncSource::InactiveSource) {
                SyncContext::throwError(params.m_name + ": access to " + sourceInfos->m_shortDescr +
                                                " not enabled, therefore type = " + sourceTypeString + " not supported");
            }
            return source;
        }
    }

    if (error) {
        string problem = params.m_name + ": type '" + sourceTypeString + "' not supported";
        if (scannedModules.m_available.size()) {
            problem += " by any of the backends (";
            problem += boost::join(scannedModules.m_available, ", ");
            problem += ")";
        }
        SyncContext::throwError(problem);
    }

    return NULL;
}

SyncSource *SyncSource::createTestingSource(const string &name, const string &type, bool error,
                                            const char *prefix)
{
    SyncConfig config("testing@client-test");
    SyncSourceNodes nodes = config.getSyncSourceNodes(name);
    SyncSourceParams params(name, nodes);
    PersistentSyncSourceConfig sourceconfig(name, nodes);
    sourceconfig.setSourceType(type);
    if (prefix) {
        sourceconfig.setDatabaseID(string(prefix) + name + "_1");
    }
    return createSource(params, error);
}

VirtualSyncSource::VirtualSyncSource(const SyncSourceParams &params, SyncConfig *config) :
    DummySyncSource(params)
{
    if (config) {
        BOOST_FOREACH(std::string name, getMappedSources()) {
            SyncSourceNodes source = config->getSyncSourceNodes(name);
            SyncSourceParams params(name, source);
            boost::shared_ptr<SyncSource> syncSource(createSource(params, true, config));
            m_sources.push_back(syncSource);
        }
    }
}

void VirtualSyncSource::open()
{
    getDataTypeSupport();
    BOOST_FOREACH(boost::shared_ptr<SyncSource> &source, m_sources) {
        source->open();
    }
}

void VirtualSyncSource::close()
{
    BOOST_FOREACH(boost::shared_ptr<SyncSource> &source, m_sources) {
        source->close();
    }
}

std::vector<std::string> VirtualSyncSource::getMappedSources()
{
    std::string evoSyncSource = getDatabaseID();
    std::vector<std::string> mappedSources = unescapeJoinedString (evoSyncSource, ',');
    return mappedSources;
}

std::string VirtualSyncSource::getDataTypeSupport()
{
    string datatypes;
    SourceType sourceType = getSourceType();
    string type = sourceType.m_format;

    datatypes = getDataTypeSupport(type, sourceType.m_forceFormat);
    return datatypes;
}

SyncSource::Databases VirtualSyncSource::getDatabases()
{
    SyncSource::Databases dbs;
    BOOST_FOREACH (boost::shared_ptr<SyncSource> &source, m_sources) {
        SyncSource::Databases sub = source->getDatabases();
        if (sub.empty()) {
            return dbs;
        }
    }
    Database db ("calendar+todo", "");
    dbs.push_back (db);
    return dbs;
}

void SyncSourceSession::init(SyncSource::Operations &ops)
{
    ops.m_startDataRead = boost::bind(&SyncSourceSession::startDataRead, this, _1, _2);
    ops.m_endDataWrite = boost::bind(&SyncSourceSession::endDataWrite, this, _1, _2);
}

sysync::TSyError SyncSourceSession::startDataRead(const char *lastToken, const char *resumeToken)
{
    beginSync(lastToken ? lastToken : "",
              resumeToken ? resumeToken : "");
    return sysync::LOCERR_OK;
}

sysync::TSyError SyncSourceSession::endDataWrite(bool success, char **newToken)
{
    std::string token = endSync(success);
    *newToken = StrAlloc(token.c_str());
    return sysync::LOCERR_OK;
}

void SyncSourceChanges::init(SyncSource::Operations &ops)
{
    ops.m_readNextItem = boost::bind(&SyncSourceChanges::iterate, this, _1, _2, _3);
}

SyncSourceChanges::SyncSourceChanges() :
    m_first(true)
{
}

bool SyncSourceChanges::addItem(const string &luid, State state)
{
    pair<Items_t::iterator, bool> res = m_items[state].insert(luid);
    return res.second;
}

sysync::TSyError SyncSourceChanges::iterate(sysync::ItemID aID,
                                            sysync::sInt32 *aStatus,
                                            bool aFirst)
{
    if (m_first || aFirst) {
        m_it = m_items[ANY].begin();
        m_first = false;
    }

    if (m_it == m_items[ANY].end()) {
        *aStatus = sysync::ReadNextItem_EOF;
    } else {
        const string &luid = *m_it;

        if (m_items[NEW].find(luid) != m_items[NEW].end() ||
            m_items[UPDATED].find(luid) != m_items[UPDATED].end()) {
            *aStatus = sysync::ReadNextItem_Changed;
        } else {
            *aStatus = sysync::ReadNextItem_Unchanged;
        }
        aID->item = StrAlloc(luid.c_str());
        ++m_it;
    }

    return sysync::LOCERR_OK;
}

void SyncSourceDelete::init(SyncSource::Operations &ops)
{
    ops.m_deleteItem = boost::bind(&SyncSourceDelete::deleteItemSynthesis, this, _1);
}

sysync::TSyError SyncSourceDelete::deleteItemSynthesis(sysync::cItemID aID)
{
    deleteItem(aID->item);
    incrementNumDeleted();
    return sysync::LOCERR_OK;
}


void SyncSourceSerialize::getSynthesisInfo(SynthesisInfo &info,
                                           XMLConfigFragments &fragments)
{
    string type = getMimeType();

    if (type == "text/x-vcard") {
        info.m_native = "vCard21";
        info.m_fieldlist = "contacts";
        info.m_profile = "\"vCard\", 1";
        info.m_datatypes =
            "        <use datatype='vCard21' mode='rw' preferred='yes'/>\n"
            "        <use datatype='vCard30' mode='rw'/>\n";
    } else if (type == "text/vcard") {
        info.m_native = "vCard30";
        info.m_fieldlist = "contacts";
        info.m_profile = "\"vCard\", 2";
        info.m_datatypes =
            "        <use datatype='vCard21' mode='rw'/>\n"
            "        <use datatype='vCard30' mode='rw' preferred='yes'/>\n";
    } else if (type == "text/x-calendar" || type == "text/x-vcalendar") {
        info.m_native = "vCalendar10";
        info.m_fieldlist = "calendar";
        info.m_profile = "\"vCalendar\", 1";
        info.m_datatypes =
            "        <use datatype='vCalendar10' mode='rw' preferred='yes'/>\n"
            "        <use datatype='iCalendar20' mode='rw'/>\n";
        /**
         * here are two default implementations. If user wants to reset it,
         * just implement its own getSynthesisInfo. If user wants to use this default
         * implementations and its new scripts, it is possible to append its implementations
         * to info.m_afterReadScript and info.m_beforeWriteScript.
         */
        info.m_afterReadScript = "$VCALENDAR10_AFTERREAD_SCRIPT;\n";
        info.m_beforeWriteScript = "$VCALENDAR10_BEFOREWRITE_SCRIPT;\n";
    } else if (type == "text/calendar") {
        info.m_native = "iCalendar20";
        info.m_fieldlist = "calendar";
        info.m_profile = "\"vCalendar\", 2";
        info.m_datatypes =
            "        <use datatype='vCalendar10' mode='rw'/>\n"
            "        <use datatype='iCalendar20' mode='rw' preferred='yes'/>\n";
    } else if (type == "text/plain") {
        info.m_fieldlist = "Note";
        info.m_profile = "\"Note\", 2";
    } else {
        throwError(string("default MIME type not supported: ") + type);
    }

    SourceType sourceType = getSourceType();
    if (!sourceType.m_format.empty()) {
        type = sourceType.m_format;
    }
    info.m_datatypes = getDataTypeSupport(type, sourceType.m_forceFormat);
}

std::string SyncSourceBase::getDataTypeSupport(const std::string &type,
                                               bool forceFormat)
{
    std::string datatypes;

    if (type == "text/x-vcard:2.1" || type == "text/x-vcard") {
        datatypes =
            "        <use datatype='vCard21' mode='rw' preferred='yes'/>\n";
        if (!forceFormat) {
            datatypes +=
                "        <use datatype='vCard30' mode='rw'/>\n";
        }
    } else if (type == "text/vcard:3.0" || type == "text/vcard") {
        datatypes =
            "        <use datatype='vCard30' mode='rw' preferred='yes'/>\n";
        if (!forceFormat) {
            datatypes +=
                "        <use datatype='vCard21' mode='rw'/>\n";
        }
    } else if (type == "text/x-vcalendar:1.0" || type == "text/x-vcalendar"
             || type == "text/x-calendar:1.0" || type == "text/x-calendar") {
        datatypes =
            "        <use datatype='vcalendar10' mode='rw' preferred='yes'/>\n";
        if (!forceFormat) {
            datatypes +=
                "        <use datatype='icalendar20' mode='rw'/>\n";
        }
    } else if (type == "text/calendar:2.0" || type == "text/calendar") {
        datatypes =
            "        <use datatype='icalendar20' mode='rw' preferred='yes'/>\n";
        if (!forceFormat) {
            datatypes +=
                "        <use datatype='vcalendar10' mode='rw'/>\n";
        }
    } else if (type == "text/plain:1.0" || type == "text/plain") {
        // note10 are the same as note11, so ignore force format
        datatypes =
            "        <use datatype='note10' mode='rw' preferred='yes'/>\n"
            "        <use datatype='note11' mode='rw'/>\n";
    } else if (type.empty()) {
        throwError("no MIME type configured");
    } else {
        throwError(string("configured MIME type not supported: ") + type);
    }

    return datatypes;
}

sysync::TSyError SyncSourceSerialize::readItemAsKey(sysync::cItemID aID, sysync::KeyH aItemKey)
{
    std::string item;

    readItem(aID->item, item);
    TSyError res = getSynthesisAPI()->setValue(aItemKey, "data", item.c_str(), item.size());
    return res;
}

sysync::TSyError SyncSourceSerialize::insertItemAsKey(sysync::KeyH aItemKey, sysync::cItemID aID, sysync::ItemID newID)
{
    SharedBuffer data;
    TSyError res = getSynthesisAPI()->getValue(aItemKey, "data", data);

    if (!res) {
        InsertItemResult inserted =
            insertItem(!aID ? "" : aID->item, data.get());
        newID->item = StrAlloc(inserted.m_luid.c_str());
    }

    return res;
}

void SyncSourceSerialize::init(SyncSource::Operations &ops)
{
    ops.m_readItemAsKey = boost::bind(&SyncSourceSerialize::readItemAsKey,
                                      this, _1, _2);
    ops.m_insertItemAsKey = boost::bind(&SyncSourceSerialize::insertItemAsKey,
                                        this, _1, (sysync::cItemID)NULL, _2);
    ops.m_updateItemAsKey = boost::bind(&SyncSourceSerialize::insertItemAsKey,
                                        this, _1, _2, _3);
}

/**
 * Mapping from Hash() value to file.
 */
class ItemCache
{
public:
#ifdef USE_SHA256
    typedef std::string Hash_t;
    Hash_t hashFunc(const std::string &data) { return SHA_256(data); }
#else
    typedef unsigned long Hash_t;
    Hash_t hashFunc(const std::string &data) { return Hash(data); }
#endif
    typedef unsigned long Counter_t;

    /** mark the algorithm used for the hash via different suffices */
    static const char *m_hashSuffix;

    /**
     * Collect information about stored hashes. Provides
     * access to file name via hash.
     *
     * If no hashes were written (as in an old SyncEvoltion
     * version), we could read the files to recreate the
     * hashes. This is not done because it won't occur
     * often enough.
     *
     * Hashes are also not verified. Users should better
     * not edit them or file contents...
     *
     * @param oldBackup     existing backup to read; may be empty
     */
    void init(const SyncSource::Operations::ConstBackupInfo &oldBackup)
    {
        m_hash2counter.clear();
        m_dirname = oldBackup.m_dirname;
        if (m_dirname.empty() || !oldBackup.m_node) {
            return;
        }

        long numitems;
        if (!oldBackup.m_node->getProperty("numitems", numitems)) {
            return;
        }
        for (long counter = 1; counter <= numitems; counter++) {
            stringstream key;
            key << counter << m_hashSuffix;
            Hash_t hash;
            if (oldBackup.m_node->getProperty(key.str(), hash)) {
                m_hash2counter[hash] = counter;
            }
        }
    }

    /**
     * create file name for a specific hash, empty if no such hash
     */
    string getFilename(Hash_t hash)
    {
        Map_t::const_iterator it = m_hash2counter.find(hash);
        if (it != m_hash2counter.end()) {
            stringstream dirname;
            dirname << m_dirname << "/" << it->second;
            return dirname.str();
        } else {
            return "";
        }
    }

private:
    typedef std::map<Hash_t, Counter_t> Map_t;
    Map_t m_hash2counter;
    string m_dirname;
};

const char *ItemCache::m_hashSuffix =
#ifdef USE_SHA256
    "-sha256"
#else
    "-hash"
#endif
;

void SyncSourceRevisions::initRevisions()
{
    if (!m_revisionsSet) {
        listAllItems(m_revisions);
        m_revisionsSet = true;
    }
}


void SyncSourceRevisions::backupData(const SyncSource::Operations::ConstBackupInfo &oldBackup,
                                     const SyncSource::Operations::BackupInfo &newBackup,
                                     BackupReport &report)
{
    ItemCache cache;
    cache.init(oldBackup);

    bool startOfSync = newBackup.m_mode == SyncSource::Operations::BackupInfo::BACKUP_BEFORE;
    RevisionMap_t buffer;
    RevisionMap_t *revisions;
    if (startOfSync) {
        initRevisions();
        revisions = &m_revisions;
    } else {
        listAllItems(buffer);
        revisions = &buffer;
    }

    unsigned long counter = 1;
    string item;
    errno = 0;
    BOOST_FOREACH(const StringPair &mapping, *revisions) {
        const string &uid = mapping.first;
        const string &rev = mapping.second;
        m_raw->readItemRaw(uid, item);

        stringstream filename;
        filename << newBackup.m_dirname << "/" << counter;

        ItemCache::Hash_t hash = cache.hashFunc(item);
        string oldfilename = cache.getFilename(hash);
        if (!oldfilename.empty()) {
            // found old file with same content, reuse it via hardlink
            if (link(oldfilename.c_str(), filename.str().c_str())) {
                // Hard linking failed. Record this, then continue
                // by ignoring the old file.
                SE_LOG_DEBUG(NULL, NULL, "hard linking old %s new %s: %s",
                             oldfilename.c_str(),
                             filename.str().c_str(),
                             strerror(errno));
                oldfilename.clear();
            }
        }

        if (oldfilename.empty()) {
            // write new file instead of reusing old one
            ofstream out(filename.str().c_str());
            out.write(item.c_str(), item.size());
            out.close();
            if (out.fail()) {
                throwError(string("error writing ") + filename.str() + ": " + strerror(errno));
            }
        }

        stringstream key;
        key << counter << "-uid";
        newBackup.m_node->setProperty(key.str(), uid);
        // clear() does not remove the existing content, which was
        // intended here. This should have been key.str(""). As a
        // result, keys for -rev are longer than intended because they
        // start with the -uid part. We cannot change it now, because
        // that would break compatibility with nodes that use the
        // older, longer keys for -rev.
        key.clear();
        key << counter << "-rev";
        newBackup.m_node->setProperty(key.str(), rev);
        key.str("");
        key << counter << ItemCache::m_hashSuffix;
        newBackup.m_node->setProperty(key.str(), hash);

        counter++;
    }

    stringstream value;
    value << counter - 1;
    newBackup.m_node->setProperty("numitems", value.str());
    newBackup.m_node->flush();

    report.setNumItems(counter - 1);
}

void SyncSourceRevisions::restoreData(const SyncSource::Operations::ConstBackupInfo &oldBackup,
                                      bool dryrun,
                                      SyncSourceReport &report)
{
    RevisionMap_t revisions;
    listAllItems(revisions);

    long numitems;
    string strval;
    strval = oldBackup.m_node->readProperty("numitems");
    stringstream stream(strval);
    stream >> numitems;

    for (long counter = 1; counter <= numitems; counter++) {
        stringstream key;
        key << counter << "-uid";
        string uid = oldBackup.m_node->readProperty(key.str());
        key.clear();
        key << counter << "-rev";
        string rev = oldBackup.m_node->readProperty(key.str());
        RevisionMap_t::iterator it = revisions.find(uid);
        report.incrementItemStat(report.ITEM_LOCAL,
                                 report.ITEM_ANY,
                                 report.ITEM_TOTAL);
        if (it != revisions.end() &&
            it->second == rev) {
            // item exists in backup and database with same revision:
            // nothing to do
        } else {
            // add or update, so need item
            stringstream filename;
            filename << oldBackup.m_dirname << "/" << counter;
            string data;
            if (!ReadFile(filename.str(), data)) {
                throwError(StringPrintf("restoring %s from %s failed: could not read file",
                                        uid.c_str(),
                                        filename.str().c_str()));
            }
            // TODO: it would be nicer to recreate the item
            // with the original revision. If multiple peers
            // synchronize against us, then some of them
            // might still be in sync with that revision. By
            // updating the revision here we force them to
            // needlessly receive an update.
            //
            // For the current peer for which we restore this is
            // avoided by the revision check above: unchanged
            // items aren't touched.
            SyncSourceReport::ItemState state =
                it == revisions.end() ?
                SyncSourceReport::ITEM_ADDED :   // not found in database, create anew
                SyncSourceReport::ITEM_UPDATED;  // found, update existing item
            try {
                report.incrementItemStat(report.ITEM_LOCAL,
                                         state,
                                         report.ITEM_TOTAL);
                if (!dryrun) {
                    m_raw->insertItemRaw(it == revisions.end() ? "" : uid,
                                         data);
                }
            } catch (...) {
                report.incrementItemStat(report.ITEM_LOCAL,
                                         state,
                                         report.ITEM_REJECT);
                throw;
            }
        }

        // remove handled item from revision list so
        // that when we are done, the only remaining
        // items listed there are the ones which did
        // no exist in the backup
        if (it != revisions.end()) {
            revisions.erase(it);
        }
    }

    // now remove items that were not in the backup
    BOOST_FOREACH(const StringPair &mapping, revisions) {
        try {
            report.incrementItemStat(report.ITEM_LOCAL,
                                     report.ITEM_REMOVED,
                                     report.ITEM_TOTAL);
            if (!dryrun) {
                m_del->deleteItem(mapping.first);
            }
        } catch(...) {
            report.incrementItemStat(report.ITEM_LOCAL,
                                     report.ITEM_REMOVED,
                                     report.ITEM_REJECT);
            throw;
        }
    }
}

void SyncSourceRevisions::detectChanges(ConfigNode &trackingNode)
{
    initRevisions();

    BOOST_FOREACH(const StringPair &mapping, m_revisions) {
        const string &uid = mapping.first;
        const string &revision = mapping.second;

        // always remember the item, need full list
        addItem(uid);

        string serverRevision(trackingNode.readProperty(uid));
        if (!serverRevision.size()) {
            addItem(uid, NEW);
            trackingNode.setProperty(uid, revision);
        } else {
            if (revision != serverRevision) {
                addItem(uid, UPDATED);
                trackingNode.setProperty(uid, revision);
            }
        }
    }

    // clear information about all items that we recognized as deleted
    ConfigProps props;
    trackingNode.readProperties(props);

    BOOST_FOREACH(const StringPair &mapping, props) {
        const string &uid(mapping.first);
        if (getAllItems().find(uid) == getAllItems().end()) {
            addItem(uid, DELETED);
            trackingNode.removeProperty(uid);
        }
    }
}

void SyncSourceRevisions::updateRevision(ConfigNode &trackingNode,
                                         const std::string &old_luid,
                                         const std::string &new_luid,
                                         const std::string &revision)
{
    databaseModified();
    if (old_luid != new_luid) {
        trackingNode.removeProperty(old_luid);
    }
    if (new_luid.empty() || revision.empty()) {
        throwError("need non-empty LUID and revision string");
    }
    trackingNode.setProperty(new_luid, revision);
}

void SyncSourceRevisions::deleteRevision(ConfigNode &trackingNode,
                                         const std::string &luid)
{
    databaseModified();
    trackingNode.removeProperty(luid);
}

void SyncSourceRevisions::sleepSinceModification()
{
    time_t current = time(NULL);
    while (current - m_modTimeStamp < m_revisionAccuracySeconds) {
        sleep(m_revisionAccuracySeconds - (current - m_modTimeStamp));
        current = time(NULL);
    }
}

void SyncSourceRevisions::databaseModified()
{
    m_modTimeStamp = time(NULL);
}

void SyncSourceRevisions::init(SyncSourceRaw *raw,
                               SyncSourceDelete *del,
                               int granularity,
                               SyncSource::Operations &ops)
{
    m_raw = raw;
    m_del = del;
    m_modTimeStamp = 0;
    m_revisionAccuracySeconds = granularity;
    m_revisionsSet = false;
    if (raw) {
        ops.m_backupData = boost::bind(&SyncSourceRevisions::backupData,
                                       this, _1, _2, _3);
    }
    if (raw && del) {
        ops.m_restoreData = boost::bind(&SyncSourceRevisions::restoreData,
                                        this, _1, _2, _3);
    }
    ops.m_endSession.push_back(boost::bind(&SyncSourceRevisions::sleepSinceModification,
                                           this));
}

std::string SyncSourceLogging::getDescription(sysync::KeyH aItemKey)
{
    try {
        std::list<std::string> values;

        BOOST_FOREACH(const std::string &field, m_fields) {
            SharedBuffer value;
            if (!getSynthesisAPI()->getValue(aItemKey, field, value) &&
                value.size()) {
                values.push_back(std::string(value.get()));
            }
        }

        std::string description = boost::join(values, m_sep);
        return description;
    } catch (...) {
        // Instead of failing we log the error and ask
        // the caller to log the UID. That way transient
        // errors or errors in the logging code don't
        // prevent syncs.
        handleException();
        return "";
    }
}

std::string SyncSourceLogging::getDescription(const string &luid)
{
    return "";
}

sysync::TSyError SyncSourceLogging::insertItemAsKey(sysync::KeyH aItemKey, sysync::ItemID newID, const boost::function<SyncSource::Operations::InsertItemAsKey_t> &parent)
{
    std::string description = getDescription(aItemKey);
    SE_LOG_INFO(this, NULL,
                description.empty() ? "%s <%s>" : "%s \"%s\"",
                "adding",
                !description.empty() ? description.c_str() : "???");
    if (parent) {
        return parent(aItemKey, newID);
    } else {
        return sysync::LOCERR_NOTIMP;
    }
}

sysync::TSyError SyncSourceLogging::updateItemAsKey(sysync::KeyH aItemKey, sysync::cItemID aID, sysync::ItemID newID, const boost::function<SyncSource::Operations::UpdateItemAsKey_t> &parent)
{
    std::string description = getDescription(aItemKey);
    SE_LOG_INFO(this, NULL,
                description.empty() ? "%s <%s>" : "%s \"%s\"",
                "updating",
                !description.empty() ? description.c_str() : aID ? aID->item : "???");
    if (parent) {
        return parent(aItemKey, aID, newID);
    } else {
        return sysync::LOCERR_NOTIMP;
    }
}

sysync::TSyError SyncSourceLogging::deleteItem(sysync::cItemID aID, const boost::function<SyncSource::Operations::DeleteItem_t> &parent)
{
    std::string description = getDescription(aID->item);
    SE_LOG_INFO(this, NULL,
                description.empty() ? "%s <%s>" : "%s \"%s\"",
                "deleting",
                !description.empty() ? description.c_str() : aID->item);
    if (parent) {
        return parent(aID);
    } else {
        return sysync::LOCERR_NOTIMP;
    }
}

void SyncSourceLogging::init(const std::list<std::string> &fields,
                             const std::string &sep,
                             SyncSource::Operations &ops)
{
    m_fields = fields;
    m_sep = sep;

    ops.m_insertItemAsKey = boost::bind(&SyncSourceLogging::insertItemAsKey,
                                        this, _1, _2, ops.m_insertItemAsKey);
    ops.m_updateItemAsKey = boost::bind(&SyncSourceLogging::updateItemAsKey,
                                        this, _1, _2, _3, ops.m_updateItemAsKey);
    ops.m_deleteItem = boost::bind(&SyncSourceLogging::deleteItem,
                                   this, _1, ops.m_deleteItem);
}

sysync::TSyError SyncSourceAdmin::loadAdminData(const char *aLocDB,
                                                const char *aRemDB,
                                                char **adminData)
{
    std::string data = m_configNode->readProperty(m_adminPropertyName);
    *adminData = StrAlloc(StringEscape::unescape(data, '!').c_str());
    resetMap();
    return sysync::LOCERR_OK;
}

sysync::TSyError SyncSourceAdmin::saveAdminData(const char *adminData)
{
    m_configNode->setProperty(m_adminPropertyName,
                              StringEscape::escape(adminData, '!', StringEscape::INI_VALUE));

    // Flush here, because some calls to saveAdminData() happend
    // after SyncSourceAdmin::flush() (= session end).
    m_configNode->flush();
    return sysync::LOCERR_OK;
}

bool SyncSourceAdmin::readNextMapItem(sysync::MapID mID, bool aFirst)
{
    if (aFirst) {
        resetMap();
    }
    if (m_mappingIterator != m_mapping.end()) {
        entry2mapid(m_mappingIterator->first, m_mappingIterator->second, mID);
        ++m_mappingIterator;
        return true;
    } else {
        return false;
    }
}

sysync::TSyError SyncSourceAdmin::insertMapItem(sysync::cMapID mID)
{
    string key, value;
    mapid2entry(mID, key, value);

#if 0
    StringMap::iterator it = m_mapping.find(key);
    if (it != m_mapping.end()) {
        // error, exists already
        return sysync::DB_Forbidden;
    } else {
        m_mapping[key] = value;
        return sysync::LOCERR_OK;
    }
#else
    m_mapping[key] = value;
    m_mappingNode->clear();
    m_mappingNode->writeProperties(m_mapping);
    m_mappingNode->flush();
    return sysync::LOCERR_OK;
#endif
}

sysync::TSyError SyncSourceAdmin::updateMapItem(sysync::cMapID mID)
{
    string key, value;
    mapid2entry(mID, key, value);

    StringMap::iterator it = m_mapping.find(key);
    if (it == m_mapping.end()) {
        // error, does not exist
        return sysync::DB_Forbidden;
    } else {
        m_mapping[key] = value;
        m_mappingNode->clear();
        m_mappingNode->writeProperties(m_mapping);
        m_mappingNode->flush();
        return sysync::LOCERR_OK;
    }
}

sysync::TSyError SyncSourceAdmin::deleteMapItem(sysync::cMapID mID)
{
    string key, value;
    mapid2entry(mID, key, value);

    StringMap::iterator it = m_mapping.find(key);
    if (it == m_mapping.end()) {
        // error, does not exist
        return sysync::DB_Forbidden;
    } else {
        m_mapping.erase(it);
        m_mappingNode->clear();
        m_mappingNode->writeProperties(m_mapping);
        m_mappingNode->flush();
        return sysync::LOCERR_OK;
    }
}

void SyncSourceAdmin::flush()
{
    m_configNode->flush();
    if (m_mappingLoaded) {
        m_mappingNode->clear();
        m_mappingNode->writeProperties(m_mapping);
        m_mappingNode->flush();
    }
}

void SyncSourceAdmin::resetMap()
{
    m_mapping.clear();
    m_mappingNode->readProperties(m_mapping);
    m_mappingIterator = m_mapping.begin();
    m_mappingLoaded = true;
}


void SyncSourceAdmin::mapid2entry(sysync::cMapID mID, string &key, string &value)
{
    key = StringPrintf("%s-%x",
                       StringEscape::escape(mID->localID ? mID->localID : "", '!', StringEscape::INI_WORD).c_str(),
                       mID->ident);
    if (mID->remoteID && mID->remoteID[0]) {
        value = StringPrintf("%s %x",
                             StringEscape::escape(mID->remoteID ? mID->remoteID : "", '!', StringEscape::INI_WORD).c_str(),
                             mID->flags);
    } else {
        value = StringPrintf("%x", mID->flags);
    }
}

void SyncSourceAdmin::entry2mapid(const string &key, const string &value, sysync::MapID mID)
{
    size_t found = key.rfind('-');
    mID->localID = StrAlloc(StringEscape::unescape(key.substr(0,found), '!').c_str());
    if (found != key.npos) {
        mID->ident =  strtol(key.substr(found+1).c_str(), NULL, 16);
    } else {
        mID->ident = 0;
    }
    std::vector< std::string > tokens;
    boost::split(tokens, value, boost::is_from_range(' ', ' '));
    if (tokens.size() >= 2) {
        // if branch from mapid2entry above
        mID->remoteID = StrAlloc(StringEscape::unescape(tokens[0], '!').c_str());
        mID->flags = strtol(tokens[1].c_str(), NULL, 16);
    } else {
        // else branch from above
        mID->remoteID = NULL;
        mID->flags = strtol(tokens[0].c_str(), NULL, 16);
    }
}

void SyncSourceAdmin::init(SyncSource::Operations &ops,
                           const boost::shared_ptr<ConfigNode> &config,
                           const std::string adminPropertyName,
                           const boost::shared_ptr<ConfigNode> &mapping)
{
    m_configNode = config;
    m_adminPropertyName = adminPropertyName;
    m_mappingNode = mapping;
    m_mappingLoaded = false;

    ops.m_loadAdminData = boost::bind(&SyncSourceAdmin::loadAdminData,
                                      this, _1, _2, _3);
    ops.m_saveAdminData = boost::bind(&SyncSourceAdmin::saveAdminData,
                                      this, _1);
    ops.m_readNextMapItem = boost::bind(&SyncSourceAdmin::readNextMapItem,
                                        this, _1, _2);
    ops.m_insertMapItem = boost::bind(&SyncSourceAdmin::insertMapItem,
                                      this, _1);
    ops.m_updateMapItem = boost::bind(&SyncSourceAdmin::updateMapItem,
                                      this, _1);
    ops.m_deleteMapItem = boost::bind(&SyncSourceAdmin::deleteMapItem,
                                      this, _1);
    ops.m_endSession.push_back(boost::bind(&SyncSourceAdmin::flush,
                                           this));
}

void SyncSourceAdmin::init(SyncSource::Operations &ops,
                           SyncSource *source)
{
    init(ops,
         source->getProperties(true),
         SourceAdminDataName,
         source->getServerNode());
}

void SyncSourceBlob::init(SyncSource::Operations &ops,
                          const std::string &dir)
{
    m_blob.Init(getSynthesisAPI(),
                getName(),
                dir, "", "", "");
    ops.m_readBlob = boost::bind(&SyncSourceBlob::readBlob, this,
                                 _1, _2, _3, _4, _5, _6, _7);
    ops.m_writeBlob = boost::bind(&SyncSourceBlob::writeBlob, this,
                                  _1, _2, _3, _4, _5, _6, _7);
    ops.m_deleteBlob = boost::bind(&SyncSourceBlob::deleteBlob, this,
                                   _1, _2);
}

SE_END_CXX

