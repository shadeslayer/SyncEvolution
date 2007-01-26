/*
 * Copyright (C) 2005-2006 Patrick Ohly
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

#include "EvolutionSyncClient.h"
#include "EvolutionSyncSource.h"

#include <client/DMTClientConfig.h>
#include <posix/base/posixlog.h>

#include <list>
#include <memory>
#include <vector>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
using namespace std;

#include <sys/stat.h>
#include <pwd.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>

EvolutionSyncClient::EvolutionSyncClient(const string &server,
                                         bool doLogging, const set<string> &sources) :
    m_server(server),
    m_sources(sources),
    m_doLogging(doLogging),
    m_configPath(string("evolution/") + server)
{
}

EvolutionSyncClient::~EvolutionSyncClient()
{
}

/// remove all files in the given directory and the directory itself
static void rmBackupDir(const string &dirname)
{
    DIR *dir = opendir(dirname.c_str());
    if (!dir) {
        throw runtime_error(dirname + ": " + strerror(errno));
    }
    vector<string> entries;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        entries.push_back(entry->d_name);
    }
    closedir(dir);

    for (vector<string>::iterator it = entries.begin();
         it != entries.end();
         ++it) {
        string path = dirname + "/" + *it;
        if (unlink(path.c_str())
            && errno != ENOENT
#ifdef EISDIR
            && errno != EISDIR
#endif
            ) {
            throw runtime_error(path + ": " + strerror(errno));
        }
    }

    if (rmdir(dirname.c_str())) {
        throw runtime_error(dirname + ": " + strerror(errno));
    }
}

// this class owns the logging directory and is responsible
// for redirecting output at the start and end of sync (even
// in case of exceptions thrown!)
class LogDir {
    string m_logdir;         /**< configured backup root dir, empty if none */
    int m_maxlogdirs;        /**< number of backup dirs to preserve, 0 if unlimited */
    string m_prefix;         /**< common prefix of backup dirs */
    string m_path;           /**< path to current logging and backup dir */
    string m_logfile;        /**< path to log file there */
    const string &m_server;  /**< name of the server for this synchronization */
    LogLevel m_oldLogLevel;  /**< logging level to restore */
    

public:
    LogDir(const string &server) : m_server(server),
                                   m_oldLogLevel(LOG.getLevel())
        {}
        
    // setup log directory and redirect logging into it
    // @param path        path to configured backup directy, NULL if defaulting to /tmp
    // @param maxlogdirs  number of backup dirs to preserve in path, 0 if unlimited
    void setLogdir(const char *path, int maxlogdirs) {
        m_maxlogdirs = maxlogdirs;
        if (path && path[0]) {
            m_logdir = path;

            // create unique directory name in the given directory
            time_t ts = time(NULL);
            struct tm *tm = localtime(&ts);
            stringstream base;
            // SyncEvolution-<server>-<yyyy>-<mm>-<dd>-<hh>-<mm>
            m_prefix = "SyncEvolution-";
            m_prefix += m_server;
            base << path << "/"
                 << m_prefix
                 << "-"
                 << setfill('0')
                 << setw(4) << tm->tm_year + 1900 << "-"
                 << setw(2) << tm->tm_mon << "-"
                 << setw(2) << tm->tm_mday << "-"
                 << setw(2) << tm->tm_hour << "-"
                 << setw(2) << tm->tm_min;
            int seq = 0;
            while (true) {
                stringstream path;
                path << base.str();
                if (seq) {
                    path << "-" << seq;
                }
                m_path = path.str();
                if (!mkdir(m_path.c_str(), S_IRWXU)) {
                    break;
                }
                if (errno != EEXIST) {
                    throw runtime_error(m_path + ": " + strerror(errno));
                }
                seq++;
            }
        } else {
            // create temporary directory: $TMPDIR/SyncEvolution-<username>
            stringstream path;
            char *tmp = getenv("TMPDIR");
            if (tmp) {
                path << tmp;
            } else {
                path << "/tmp";
            }
            path << "/SyncEvolution-";
            struct passwd *user = getpwuid(getuid());
            if (user && user->pw_name) {
                path << user->pw_name;
            } else {
                path << getuid();
            }
            path << "-" << m_server;

            m_path = path.str();
            if (mkdir(m_path.c_str(), S_IRWXU)) {
                if (errno != EEXIST) {
                    throw runtime_error(m_path + ": " + strerror(errno));
                }
            }
        }

        // redirect logging into that directory, including stderr,
        // after truncating it
        m_logfile = m_path + "/client.log";
        ofstream out;
        out.exceptions(ios_base::badbit|ios_base::failbit|ios_base::eofbit);
        out.open(m_logfile.c_str());
        out.close();
        setLogFile(m_logfile.c_str(), true);
        LOG.setLevel(LOG_LEVEL_DEBUG);
    }

    // return log directory, empty if not enabled
    const string &getLogdir() {
        return m_path;
    }

    // return log file, empty if not enabled
    const string &getLogfile() {
        return m_logfile;
    }

    // remove oldest backup dirs if exceeding limit
    void expire() {
        if (m_logdir.size() && m_maxlogdirs > 0 ) {
            DIR *dir = opendir(m_logdir.c_str());
            if (!dir) {
                throw runtime_error(m_logdir + ": " + strerror(errno));
            }
            vector<string> entries;
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL) {
                if (strlen(entry->d_name) >= m_prefix.size() &&
                    !m_prefix.compare(0, m_prefix.size(), entry->d_name, m_prefix.size())) {
                    entries.push_back(entry->d_name);
                }
            }
            closedir(dir);
                
            sort(entries.begin(), entries.end());

            unsigned int deleted = 0;
            for (vector<string>::iterator it = entries.begin();
                 it != entries.end() && entries.size() - deleted > m_maxlogdirs;
                 ++it, ++deleted) {
                string path = m_logdir + "/" + *it;
                string msg = "removing " + path;
                LOG.info(msg.c_str());
                rmBackupDir(path);
            }
        }
    }

    // remove redirection of stderr and (optionally) also of logging
    void restore(bool all) {
        if (all) {
            setLogFile("-", false);
            LOG.setLevel(m_oldLogLevel);
        } else {
            setLogFile(m_logfile.c_str(), false);
        }
    }

    ~LogDir() {
        restore(true);
    }
};

// this class owns the sync sources and (together with
// a logdir) handles writing of per-sync files as well
// as the final report (
class SourceList : public list<EvolutionSyncSource *> {
    LogDir m_logdir;     /**< our logging directory */
    bool m_doLogging;    /**< true iff additional files are to be written during sync */
    bool m_reportTodo;   /**< true if syncDone() shall print a final report */
    arrayptr<SyncSource *> m_sourceArray;  /** owns the array that is expected by SyncClient::sync() */

    string databaseName(EvolutionSyncSource &source, const string suffix) {
        return m_logdir.getLogdir() + "/" +
            source.getName() + "." + suffix + "." +
            source.fileSuffix();
    }

    void dumpDatabases(const string &suffix) {
        ofstream out;
        out.exceptions(ios_base::badbit|ios_base::failbit|ios_base::eofbit);

        for( iterator it = begin();
             it != end();
             ++it ) {
            string file = databaseName(**it, suffix);
            out.open(file.c_str());
            (*it)->exportData(out);
            out.close();
        }
    }
        
public:
    SourceList(const string &server, bool doLogging) :
        m_logdir(server),
        m_doLogging(doLogging),
        m_reportTodo(false) {
    }
    
    // call as soon as logdir settings are known
    void setLogdir(const char *logDirPath, int maxlogdirs) {
        if (m_doLogging) {
            m_logdir.setLogdir(logDirPath, maxlogdirs);
        } else {
            // at least increase log level
            LOG.setLevel(LOG_LEVEL_DEBUG);
        }
    }

    // call when all sync sources are ready to dump
    // pre-sync databases
    void syncPrepare() {
        if (m_doLogging) {
            m_reportTodo = true;

            // dump initial databases
            dumpDatabases("before");
        }
    }

    // call at the end of a sync with success == true
    // if all went well to print report
    void syncDone(bool success) {
        if (m_doLogging) {
            // ensure that stderr is seen again
            m_logdir.restore(false);

            if (m_reportTodo) {
                // haven't looked at result of sync yet;
                // don't do it again
                m_reportTodo = false;

                // dump datatbase after sync
                dumpDatabases("after");

                // scan for error messages
                ifstream in;
                in.open(m_logdir.getLogfile().c_str());
                while (in.good()) {
                    string line;
                    getline(in, line);
                    if (line.find("[ERROR]") != line.npos) {
                        success = false;
                        cout << line << "\n";
                    } else if (line.find("[INFO]") != line.npos) {
                        cout << line << "\n";
                    }
                }
                in.close();
                cout << flush;

                cout << "\n";
                if (success) {
                    cout << "Synchronization successful.\n";
                } else {
                    cout << "Synchronization failed, see "
                         << m_logdir.getLogdir()
                         << " for details.\n";
                }

                // compare databases
                cout << "\nModifications:\n";
                for( iterator it = begin();
                     it != end();
                     ++it ) {
                    cout << "*** " << (*it)->getName() << " ***\n" << flush;

                    string before = databaseName(**it, "before");
                    string after = databaseName(**it, "after");
                    string cmd = string("synccompare '" ) +
                        before + "' '" + after +
                        "' && echo 'no changes'";
                    system(cmd.c_str());
                }
                cout << "\n";

                if (success) {
                    m_logdir.expire();
                }
            }
        }
    }

    /** returns current sources as array as expected by SyncClient::sync(), memory owned by this class */
    SyncSource **getSourceArray() {
        m_sourceArray = new SyncSource *[size() + 1];

        int index = 0;
        for (iterator it = begin();
             it != end();
             ++it) {
            ((SyncSource **)m_sourceArray)[index] = *it;
            index++;
        }
        ((SyncSource **)m_sourceArray)[index] = 0;
        return m_sourceArray;
    }
   
    ~SourceList() {
        // if we get here without a previous report,
        // something went wrong
        syncDone(false);

        // free sync sources
        for( iterator it = begin();
             it != end();
             ++it ) {
            delete *it;
        }
    }
};

void unref(SourceList *sourceList)
{
    delete sourceList;
}

int EvolutionSyncClient::sync()
{
    class EvolutionClientConfig : public DMTClientConfig {
    public:
        EvolutionClientConfig(const char *root) :
            DMTClientConfig(root) {}

    protected:
        /*
         * tweak the base class in two ways:
         * - continue to use the "syncml" node for all non-source properties, as in previous versions
         * - do not save properties which cannot be configured
         */
        virtual int readAuthConfig(ManagementNode& syncMLNode,
                                   ManagementNode& authNode) {
            return DMTClientConfig::readAuthConfig(syncMLNode, syncMLNode);
        }
        virtual void saveAuthConfig(ManagementNode& syncMLNode,
                                    ManagementNode& authNode) {
            DMTClientConfig::saveAuthConfig(syncMLNode, syncMLNode);
        }
        virtual int readConnConfig(ManagementNode& syncMLNode,
                                    ManagementNode& connNode) {
            return DMTClientConfig::readConnConfig(syncMLNode, syncMLNode);
        }
        virtual void saveConnConfig(ManagementNode& syncMLNode,
                                    ManagementNode& connNode) {
            DMTClientConfig::saveConnConfig(syncMLNode, syncMLNode);
        }
        virtual int readExtAccessConfig(ManagementNode& syncMLNode,
                                        ManagementNode& extNode) {
            return DMTClientConfig::readExtAccessConfig(syncMLNode, syncMLNode);
        }
        virtual void saveExtAccessConfig(ManagementNode& syncMLNode,
                                         ManagementNode& extNode) {
            DMTClientConfig::saveExtAccessConfig(syncMLNode, syncMLNode);
        }
        virtual int readDevInfoConfig(ManagementNode& syncMLNode,
                                      ManagementNode& devInfoNode) {
            int res = DMTClientConfig::readDevInfoConfig(syncMLNode, syncMLNode);

            // always read device ID from the traditional property "deviceId"
            arrayptr<char> tmp(syncMLNode.readPropertyValue("deviceId"));
            deviceConfig.setDevID(tmp);

            return res;
        }
        virtual void saveDevInfoConfig(ManagementNode& syncMLNode,
                                       ManagementNode& devInfoNode) {
            // these properties are always set by the code, don't save them
        }
        virtual int readDevDetailConfig(ManagementNode& syncMLNode,
                                        ManagementNode& devDetailNode) {
            return DMTClientConfig::readDevDetailConfig(syncMLNode, syncMLNode);
        }
        virtual void saveDevDetailConfig(ManagementNode& syncMLNode,
                                         ManagementNode& devDetailNode) {
            // these properties are always set by the code, don't save them
        }
        virtual int readExtDevConfig(ManagementNode& syncMLNode,
                                     ManagementNode& extNode) {
            return DMTClientConfig::readExtDevConfig(syncMLNode, syncMLNode);
        }
        virtual void saveExtDevConfig(ManagementNode& syncMLNode,
                                      ManagementNode& extNode) {
            // these properties are always set by the code, don't save them
        }

        virtual void saveSourceConfig(int i,
                                      ManagementNode& sourcesNode,
                                      ManagementNode& sourceNode) {
            // no, don't overwrite config, in particular not the "type"
        }

    } config(m_configPath.c_str());
    
    if (!config.read() || !config.open()) {
        throw runtime_error("reading configuration failed");
    }

    // remember for use by sync sources
    string url = config.getAccessConfig().getSyncURL() ? config.getAccessConfig().getSyncURL() : "";

    if (!url.size()) {
        LOG.error("no syncURL configured - perhaps the server name \"%s\" is wrong?",
                  m_server.c_str());
        throw runtime_error("cannot proceed without configuration");
    }

    // redirect logging as soon as possible
    SourceList sourceList(m_server, m_doLogging);

    arrayptr<char> logdir(config.getSyncMLNode()->readPropertyValue("logdir"));
    arrayptr<char> maxlogdirs(config.getSyncMLNode()->readPropertyValue("maxlogdirs"));
    sourceList.setLogdir(logdir, atoi(maxlogdirs));

    SyncSourceConfig *sourceconfigs = config.getSyncSourceConfigs();
    for (int index = 0; index < config.getNumSources(); index++) {
        ManagementNode &node(*config.getSyncSourceNode(index));
        SyncSourceConfig &sc(sourceconfigs[index]);
        
        // is the source enabled?
        string sync = sc.getSync() ? sc.getSync() : "";
        bool enabled = sync != "none";
        SyncMode overrideMode = SYNC_NONE;

        // override state?
        if (m_sources.size()) {
            if (m_sources.find(sc.getName()) != m_sources.end()) {
                if (!enabled) {
                    overrideMode = SYNC_TWO_WAY;
                    enabled = true;
                }
            } else {
                enabled = false;
            }
        }
        
        if (enabled) {
            // create it
            string type = sc.getType() ? sc.getType() : "";
            EvolutionSyncSource *syncSource =
                EvolutionSyncSource::createSource(
                    sc.getName(),
                    &sc,
                    string("sync4jevolution:") + url + "/" + sc.getName(),
                    EvolutionSyncSource::getPropertyValue(node, "evolutionsource"),
                    type
                    );
            if (!syncSource) {
                throw runtime_error(string(sc.getName()) + ": type " +
                                    ( type.size() ? string("not configured") :
                                      string("'") + type + "' empty or unknown" ));
            }
            sourceList.push_back(syncSource);

            // Update the backend configuration. The EvolutionClientConfig
            // above prevents that these modifications overwrite the user settings.
            sc.setType(syncSource->getMimeType());
            sc.setVersion(syncSource->getMimeVersion());
            sc.setSupportedTypes(syncSource->getSupportedTypes());

            if (overrideMode != SYNC_NONE) {
                // disabled source selected via source name
                syncSource->setPreferredSyncMode(overrideMode);
            }
            const string user(EvolutionSyncSource::getPropertyValue(node, "evolutionuser")),
                passwd(EvolutionSyncSource::getPropertyValue(node, "evolutionpassword"));
            syncSource->setAuthentication(user, passwd);

            // also open it; failing now is still safe
            syncSource->open();    
        }
    }

    // reconfigure with our fixed properties
    DeviceConfig &dc(config.getDeviceConfig());
    dc.setVerDTD("1.1");
    dc.setMod("SyncEvolution");
    dc.setSwv(VERSION);
    dc.setMan("Patrick Ohly");
    dc.setDevType("workstation");
    dc.setUtc(1);
    dc.setOem("Open Source");

    // give derived class also a chance to update the configs
    prepare(config, sourceList.getSourceArray());

    // ready to go: dump initial databases and prepare for final report
    sourceList.syncPrepare();

    // do it
    int res = SyncClient::sync(config, sourceList.getSourceArray());

    if (res) {
        if (lastErrorCode && lastErrorMsg[0]) {
            throw runtime_error(lastErrorMsg);
        }
        // no error code/description?!
        throw runtime_error("sync failed without an error description, check log");
    }

    // store modified properties
    config.save();

    // all went well: print final report before cleaning up
    sourceList.syncDone(true);

    return 0;
}
