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

#include "EvolutionSyncSource.h"
#include "EvolutionSyncClient.h"
#include "SyncEvolutionUtil.h"
#include "Logging.h"

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/foreach.hpp>

#include <list>
using namespace std;

#include <dlfcn.h>

#ifdef HAVE_EDS
ESource *EvolutionSyncSource::findSource( ESourceList *list, const string &id )
{
    for (GSList *g = e_source_list_peek_groups (list); g; g = g->next) {
        ESourceGroup *group = E_SOURCE_GROUP (g->data);
        GSList *s;
        for (s = e_source_group_peek_sources (group); s; s = s->next) {
            ESource *source = E_SOURCE (s->data);
            char *uri = e_source_get_uri(source);
            bool found = id.empty() ||
                !id.compare(e_source_peek_name(source)) ||
                (uri && !id.compare(uri));
            g_free(uri);
            if (found) {
                return source;
            }
        }
    }
    return NULL;
}
#endif // HAVE_EDS

#ifdef HAVE_EDS
void EvolutionSyncSource::throwError(const string &action, GError *gerror)
{
    string gerrorstr;
    if (gerror) {
        gerrorstr += ": ";
        gerrorstr += gerror->message;
        g_clear_error(&gerror);
    } else {
        gerrorstr = ": failure";
    }

    throwError(action + gerrorstr);
}
#endif

void EvolutionSyncSource::throwError(const string &action, int error)
{
    throwError(action + ": " + strerror(error));
}

void EvolutionSyncSource::throwError(const string &failure)
{
    setFailed(true);
    EvolutionSyncClient::throwError(string(getName()) + ": " + failure);
}

void EvolutionSyncSource::resetItems()
{
    m_allItems.clear();
    m_newItems.clear();
    m_updatedItems.clear();
    m_deletedItems.clear();
}

void EvolutionSyncSource::handleException()
{
    SyncEvolutionException::handle();
    setFailed(true);
}

SourceRegistry &EvolutionSyncSource::getSourceRegistry()
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
    SourceRegistry &registry(EvolutionSyncSource::getSourceRegistry());

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

#if 0
static ostream & operator << (ostream &out, const RegisterSyncSource &rhs)
{
    out << rhs.m_shortDescr << (rhs.m_enabled ? " (enabled)" : " (disabled)");
}
#endif

EvolutionSyncSource *const RegisterSyncSource::InactiveSource = (EvolutionSyncSource *)1;

TestRegistry &EvolutionSyncSource::getTestRegistry()
{
    static TestRegistry testRegistry;
    return testRegistry;
}

RegisterSyncSourceTest::RegisterSyncSourceTest(const string &configName, const string &testCaseName) :
    m_configName(configName),
    m_testCaseName(testCaseName)
{
    EvolutionSyncSource::getTestRegistry().push_back(this);
}

static class ScannedModules {
public:
    ScannedModules() {
#ifdef ENABLE_MODULES
        list<string> *state;

        // possible extension: scan directories for matching module names instead of hard-coding known names
        const char *modules[] = {
            "syncebook.so.0",
            "syncecal.so.0",
            "syncsqlite.so.0",
            "syncfile.so.0",
            "addressbook.so.0",
            NULL
        };

        for (int i = 0; modules[i]; i++) {
            void *dlhandle;

            // Open the shared object so that backend can register
            // itself. We keep that pointer, so never close the
            // module!
            dlhandle = dlopen(modules[i], RTLD_NOW|RTLD_GLOBAL);
            if (!dlhandle) {
                string fullpath = LIBDIR "/syncevolution/";
                fullpath += modules[i];
                dlhandle = dlopen(fullpath.c_str(), RTLD_NOW|RTLD_GLOBAL);
            }
            // remember which modules were found and which were not
            state = dlhandle ? &m_available : &m_missing;
            state->push_back(modules[i]);
        }
#endif
    }
    list<string> m_available;
    list<string> m_missing;
} scannedModules;


EvolutionSyncSource *EvolutionSyncSource::createSource(const EvolutionSyncSourceParams &params, bool error)
{
    string sourceTypeString = getSourceTypeString(params.m_nodes);
    SourceType sourceType = EvolutionSyncSource::getSourceType(params.m_nodes);

    const SourceRegistry &registry(getSourceRegistry());
    BOOST_FOREACH(const RegisterSyncSource *sourceInfos, registry) {
        EvolutionSyncSource *source = sourceInfos->m_create(params);
        if (source) {
            if (source == RegisterSyncSource::InactiveSource) {
                EvolutionSyncClient::throwError(params.m_name + ": access to " + sourceInfos->m_shortDescr +
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
        if (scannedModules.m_missing.size()) {
            problem += ". The following backend(s) were not found: ";
            problem += boost::join(scannedModules.m_missing, ", ");
        }
        EvolutionSyncClient::throwError(problem);
    }

    return NULL;
}

EvolutionSyncSource *EvolutionSyncSource::createTestingSource(const string &name, const string &type, bool error,
                                                              const char *prefix)
{
    EvolutionSyncConfig config("testing");
    SyncSourceNodes nodes = config.getSyncSourceNodes(name);
    EvolutionSyncSourceParams params(name, nodes, "");
    PersistentEvolutionSyncSourceConfig sourceconfig(name, nodes);
    sourceconfig.setSourceType(type);
    if (prefix) {
        sourceconfig.setDatabaseID(string(prefix) + name + "_1");
    }
    return createSource(params, error);
}

void EvolutionSyncSource::getSynthesisInfo(string &profile,
                                           string &datatypes,
                                           string &native,
                                           XMLConfigFragments &fragments)
{
    string type = getMimeType();

    if (type == "text/x-vcard") {
        native = "vCard21";
        profile = "\"vCard\", 1";
        datatypes =
            "        <use datatype='vCard21' mode='rw' preferred='yes'/>\n"
            "        <use datatype='vCard30' mode='rw'/>\n";
    } else if (type == "text/vcard") {
        native = "vCard30";
        profile = "\"vCard\", 2";
        datatypes =
            "        <use datatype='vCard21' mode='rw'/>\n"
            "        <use datatype='vCard30' mode='rw' preferred='yes'/>\n";
    } else if (type == "text/x-calendar") {
        native = "vCalendar10";
        profile = "\"vCalendar\", 1";
        datatypes =
            "        <use datatype='vCalendar10' mode='rw' preferred='yes'/>\n"
            "        <use datatype='iCalendar20' mode='rw'/>\n";
    } else if (type == "text/calendar") {
        native = "iCalendar20";
        profile = "\"vCalendar\", 2";
        datatypes =
            "        <use datatype='vCalendar10' mode='rw'/>\n"
            "        <use datatype='iCalendar20' mode='rw' preferred='yes'/>\n";
    } else if (type == "text/plain") {
        profile = "\"Note\", 2";
    } else {
        throwError(string("default MIME type not supported: ") + type);
    }

    SourceType sourceType = getSourceType();
    if (!sourceType.m_format.empty()) {
        type = sourceType.m_format;
    }

    if (type == "text/x-vcard:2.1" || type == "text/x-vcard") {
        datatypes =
            "        <use datatype='vCard21' mode='rw' preferred='yes'/>\n";
        if (!sourceType.m_forceFormat) {
            datatypes +=
                "        <use datatype='vCard30' mode='rw'/>\n";
        }
    } else if (type == "text/vcard:3.0" || type == "text/vcard") {
        datatypes =
            "        <use datatype='vCard30' mode='rw' preferred='yes'/>\n";
        if (!sourceType.m_forceFormat) {
            datatypes +=
                "        <use datatype='vCard21' mode='rw'/>\n";
        }
    } else if (type == "text/x-vcalendar:2.0" || type == "text/x-vcalendar") {
        datatypes =
            "        <use datatype='vcalendar10' mode='rw' preferred='yes'/>\n";
        if (!sourceType.m_forceFormat) {
            datatypes +=
                "        <use datatype='icalendar20' mode='rw'/>\n";
        }
    } else if (type == "text/calendar:2.0" || type == "text/calendar") {
        datatypes =
            "        <use datatype='icalendar20' mode='rw' preferred='yes'/>\n";
        if (!sourceType.m_forceFormat) {
            datatypes +=
                "        <use datatype='vcalendar10' mode='rw'/>\n";
        }
    } else if (type == "text/plain:1.0" || type == "text/plain") {
        // note10 are the same as note11, so ignore force format
        datatypes =
            "        <use datatype='note10' mode='rw' preferred='yes'/>\n"
            "        <use datatype='note11' mode='rw'/>\n";
    } else {
        throwError(string("configured MIME type not supported: ") + type);
    }
}

void EvolutionSyncSource::getDatastoreXML(string &xml, XMLConfigFragments &fragments)
{
    stringstream xmlstream;
    string profile;
    string datatypes;
    string native;

    getSynthesisInfo(profile, datatypes, native, fragments);

    xmlstream <<
        "      <plugin_module>SyncEvolution</plugin_module>\n"
        "      <plugin_datastoreadmin>no</plugin_datastoreadmin>\n"
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
        "      <!-- on slowsync: duplicate items that are not fully equal\n"
        "           You can set this to 'newer-wins' as well to avoid\n"
        "           duplicates as much as possible\n"
        "      -->\n"
        "      <slowsyncstrategy>duplicate</slowsyncstrategy>\n"
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
        "\n"
        "      <!-- Mapping of the fields to the fieldlist 'contacts' -->\n"
        "      <fieldmap fieldlist='contacts'>\n"
        "        <initscript><![CDATA[\n"
        "           string itemdata;\n"
        "        ]]></initscript>\n"
        "        <beforewritescript><![CDATA[\n"
        "           itemdata = MAKETEXTWITHPROFILE(" << profile << ", \"EVOLUTION\");\n"
        "        ]]></beforewritescript>\n"
        "        <afterreadscript><![CDATA[\n"
        "           PARSETEXTWITHPROFILE(itemdata, " << profile << ", \"EVOLUTION\");\n"
        "        ]]></afterreadscript>\n"
        "        <map name='data' references='itemdata' type='string'/>\n"
        "      </fieldmap>\n"
        "\n"
        "      <!-- datatypes supported by this datastore -->\n"
        "      <typesupport>\n" <<
        datatypes <<
        "      </typesupport>\n";

    xml = xmlstream.str();
}

SyncMLStatus EvolutionSyncSource::beginSync(SyncMode mode) throw()
{
    // start background thread if not running yet:
    // necessary to catch problems with Evolution backend
    EvolutionSyncClient::startLoopThread();

    try {
        // @TODO: force slow sync if something goes wrong
        //
        // reset anchors now, once we proceed there is no going back
        // (because the change marker is about to be moved)
        // and the sync must either complete or result in a slow sync
        // the next time
        
        const char *error = getenv("SYNCEVOLUTION_BEGIN_SYNC_ERROR");
        if (error && strstr(error, getName())) {
            EvolutionSyncClient::throwError("artificial error in beginSync()");
        }

        // reset state
        m_isModified = false;
        m_allItems.clear();
        m_newItems.clear();
        m_updatedItems.clear();
        m_deletedItems.clear();

        // determine what to do
        bool needAll = false;
        bool needPartial = false;
        bool deleteLocal = false;
        switch (mode) {
         case SYNC_SLOW:
            needAll = true;
            m_isModified = true;
            break;
         case SYNC_ONE_WAY_FROM_CLIENT:
         case SYNC_TWO_WAY:
            needPartial = true;
            break;
         case SYNC_REFRESH_FROM_SERVER:
            deleteLocal = true;
            m_isModified = true;
            break;
         case SYNC_REFRESH_FROM_CLIENT:
            needAll = true;
            m_isModified = true;
            break;
         case SYNC_NONE:
            // special mode for testing: prepare both all and partial lists
            needAll = needPartial = true;
            break;
         case SYNC_ONE_WAY_FROM_SERVER:
            // nothing to do, just wait for server's changes
            break;
         default:
            EvolutionSyncClient::throwError("unsupported sync mode, valid are only: slow, two-way, refresh");
            break;
        }

        beginSyncThrow(needAll, needPartial, deleteLocal);

        // This code here puts iterators in a state
        // where iterating with nextItem() is possible.
        rewindItems();
    } catch( ... ) {
        handleException();
        return STATUS_FATAL;
    }
    return STATUS_OK;
}

void EvolutionSyncSource::rewindItems() throw()
{
    m_allItems.rewind();
}

SyncItem::State EvolutionSyncSource::nextItem(string *data, string &luid) throw()
{
    cxxptr<SyncItem> item(m_allItems.iterate(data ? false : true));
    SyncItem::State state = SyncItem::NO_MORE_ITEMS;

    if (item) {
        if (m_newItems.find(item->getKey()) != m_newItems.end()) {
            state = SyncItem::NEW;
        } else if (m_updatedItems.find(item->getKey()) != m_updatedItems.end()) {
            state = SyncItem::UPDATED;
        } else {
            state = SyncItem::UNCHANGED;
        }
        if (data) {
            data->assign((const char *)item->getData(), item->getDataSize());
        }
        luid = item->getKey();
    }
    return state;
}

SyncMLStatus EvolutionSyncSource::endSync() throw()
{
    try {
        endSyncThrow();
    } catch ( ... ) {
        handleException();
    }

    // @TODO: Do _not_ tell the caller (SyncManager) if an error occurred
    // because that causes Sync4jClient to abort processing for all
    // sync sources. Instead deal with failed sync sources in
    // EvolutionSyncClient::sync().
    return STATUS_OK;
}

SyncMLStatus EvolutionSyncSource::addItem(SyncItem& item) throw()
{
    return processItem("add", &EvolutionSyncSource::addItemThrow, item, true);
}

SyncMLStatus EvolutionSyncSource::updateItem(SyncItem& item) throw()
{
    return processItem("update", &EvolutionSyncSource::updateItemThrow, item, true);
}

SyncMLStatus EvolutionSyncSource::deleteItem(SyncItem& item) throw()
{
    SyncMLStatus status = processItem("delete", &EvolutionSyncSource::deleteItemThrow, item, false);
    if (status == STATUS_OK) {
        incrementNumDeleted();
    }
    return status;
}

SyncMLStatus EvolutionSyncSource::removeAllItems() throw()
{
    SyncMLStatus status = STATUS_OK;
    
    try {
        BOOST_FOREACH(const string &key, m_allItems) {
            SyncItem item;
            item.setKey(key.c_str());
            logItem(item, "delete all items");
            deleteItemThrow(item);
            incrementNumDeleted();
            m_isModified = true;
        }
    } catch (...) {
        handleException();
        status = STATUS_FATAL;
    }
    return status;
}

SyncMLStatus EvolutionSyncSource::processItem(const char *action,
                                              SyncMLStatus (EvolutionSyncSource::*func)(SyncItem& item),
                                              SyncItem& item,
                                              bool needData) throw()
{
    SyncMLStatus status = STATUS_FATAL;
    
    try {
        logItem(item, action);
        if (needData && (item.getDataSize() < 0 || !item.getData())) {
            // Something went wrong in the server: update or add without data.
            // Shouldn't happen, but it did with one server and thus this
            // security check was added to prevent segfaults.
            logItem(item, "ignored due to missing data");
            status = STATUS_OK;
        } else {
            status = (this->*func)(item);
        }
        m_isModified = true;
    } catch (...) {
        handleException();
    }
    databaseModified();
    return status;
}

void EvolutionSyncSource::sleepSinceModification(int seconds)
{
    time_t current = time(NULL);
    while (current - m_modTimeStamp < seconds) {
        sleep(seconds - (current - m_modTimeStamp));
        current = time(NULL);
    }
}

void EvolutionSyncSource::databaseModified()
{
    m_modTimeStamp = time(NULL);
}

void EvolutionSyncSource::logItemUtil(const string data, const string &mimeType, const string &mimeVersion,
                                      const string &uid, const string &info, bool debug)
{
    if (getLevel() >= (debug ? Logger::DEBUG : Logger::INFO)) {
        string name;

        if (mimeType == "text/plain") {
            size_t eol = data.find('\n');
            if (eol != data.npos) {
                name.assign(data, 0, eol);
            } else {
                name = data;
            }
        } else {
            // Avoid pulling in a full vCard/iCalendar parser by just
            // searching for a specific property. This is rather crude
            // and does not handle encoding correctly at the moment, too.
            string prop;
            
            if (mimeType == "text/vcard" ||
                mimeType == "text/x-vcard") {
                prop = "FN";
            } else if (mimeType == "text/calendar" ||
                       mimeType == "text/x-calendar") {
                prop = "SUMMARY";
            }

            if (prop.size()) {
                size_t start = 0;

                while (start < data.size()) {
                    start = data.find(prop, start);
                    if (start == data.npos) {
                        break;
                    }
                    // must follow line break and continue with
                    // : or ;
                    if (start > 0 && data[start - 1] == '\n' &&
                    start + prop.size() < data.size() &&
                        (data[start + prop.size()] == ';' ||
                         data[start + prop.size()] == ':')) {
                        start = data.find(':', start);
                        if (start != data.npos) {
                            start++;
                            size_t end = data.find_first_of("\n\r", start);
                            name.assign(data,
                                        start,
                                        end == data.npos ? data.npos : (end - start));
                        }
                        break;
                    } else {
                        start += prop.size();
                    }
                }
            }
        }

        if (name.size()) {
            SE_LOG(debug ? Logger::DEBUG : Logger::INFO, this, NULL,
                   "%s %s",
                   name.c_str(),
                   info.c_str());
        } else {
            SE_LOG(debug ? Logger::DEBUG : Logger::INFO, this, NULL,
                   "LUID %s %s",
                   uid.c_str(),
                   info.c_str());
        }
    }
}

void EvolutionSyncSource::setLevel(Level level)
{
    LoggerBase::instance().setLevel(level);
}

Logger::Level EvolutionSyncSource::getLevel()
{
    return LoggerBase::instance().getLevel();
}

void EvolutionSyncSource::messagev(Level level,
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

SyncItem *EvolutionSyncSource::Items::start()
{
    m_it = begin();
    SE_LOG_DEBUG(&m_source, NULL, "start scanning %s items", m_type.c_str());
    return iterate();
}

SyncItem *EvolutionSyncSource::Items::iterate(bool idOnly)
{
    if (m_it != end()) {
        const string &uid( *m_it );
        SE_LOG_DEBUG(&m_source, NULL, "next %s item: %s", m_type.c_str(), uid.c_str());
        ++m_it;
        if (&m_source.m_deletedItems == this || idOnly) {
            // just tell caller the uid of the (possibly deleted) item
            cxxptr<SyncItem> item(new SyncItem());
            item->setKey(uid);
            return item.release();
        } else {
            // retrieve item with all its data
            try {
                cxxptr<SyncItem> item(m_source.createItem(uid));
                return item.release();
            } catch(...) {
                m_source.handleException();
                return NULL;
            }
        }
    } else {
        return NULL;
    }
}

bool EvolutionSyncSource::Items::addItem(const string &uid) {
    pair<iterator, bool> res = insert(uid);
    if (res.second) {
        m_source.logItem(uid, m_type, true);
    }
    return res.second;
}
