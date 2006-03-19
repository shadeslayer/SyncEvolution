/*
 * Copyright (C) 2005 Patrick Ohly
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

#include <spdm/DMTree.h>
#include <posix/base/posixlog.h>

#include <list>
#include <memory>
#include <vector>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <iostream>
using namespace std;

#include <sys/stat.h>
#include <pwd.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>

EvolutionSyncClient::EvolutionSyncClient(const string &server) :
    m_client(Sync4jClient::getInstance()),
    m_server(server),
    m_configPath(string("evolution/") + server)
{
    LOG.setLevel(LOG_LEVEL_INFO);
    m_client.setDMConfig(m_configPath.c_str());
}

EvolutionSyncClient::~EvolutionSyncClient()
{
    Sync4jClient::dispose();
}

/// remove all files in the given directory and the directory itself
static void rmBackupDir(const string &dirname)
{
    DIR *dir = opendir(dirname.c_str());
    if (!dir) {
        throw dirname + ": " + strerror(errno);
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
            throw path + ": " + strerror(errno);
        }
    }

    if (rmdir(dirname.c_str())) {
        throw dirname + ": " + strerror(errno);
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

public:
    LogDir(const string &server) : m_server(server) {}
        
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
                 << tm->tm_mday << "-"
                 << tm->tm_hour << "-"
                 << tm->tm_min;
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
                    throw m_path + ": " + strerror(errno);
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
                    throw m_path + ": " + strerror(errno);
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
                throw m_logdir + ": " + strerror(errno);
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

            int deleted = 0;
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
            LOG.setLevel(LOG_LEVEL_INFO);
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
                    string cmd = string("normalize_vcard '" ) +
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

void EvolutionSyncClient::sync(SyncMode syncMode, bool doLogging)
{
    SourceList sources(m_server, doLogging);

    DMTree config(m_configPath.c_str());

    // find server URL (part of change id)
    string serverPath = m_configPath + "/spds/syncml";
    auto_ptr<ManagementNode> serverNode(config.getManagementNode(serverPath.c_str()));
    string url = EvolutionSyncSource::getPropertyValue(*serverNode, "syncURL");

    // redirect logging as soon as possible
    sources.setLogdir(serverNode->getPropertyValue("logdir"),
                      atoi(serverNode->getPropertyValue("maxlogdirs")));

    // find sources
    string sourcesPath = m_configPath + "/spds/sources";
    auto_ptr<ManagementNode> sourcesNode(config.getManagementNode(sourcesPath.c_str()));
    int index, numSources = sourcesNode->getChildrenMaxCount();
    char **sourceNamesPtr = sourcesNode->getChildrenNames();

    // copy source names into format that will be
    // freed in case of exception
    vector<string> sourceNames;
    for ( index = 0; index < numSources; index++ ) {
        sourceNames.push_back(sourceNamesPtr[index]);
        delete [] sourceNamesPtr[index];
    }
    delete [] sourceNamesPtr;
    
    // iterate over sources
    for ( index = 0; index < numSources; index++ ) {
        // is the source enabled?
        string sourcePath(sourcesPath + "/" + sourceNames[index]);
        auto_ptr<ManagementNode> sourceNode(config.getManagementNode(sourcePath.c_str()));
        string disabled = EvolutionSyncSource::getPropertyValue(*sourceNode, "disabled");
        if (disabled != "T" && disabled != "t") {
            // create it
            string type = EvolutionSyncSource::getPropertyValue(*sourceNode, "type");
            EvolutionSyncSource *syncSource =
                EvolutionSyncSource::createSource(
                    sourceNames[index],
                    string("sync4jevolution:") + url + "/" + EvolutionSyncSource::getPropertyValue(*sourceNode, "name"),
                    EvolutionSyncSource::getPropertyValue(*sourceNode, "evolutionsource"),
                    type
                    );
            if (!syncSource) {
                throw sourceNames[index] + ": type " +
                    ( type.size() ? string("not configured") :
                      string("'") + type + "' empty or unknown" );
            }
            syncSource->setPreferredSyncMode( syncMode );
            sources.push_back(syncSource);

            // also open it; failing now is still safe
            syncSource->open();
        }
    }

    if (!sources.size()) {
        throw string("no sources configured");
    }

    // ready to go: dump initial databases and prepare for final report
    sources.syncPrepare();
    
    // build array as sync wants it, then sync
    // (no exceptions allowed here)
    SyncSource **sourceArray = new SyncSource *[sources.size() + 1];
    index = 0;
    for ( list<EvolutionSyncSource *>::iterator it = sources.begin();
          it != sources.end();
          ++it ) {
        sourceArray[index] = *it;
        ++index;
    }
    sourceArray[index] = NULL;
    int res = m_client.sync( sourceArray );
    delete [] sourceArray;

    // TODO: force slow sync in case of failure or failed Evolution source
    
    if (res) {
        if (lastErrorCode) {
            throw lastErrorCode;
        }
        // no error code?!
        lastErrorCode = res;
        if (!lastErrorMsg[0]) {
            strcpy(lastErrorMsg, "sync() failed without setting an error description");
        }
        throw res;
    }

    // all went well: print final report before cleaning up
    sources.syncDone(true);
}
