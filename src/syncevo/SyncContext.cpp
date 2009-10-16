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

#include <syncevo/SyncContext.h>
#include <syncevo/SyncSource.h>
#include <syncevo/util.h>

#include <syncevo/SafeConfigNode.h>
#include <syncevo/FileConfigNode.h>

#include <syncevo/LogStdout.h>
#include <syncevo/TransportAgent.h>
#include <syncevo/CurlTransportAgent.h>
#include <syncevo/SoupTransportAgent.h>

#include <list>
#include <memory>
#include <vector>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <ctime>
using namespace std;

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/foreach.hpp>
#include <boost/algorithm/string/split.hpp>

#include <sys/stat.h>
#include <pwd.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>

#include <synthesis/enginemodulebridge.h>
#include <synthesis/SDK_util.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

SourceList *SyncContext::m_sourceListPtr;

SuspendFlags SyncContext::s_flags;

extern "C" void suspend_handler(int sig)
{
  time_t current;
  time (&current);
  SuspendFlags& s_flags = SyncContext::getSuspendFlags();
  //first time suspend or already aborted
  if (s_flags.state == SuspendFlags::CLIENT_NORMAL)
  {
      s_flags.state = SuspendFlags::CLIENT_SUSPEND;
      s_flags.last_suspend = current;
      SE_LOG_INFO(NULL, NULL,"Asking server to suspend...\nPress CTRL-C again quickly (within 2s) to stop sync immediately (can cause problems during next sync!)");
      return;
  }
  else 
  {
      if (current - s_flags.last_suspend
              < s_flags.ABORT_INTERVAL) 
      {
          s_flags.state = SuspendFlags::CLIENT_ABORT;
          SE_LOG_INFO(NULL, NULL, "Aborting sync as requested via CTRL-C ...");
      }
      else
      {
          s_flags.last_suspend = current;
          SE_LOG_INFO(NULL, NULL, "Suspend in progress...\nPress CTRL-C again quickly (within 2s) to stop sync immediately (can cause problems during next sync!)");
      }
  }
}


SyncContext::SyncContext(const string &server,
                                         bool doLogging,
                                         const set<string> &sources) :
    SyncConfig(server),
    m_server(server),
    m_sources(sources),
    m_doLogging(doLogging),
    m_quiet(false),
    m_dryrun(false)
{
}

SyncContext::~SyncContext()
{
}


// this class owns the logging directory and is responsible
// for redirecting output at the start and end of sync (even
// in case of exceptions thrown!)
class LogDir : public LoggerBase {
    SyncContext &m_client;
    Logger &m_parentLogger;  /**< the logger which was active before we started to intercept messages */
    string m_logdir;         /**< configured backup root dir */
    int m_maxlogdirs;        /**< number of backup dirs to preserve, 0 if unlimited */
    string m_prefix;         /**< common prefix of backup dirs */
    string m_path;           /**< path to current logging and backup dir */
    string m_logfile;        /**< Path to log file there, empty if not writing one.
                                  The file is no longer written by this class, nor
                                  does it control the basename of it. Writing the
                                  log file is enabled by the XML configuration that
                                  we prepare for the Synthesis engine; the base name
                                  of the file is hard-coded in the engine. Despite
                                  that this class still is the central point to ask
                                  for the name of the log file. */
    SafeConfigNode *m_info;  /**< key/value representation of sync information */
    bool m_readonly;         /**< m_info is not to be written to */
    SyncReport *m_report;    /**< record start/end times here */

    /** set m_logdir and adapt m_prefix accordingly */
    void setLogdir(const string &logdir) {
        // strip trailing slashes, but not the initial one
        size_t off = logdir.size();
        while (off > 0 && logdir[off - 1] == '/') {
            off--;
        }
        m_logdir = logdir.substr(0, off);

        string lower = m_client.getServer();
        boost::to_lower(lower);

        if (boost::iends_with(m_logdir, "syncevolution")) {
            // use just the server name as prefix
            m_prefix = lower;
        } else {
            // SyncEvolution-<server>-<yyyy>-<mm>-<dd>-<hh>-<mm>
            m_prefix = "SyncEvolution-";
            m_prefix += lower;
        }
    }

public:
    LogDir(SyncContext &client) : m_client(client), m_parentLogger(LoggerBase::instance()), m_info(NULL), m_readonly(false), m_report(NULL)
    {
        // Set default log directory. This will be overwritten with a user-specified
        // location later on, if one was selected by the user. SyncEvolution >= 0.9 alpha
        // and < 0.9 beta 2 used XDG_DATA_HOME because the logs and data base dumps
        // were not considered "non-essential data files". Because XDG_DATA_HOME is
        // searched for .desktop files and creating large amounts of other files there
        // slows down that search, the default was changed to XDG_CACHE_DIR.
        //
        // To migrate old installations seamlessly, this code here renames the old
        // default directory to the new one. Errors (like not found) are silently ignored.
        mkdir_p(SubstEnvironment("${XDG_CACHE_HOME}").c_str());
        rename(SubstEnvironment("${XDG_DATA_HOME}/applications/syncevolution").c_str(),
               SubstEnvironment("${XDG_CACHE_HOME}/syncevolution").c_str());

        setLogdir(SubstEnvironment("${XDG_CACHE_HOME}/syncevolution"));
    }

    /**
     * Finds previous log directories. Reports errors via exceptions.
     *
     * @param path        path to configured backup directy, NULL if defaulting to /tmp, "none" if not creating log file
     * @retval dirs       vector of full path names, oldest first
     */
    void previousLogdirs(const char *path, vector<string> &dirs) {
        string logdir;

        dirs.clear();
        if (path && !strcasecmp(path, "none")) {
            return;
        } else {
            if (path && path[0]) {
                setLogdir(SubstEnvironment(path));
            }
            getLogdirs(dirs);
        }
    }

    /**
     * Finds previous log directory. Returns empty string if anything went wrong.
     *
     * @param path        path to configured backup directy, NULL if defaulting to /tmp, "none" if not creating log file
     * @return full path of previous log directory, empty string if not found
     */
    string previousLogdir(const char *path) throw() {
        try {
            vector<string> dirs;
            previousLogdirs(path, dirs);
            return dirs.empty() ? "" : dirs.back();
        } catch (...) {
            Exception::handle();
            return "";
        }
    }

    /**
     * access existing log directory to extract status information
     */
    void openLogdir(const string &dir) {
        boost::shared_ptr<ConfigNode> filenode(new FileConfigNode(dir, "status.ini", true));
        m_info = new SafeConfigNode(filenode);
        m_info->setMode(false);
        m_readonly = true;
    }

    /**
     * read sync report for session selected with openLogdir()
     */
    void readReport(SyncReport &report) {
        report.clear();
        if (!m_info) {
            return;
        }
        *m_info >> report;
    }

    /**
     * write sync report for current session
     */
    void writeReport(SyncReport &report) {
        if (m_info) {
            *m_info << report;

            /* write in slightly different format and flush at the end */
            writeTimestamp("start", report.getStart(), false);
            writeTimestamp("end", report.getEnd(), true);
        }
    }

    // setup log directory and redirect logging into it
    // @param path        path to configured backup directy, empty for using default, "none" if not creating log file
    // @param maxlogdirs  number of backup dirs to preserve in path, 0 if unlimited
    // @param logLevel    0 = default, 1 = ERROR, 2 = INFO, 3 = DEBUG
    // @param usePath     write directly into path, don't create and manage subdirectories
    // @param report      record information about session here (may be NULL)
    // @param logname     the basename to be used for logs, traditionally "client" for syncs
    void startSession(const char *path, int maxlogdirs, int logLevel, bool usePath, SyncReport *report, const string &logname) {
        m_maxlogdirs = maxlogdirs;
        m_report = report;
        m_logfile = "";
        if (path && !strcasecmp(path, "none")) {
            m_path = "";
        } else {
            if (path && path[0]) {
                setLogdir(SubstEnvironment(path));
            }

            if (!usePath) {
                // create unique directory name in the given directory
                time_t ts = time(NULL);
                struct tm *tm = localtime(&ts);
                stringstream base;
                base << m_logdir << "/"
                     << m_prefix
                     << "-"
                     << setfill('0')
                     << setw(4) << tm->tm_year + 1900 << "-"
                     << setw(2) << tm->tm_mon + 1 << "-"
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
                    if (!isDir(m_path)) {
                        mkdir_p(m_path);
                        break;
                    } else {
                        seq++;
                    }
                }
            } else {
                m_path = m_logdir;
                if (mkdir(m_path.c_str(), S_IRWXU) &&
                    errno != EEXIST) {
                    SE_LOG_DEBUG(NULL, NULL, "%s: %s", m_path.c_str(), strerror(errno));
                    SyncContext::throwError(m_path, errno);
                }
            }
            m_logfile = m_path + "/" + "sysynclib_linux.html";
        }

        // update log level of default logger and our own replacement
        Level level;
        switch (logLevel) {
        case 0:
            // default for console output
            level = INFO;
            break;
        case 1:
            level = ERROR;
            break;
        case 2:
            level = INFO;
            break;
        default:
            if (m_logfile.empty()) {
                // no log file: print all information to the console
                level = DEBUG;
            } else {
                // have log file: avoid excessive output to the console,
                // full information is in the log file
                level = INFO;
            }
            break;
        }
        if (!usePath) {
            LoggerBase::instance().setLevel(level);
        }
        setLevel(level);
        LoggerBase::pushLogger(this);

        time_t start = time(NULL);
        if (m_report) {
            m_report->setStart(start);
        }
        if (!m_path.empty()) {
            boost::shared_ptr<ConfigNode> filenode(new FileConfigNode(m_path, "status.ini", false));
            m_info = new SafeConfigNode(filenode);
            m_info->setMode(false);
            writeTimestamp("start", start);
        }
    }

    /** sets a fixed directory for database files without redirecting logging */
    void setPath(const string &path) { m_path = path; }

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
            vector<string> dirs;
            getLogdirs(dirs);

            int deleted = 0;
            for (vector<string>::iterator it = dirs.begin();
                 it != dirs.end() && (int)dirs.size() - deleted > m_maxlogdirs;
                 ++it, ++deleted) {
                string &path = *it;
                string msg = "removing " + path;
                SE_LOG_INFO(NULL, NULL, "%s", msg.c_str());
                rm_r(path);
            }
        }
    }

    // remove redirection of logging
    void restore() {
        if (&LoggerBase::instance() == this) {
            LoggerBase::popLogger();
        }
        time_t end = time(NULL);
        if (m_report) {
            m_report->setEnd(end);
        }
        if (m_info) {
            if (!m_readonly) {
                writeTimestamp("end", end);
                if (m_report) {
                    writeReport(*m_report);
                }
                m_info->flush();
            }
            delete m_info;
            m_info = NULL;
        }
    }

    ~LogDir() {
        restore();
    }


    virtual void messagev(Level level,
                          const char *prefix,
                          const char *file,
                          int line,
                          const char *function,
                          const char *format,
                          va_list args)
    {
        if (m_client.getEngine().get()) {
            va_list argscopy;
            va_copy(argscopy, args);
            // once to Synthesis log, with full debugging
            m_client.getEngine().doDebug(level, prefix, file, line, function, format, argscopy);
            va_end(argscopy);
        }
        // always to parent (usually stdout)
        m_parentLogger.messagev(level, prefix, file, line, function, format, args);
    }

private:
    /** find all entries in a given directory, return as sorted array of full paths */
    void getLogdirs(vector<string> &dirs) {
        if (!isDir(m_logdir)) {
            return;
        }
        ReadDir dir(m_logdir);
        BOOST_FOREACH(const string &entry, dir) {
            if (boost::starts_with(entry, m_prefix)) {
                string remain = boost::erase_first_copy(entry, m_prefix);
                if(checkDirName(remain)) {
                    dirs.push_back(m_logdir + "/" + entry);
                }
            }
        }
        sort(dirs.begin(), dirs.end());
    }
    // check the dir name is conforming to what format we write
    bool checkDirName(const string& value) {
        const char* str = value.c_str();
        /** need check whether string after prefix is a valid date-time we wrote, format
         * should be -YYYY-MM-DD-HH-MM and optional sequence number */
        static char table[] = {'-','9','9','9','9', //year
                               '-','1','9', //month
                               '-','3','9', //date
                               '-','2','9', //hour
                               '-','5','9'  //minute
        };
        for(size_t i = 0; i < sizeof(table)/sizeof(table[0]) && *str; i++,str++) {
            switch(table[i]) {
                case '-':
                    if(*str != '-')
                        return false;
                    break;
                case '1':
                    if(*str < '0' || *str > '1')
                        return false;
                    break;
                case '2':
                    if(*str < '0' || *str > '2')
                        return false;
                    break;
                case '3':
                    if(*str < '0' || *str > '3')
                        return false;
                    break;
                case '5':
                    if(*str < '0' || *str > '5')
                        return false;
                    break;
                case '9':
                    if(*str < '0' || *str > '9')
                        return false;
                    break;
                default:
                    return false;
            };
        }
        return true;
    }

    // store time stamp in session info
    void writeTimestamp(const string &key, time_t val, bool flush = true) {
        if (m_info) {
            char buffer[160];
            struct tm tm;
            // be nice and store a human-readable date in addition the seconds since the epoch
            strftime(buffer, sizeof(buffer), "%s, %Y-%m-%d %H:%m:%S %z", localtime_r(&val, &tm));
            m_info->setProperty(key, buffer);
            if (flush) {
                m_info->flush();
            }
        }
    }
};

// this class owns the sync sources and (together with
// a logdir) handles writing of per-sync files as well
// as the final report (
class SourceList : public vector<SyncSource *> {
public:
    enum LogLevel {
        LOGGING_QUIET,    /**< avoid all extra output */
        LOGGING_SUMMARY,  /**< sync report, but no database comparison */
        LOGGING_FULL      /**< everything */
    };

private:
    LogDir m_logdir;     /**< our logging directory */
    bool m_prepared;     /**< remember whether syncPrepare() dumped databases successfully */
    bool m_doLogging;    /**< true iff the normal logdir handling is enabled
                            (creating and expiring directoties, before/after comparison) */
    bool m_reportTodo;   /**< true if syncDone() shall print a final report */
    LogLevel m_logLevel; /**< chooses how much information is printed */
    string m_previousLogdir; /**< remember previous log dir before creating the new one */

    /** create name in current (if set) or previous logdir */
    string databaseName(SyncSource &source, const string suffix, string logdir = "") {
        if (!logdir.size()) {
            logdir = m_logdir.getLogdir();
        }
        return logdir + "/" +
            source.getName() + "." + suffix;
    }

public:
    LogLevel getLogLevel() const { return m_logLevel; }
    void setLogLevel(LogLevel logLevel) { m_logLevel = logLevel; }

    /**
     * dump into files with a certain suffix,
     * optionally store report in member of SyncSourceReport
     */
    void dumpDatabases(const string &suffix,
                       BackupReport SyncSourceReport::*report) {
        BOOST_FOREACH(SyncSource *source, *this) {
            string dir = databaseName(*source, suffix);
            boost::shared_ptr<ConfigNode> node = ConfigNode::createFileNode(dir + ".ini");
            SE_LOG_DEBUG(NULL, NULL, "creating %s", dir.c_str());
            rm_r(dir);
            mkdir_p(dir);
            BackupReport dummy;
            if (source->getOperations().m_backupData) {
                source->getOperations().m_backupData(dir, *node,
                                                     report ? source->*report : dummy);
                SE_LOG_DEBUG(NULL, NULL, "%s created", dir.c_str());
            }
        }
    }

    void restoreDatabase(SyncSource &source, const string &suffix, bool dryrun, SyncSourceReport &report)
    {
        string dir = databaseName(source, suffix);
        boost::shared_ptr<ConfigNode> node = ConfigNode::createFileNode(dir + ".ini");
        if (!node->exists()) {
            SyncContext::throwError(dir + ": no such database backup found");
        }
        if (source.getOperations().m_restoreData) {
            source.getOperations().m_restoreData(dir, *node, dryrun, report);
        }
    }

    SourceList(SyncContext &client, bool doLogging) :
        m_logdir(client),
        m_prepared(false),
        m_doLogging(doLogging),
        m_reportTodo(true),
        m_logLevel(LOGGING_FULL)
    {
    }
    
    // call as soon as logdir settings are known
    void startSession(const char *logDirPath, int maxlogdirs, int logLevel, SyncReport *report,
                      const string &logname) {
        m_previousLogdir = m_logdir.previousLogdir(logDirPath);
        if (m_doLogging) {
            m_logdir.startSession(logDirPath, maxlogdirs, logLevel, false, report, logname);
        } else {
            // Run debug session without paying attention to
            // the normal logdir handling. The log level here
            // refers to stdout. The log file will be as complete
            // as possible.
            m_logdir.startSession(logDirPath, 0, 1, true, report, logname);
        }
    }

    /** return log directory, empty if not enabled */
    const string &getLogdir() {
        return m_logdir.getLogdir();
    }

    /** return previous log dir found in startSession() */
    const string &getPrevLogdir() const { return m_previousLogdir; }

    /** set directory for database files without actually redirecting the logging */
    void setPath(const string &path) { m_logdir.setPath(path); }

    /**
     * If possible (directory to compare against available) and enabled,
     * then dump changes applied locally.
     *
     * @param oldSuffix      suffix of old database dump: usually "after"
     * @param currentSuffix  the current database dump suffix: "current"
     *                       when not doing a sync, otherwise "before"
     */
    bool dumpLocalChanges(const string &oldDir,
                          const string &oldSuffix, const string &newSuffix,
                          const string &intro = "Local data changes to be applied to server during synchronization:\n",
                          const string &config = "CLIENT_TEST_LEFT_NAME='after last sync' CLIENT_TEST_RIGHT_NAME='current data' CLIENT_TEST_REMOVED='removed since last sync' CLIENT_TEST_ADDED='added since last sync'") {
        if (m_logLevel <= LOGGING_SUMMARY || oldDir.empty()) {
            return false;
        }

        cout << intro;
        BOOST_FOREACH(SyncSource *source, *this) {
            string oldFile = databaseName(*source, oldSuffix, oldDir);
            string newFile = databaseName(*source, newSuffix);
            cout << "*** " << source->getName() << " ***\n" << flush;
            string cmd = string("env CLIENT_TEST_COMPARISON_FAILED=10 " + config + " synccompare 2>/dev/null '" ) +
                oldFile + "' '" + newFile + "'";
            int ret = system(cmd.c_str());
            switch (ret == -1 ? ret : WEXITSTATUS(ret)) {
            case 0:
                cout << "no changes\n";
                break;
            case 10:
                break;
            default:
                cout << "Comparison was impossible.\n";
                break;
            }
        }
        cout << "\n";
        return true;
    }

    // call when all sync sources are ready to dump
    // pre-sync databases
    void syncPrepare() {
        if (m_logdir.getLogfile().size() &&
            m_doLogging) {
            // dump initial databases
            dumpDatabases("before", &SyncSourceReport::m_backupBefore);
            // compare against the old "after" database dump
            dumpLocalChanges(getPrevLogdir(), "after", "before");

            m_prepared = true;
        }
    }

    // call at the end of a sync with success == true
    // if all went well to print report
    void syncDone(SyncMLStatus status, SyncReport *report) {
        // record status - failures from now only affect post-processing
        // and thus do no longer change that result
        if (report) {
            report->setStatus(status == 0 ? STATUS_HTTP_OK : status);
        }

        if (m_doLogging) {
            // dump database after sync, but not if already dumping it at the beginning didn't complete
            if (m_reportTodo && m_prepared) {
                try {
                    dumpDatabases("after", &SyncSourceReport::m_backupAfter);
                } catch (...) {
                    Exception::handle();
                    m_prepared = false;
                }
                if (report) {
                    // update report with more recent information about m_backupAfter
                    updateSyncReport(*report);
                }
            }

            // ensure that stderr is seen again, also writes out session status
            m_logdir.restore();

            if (m_reportTodo) {
                // haven't looked at result of sync yet;
                // don't do it again
                m_reportTodo = false;

                string logfile = m_logdir.getLogfile();
                cout << flush;
                cerr << flush;
                cout << "\n";
                if (status == STATUS_OK) {
                    cout << "Synchronization successful.\n";
                } else if (logfile.size()) {
                    cout << "Synchronization failed, see "
                         << logfile
                         << " for details.\n";
                } else {
                    cout << "Synchronization failed.\n";
                }

                // pretty-print report
                if (m_logLevel > LOGGING_QUIET) {
                    cout << "\nChanges applied during synchronization:\n";
                }
                if (m_logLevel > LOGGING_QUIET && report) {
                    cout << *report;
                }

                // compare databases?
                if (m_logLevel > LOGGING_SUMMARY && m_prepared) {
                    cout << "\nChanges applied to client during synchronization:\n";
                    BOOST_FOREACH(SyncSource *source, *this) {
                        cout << "*** " << source->getName() << " ***\n" << flush;

                        string before = databaseName(*source, "before");
                        string after = databaseName(*source, "after");
                        string cmd = string("synccompare '" ) +
                            before + "' '" + after +
                            "' && echo 'no changes'";
                        if (system(cmd.c_str())) {
                            // ignore error
                        }
                    }
                    cout << "\n";
                }

                if (status == STATUS_OK) {
                    m_logdir.expire();
                }
            }
        }
    }

    /** copies information about sources into sync report */
    void updateSyncReport(SyncReport &report) {
        BOOST_FOREACH(SyncSource *source, *this) {
            report.addSyncSourceReport(source->getName(), *source);
        }
    }

    /** returns names of active sources */
    set<string> getSources() {
        set<string> res;

        BOOST_FOREACH(SyncSource *source, *this) {
            res.insert(source->getName());
        }
        return res;
    }
   
    ~SourceList() {
        // free sync sources
        BOOST_FOREACH(SyncSource *source, *this) {
            delete source;
        }
    }

    /** find sync source by name */
    SyncSource *operator [] (const string &name) {
        BOOST_FOREACH(SyncSource *source, *this) {
            if (name == source->getName()) {
                return source;
            }
        }
        return NULL;
    }

    /** find by index */
    SyncSource *operator [] (int index) { return vector<SyncSource *>::operator [] (index); }
};

void unref(SourceList *sourceList)
{
    delete sourceList;
}

string SyncContext::askPassword(const string &passwordName, const string &descr, const ConfigPasswordKey &key)
{
    char buffer[256];

    printf("Enter password for %s: ",
           descr.c_str());
    fflush(stdout);
    if (fgets(buffer, sizeof(buffer), stdin) &&
        strcmp(buffer, "\n")) {
        size_t len = strlen(buffer);
        if (len && buffer[len - 1] == '\n') {
            buffer[len - 1] = 0;
        }
        return buffer;
    } else {
        throwError(string("could not read password for ") + descr);
        return "";
    }
}

boost::shared_ptr<TransportAgent> SyncContext::createTransportAgent()
{
#ifdef ENABLE_LIBSOUP
    boost::shared_ptr<SoupTransportAgent> agent(new SoupTransportAgent());
#elif defined(ENABLE_LIBCURL)
    boost::shared_ptr<CurlTransportAgent> agent(new CurlTransportAgent());
#else
    boost::shared_ptr<TransportAgent> agent;
    throw std::string("libsyncevolution was compiled without default transport, client must implement SyncContext::createTransportAgent()");
#endif
    return agent;
}

void SyncContext::displayServerMessage(const string &message)
{
    SE_LOG_INFO(NULL, NULL, "message from server: %s", message.c_str());
}

void SyncContext::displaySyncProgress(sysync::TProgressEventEnum type,
                                              int32_t extra1, int32_t extra2, int32_t extra3)
{
    
}

void SyncContext::displaySourceProgress(sysync::TProgressEventEnum type,
                                                SyncSource &source,
                                                int32_t extra1, int32_t extra2, int32_t extra3)
{
    switch(type) {
    case sysync::PEV_PREPARING:
        /* preparing (e.g. preflight in some clients), extra1=progress, extra2=total */
        /* extra2 might be zero */
        if (extra2) {
            SE_LOG_INFO(NULL, NULL, "%s: preparing %d/%d",
                        source.getName(), extra1, extra2);
        } else {
            SE_LOG_INFO(NULL, NULL, "%s: preparing %d",
                        source.getName(), extra1);
        }
        break;
    case sysync::PEV_DELETING:
        /* deleting (zapping datastore), extra1=progress, extra2=total */
        if (extra2) {
            SE_LOG_INFO(NULL, NULL, "%s: deleting %d/%d",
                        source.getName(), extra1, extra2);
        } else {
            SE_LOG_INFO(NULL, NULL, "%s: deleting %d",
                        source.getName(), extra1);
        }
        break;
    case sysync::PEV_ALERTED: {
        /* datastore alerted (extra1=0 for normal, 1 for slow, 2 for first time slow, 
           extra2=1 for resumed session,
           extra3 0=twoway, 1=fromserver, 2=fromclient */
        SE_LOG_INFO(NULL, NULL, "%s: %s %s sync%s",
                    source.getName(),
                    extra2 ? "resuming" : "starting",
                    extra1 == 0 ? "normal" :
                    extra1 == 1 ? "slow" :
                    extra1 == 2 ? "first time" :
                    "unknown",
                    extra3 == 0 ? ", two-way" :
                    extra3 == 1 ? " from server" :
                    extra3 == 2 ? " from client" :
                    ", unknown direction");

        SyncMode mode = SYNC_NONE;
        switch (extra1) {
        case 0:
            switch (extra3) {
            case 0:
                mode = SYNC_TWO_WAY;
                break;
            case 1:
                mode = SYNC_ONE_WAY_FROM_SERVER;
                break;
            case 2:
                mode = SYNC_ONE_WAY_FROM_CLIENT;
                break;
            }
            break;
        case 1:
        case 2:
            switch (extra3) {
            case 0:
                mode = SYNC_SLOW;
                break;
            case 1:
                mode = SYNC_REFRESH_FROM_SERVER;
                break;
            case 2:
                mode = SYNC_REFRESH_FROM_CLIENT;
                break;
            }
            break;
        }
        source.recordFinalSyncMode(mode);
        source.recordFirstSync(extra1 == 2);
        source.recordResumeSync(extra2 == 1);
        break;
    }
    case sysync::PEV_SYNCSTART:
        /* sync started */
        SE_LOG_INFO(NULL, NULL, "%s: started",
                    source.getName());
        break;
    case sysync::PEV_ITEMRECEIVED:
        /* item received, extra1=current item count,
           extra2=number of expected changes (if >= 0) */
        if (extra2 > 0) {
            SE_LOG_INFO(NULL, NULL, "%s: received %d/%d",
                        source.getName(), extra1, extra2);
        } else {
            SE_LOG_INFO(NULL, NULL, "%s: received %d",
                     source.getName(), extra1);
        }
        break;
    case sysync::PEV_ITEMSENT:
        /* item sent,     extra1=current item count,
           extra2=number of expected items to be sent (if >=0) */
        if (extra2 > 0) {
            SE_LOG_INFO(NULL, NULL, "%s: sent %d/%d",
                     source.getName(), extra1, extra2);
        } else {
            SE_LOG_INFO(NULL, NULL, "%s: sent %d",
                     source.getName(), extra1);
        }
        break;
    case sysync::PEV_ITEMPROCESSED:
        /* item locally processed,               extra1=# added, 
           extra2=# updated,
           extra3=# deleted */
        SE_LOG_INFO(NULL, NULL, "%s: added %d, updated %d, removed %d",
                 source.getName(), extra1, extra2, extra3);
        break;
    case sysync::PEV_SYNCEND:
        /* sync finished, probably with error in extra1 (0=ok),
           syncmode in extra2 (0=normal, 1=slow, 2=first time), 
           extra3=1 for resumed session) */
        SE_LOG_INFO(NULL, NULL, "%s: %s%s sync done %s",
                 source.getName(),
                 extra3 ? "resumed " : "",
                 extra2 == 0 ? "normal" :
                 extra2 == 1 ? "slow" :
                 extra2 == 2 ? "first time" :
                 "unknown",
                 extra1 ? "unsuccessfully" : "successfully");
        switch (extra1) {
        case 401:
            // TODO: reset cached password
            SE_LOG_INFO(NULL, NULL, "authorization failed, check username '%s' and password", getUsername());
            break;
        case 403:
            SE_LOG_INFO(&source, NULL, "log in succeeded, but server refuses access - contact server operator");
            break;
        case 407:
            SE_LOG_INFO(NULL, NULL, "proxy authorization failed, check proxy username and password");
            break;
        case 404:
            SE_LOG_INFO(&source, NULL, "server database not found, check URI '%s'", source.getURI());
            break;
        }
        source.recordStatus(SyncMLStatus(extra1));
        break;
    case sysync::PEV_DSSTATS_L:
        /* datastore statistics for local       (extra1=# added, 
           extra2=# updated,
           extra3=# deleted) */
        source.setItemStat(SyncSource::ITEM_LOCAL,
                           SyncSource::ITEM_ADDED,
                           SyncSource::ITEM_TOTAL,
                           extra1);
        source.setItemStat(SyncSource::ITEM_LOCAL,
                           SyncSource::ITEM_UPDATED,
                           SyncSource::ITEM_TOTAL,
                           extra2);
        source.setItemStat(SyncSource::ITEM_LOCAL,
                           SyncSource::ITEM_REMOVED,
                           SyncSource::ITEM_TOTAL,
                           // Synthesis engine doesn't count locally
                           // deleted items during
                           // refresh-from-server. That's a matter of
                           // taste. In SyncEvolution we'd like these
                           // items to show up, so add it here.
                           source.getFinalSyncMode() == SYNC_REFRESH_FROM_SERVER ? 
                           source.getNumDeleted() :
                           extra3);
        break;
    case sysync::PEV_DSSTATS_R:
        /* datastore statistics for remote      (extra1=# added, 
           extra2=# updated,
           extra3=# deleted) */
        source.setItemStat(SyncSource::ITEM_REMOTE,
                           SyncSource::ITEM_ADDED,
                           SyncSource::ITEM_TOTAL,
                           extra1);
        source.setItemStat(SyncSource::ITEM_REMOTE,
                           SyncSource::ITEM_UPDATED,
                           SyncSource::ITEM_TOTAL,
                           extra2);
        source.setItemStat(SyncSource::ITEM_REMOTE,
                           SyncSource::ITEM_REMOVED,
                           SyncSource::ITEM_TOTAL,
                           extra3);
        break;
    case sysync::PEV_DSSTATS_E:
        /* datastore statistics for local/remote rejects (extra1=# locally rejected, 
           extra2=# remotely rejected) */
        source.setItemStat(SyncSource::ITEM_LOCAL,
                           SyncSource::ITEM_ANY,
                           SyncSource::ITEM_REJECT,
                           extra1);
        source.setItemStat(SyncSource::ITEM_REMOTE,
                           SyncSource::ITEM_ANY,
                           SyncSource::ITEM_REJECT,
                           extra2);
        break;
    case sysync::PEV_DSSTATS_S:
        /* datastore statistics for server slowsync  (extra1=# slowsync matches) */
        source.setItemStat(SyncSource::ITEM_REMOTE,
                           SyncSource::ITEM_ANY,
                           SyncSource::ITEM_MATCH,
                           extra1);
        break;
    case sysync::PEV_DSSTATS_C:
        /* datastore statistics for server conflicts (extra1=# server won,
           extra2=# client won,
           extra3=# duplicated) */
        source.setItemStat(SyncSource::ITEM_REMOTE,
                           SyncSource::ITEM_ANY,
                           SyncSource::ITEM_CONFLICT_SERVER_WON,
                           extra1);
        source.setItemStat(SyncSource::ITEM_REMOTE,
                           SyncSource::ITEM_ANY,
                           SyncSource::ITEM_CONFLICT_CLIENT_WON,
                           extra2);
        source.setItemStat(SyncSource::ITEM_REMOTE,
                           SyncSource::ITEM_ANY,
                           SyncSource::ITEM_CONFLICT_DUPLICATED,
                           extra3);
        break;
    case sysync::PEV_DSSTATS_D:
        /* datastore statistics for data   volume    (extra1=outgoing bytes,
           extra2=incoming bytes) */
        source.setItemStat(SyncSource::ITEM_LOCAL,
                           SyncSource::ITEM_ANY,
                           SyncSource::ITEM_SENT_BYTES,
                           extra1);
        source.setItemStat(SyncSource::ITEM_LOCAL,
                           SyncSource::ITEM_ANY,
                           SyncSource::ITEM_RECEIVED_BYTES,
                           extra2);
        break;
    default:
        SE_LOG_DEBUG(NULL, NULL, "%s: progress event %d, extra %d/%d/%d",
                  source.getName(),
                  type, extra1, extra2, extra3);
    }
}

void SyncContext::throwError(const string &error)
{
#ifdef IPHONE
    /*
     * Catching the runtime_exception fails due to a toolchain problem,
     * so do the error handling now and abort: because there is just
     * one sync source this is probably the only thing that can be done.
     * Still, it's not nice on the server...
     */
    fatalError(NULL, error.c_str());
#else
    throw runtime_error(error);
#endif
}

void SyncContext::throwError(const string &action, int error)
{
    throwError(action + ": " + strerror(error));
}

void SyncContext::fatalError(void *object, const char *error)
{
    SE_LOG_ERROR(NULL, NULL, "%s", error);
    if (m_sourceListPtr) {
        m_sourceListPtr->syncDone(STATUS_FATAL, NULL);
    }
    exit(1);
}

/*
 * There have been segfaults inside glib in the background
 * thread which ran the second event loop. Disabled it again,
 * even though the synchronous EDS API calls will block then
 * when EDS dies.
 */
#if 0 && defined(HAVE_GLIB) && defined(HAVE_EDS)
# define RUN_GLIB_LOOP
#endif

#ifdef RUN_GLIB_LOOP
static void *mainLoopThread(void *)
{
    // The test framework uses SIGALRM for timeouts.
    // Block the signal here because a) the signal handler
    // prints a stack back trace when called and we are not
    // interessted in the background thread's stack and b)
    // it seems to have confused glib/libebook enough to
    // access invalid memory and segfault when it gets the SIGALRM.
    sigset_t blocked;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGALRM);
    pthread_sigmask(SIG_BLOCK, &blocked, NULL);

    GMainLoop *mainloop = g_main_loop_new(NULL, TRUE);
    if (mainloop) {
        g_main_loop_run(mainloop);
        g_main_loop_unref(mainloop);
    }
    return NULL;
}
#endif

void SyncContext::startLoopThread()
{
#ifdef RUN_GLIB_LOOP
    // when using Evolution we must have a running main loop,
    // otherwise loss of connection won't be reported to us
    static pthread_t loopthread;
    static bool loopthreadrunning;
    if (!loopthreadrunning) {
        loopthreadrunning = !pthread_create(&loopthread, NULL, mainLoopThread, NULL);
    }
#endif
}

SyncSource *SyncContext::findSource(const char *name)
{
    return m_sourceListPtr ? (*m_sourceListPtr)[name] : NULL;
}

void SyncContext::setConfigFilter(bool sync, const FilterConfigNode::ConfigFilter &filter)
{
    map<string, string>::const_iterator hasSync = filter.find(SyncSourceConfig::m_sourcePropSync.getName());

    if (!sync && hasSync != filter.end()) {
        m_overrideMode = hasSync->second;
        FilterConfigNode::ConfigFilter strippedFilter = filter;
        strippedFilter.erase(SyncSourceConfig::m_sourcePropSync.getName());
        SyncConfig::setConfigFilter(sync, strippedFilter);
    } else {
        SyncConfig::setConfigFilter(sync, filter);
    }
}

void SyncContext::initSources(SourceList &sourceList)
{
    set<string> unmatchedSources = m_sources;
    list<string> configuredSources = getSyncSources();
    BOOST_FOREACH(const string &name, configuredSources) {
        boost::shared_ptr<PersistentSyncSourceConfig> sc(getSyncSourceConfig(name));
        
        // is the source enabled?
        string sync = sc->getSync();
        bool enabled = sync != "disabled";
        string overrideMode = m_overrideMode;

        // override state?
        if (m_sources.size()) {
            if (m_sources.find(sc->getName()) != m_sources.end()) {
                if (!enabled) {
                    if (overrideMode.empty()) {
                        overrideMode = "two-way";
                    }
                    enabled = true;
                }
                unmatchedSources.erase(sc->getName());
            } else {
                enabled = false;
            }
        }
        
        if (enabled) {
            string url = getSyncURL();
            boost::replace_first(url, "https://", "http://"); // do not distinguish between protocol in change tracking
            string changeId = string("sync4jevolution:") + url + "/" + name;
            SyncSourceParams params(name,
                                    getSyncSourceNodes(name),
                                    changeId);
            // the sync mode has to be set before instantiating the source
            // because the client library reads the preferredSyncMode at that time
            if (!overrideMode.empty()) {
                params.m_nodes.m_configNode->addFilter(SyncSourceConfig::m_sourcePropSync.getName(),
                                                       overrideMode);
            }
            SyncSource *syncSource =
                SyncSource::createSource(params);
            if (!syncSource) {
                throwError(name + ": type unknown" );
            }
            sourceList.push_back(syncSource);
        }
    }

    // check whether there were any sources specified which do not exist
    if (unmatchedSources.size()) {
        throwError(string("no such source(s): ") + boost::join(unmatchedSources, " "));
    }
}

bool SyncContext::transport_cb (void *udata)
{
    return static_cast <SyncContext *> (udata) -> processTransportCb();
}

bool SyncContext::processTransportCb()
{
    //Always return true to continue, we will detect the retry count at
    //the higher level together with transport error scenarios.
    SE_LOG_INFO(NULL, NULL, "Transport timeout after %d:%02dmin",
                m_retryInterval / 60,
                m_retryInterval % 60);
    return true;
}

// XML configuration converted to C string constant
extern "C" {
    extern const char *SyncEvolutionXML;
}

void SyncContext::setSyncModes(const std::vector<SyncSource *> &sources,
                                       const SyncModes &modes)
{
    BOOST_FOREACH(SyncSource *source, sources) {
        SyncMode mode = modes.getSyncMode(source->getName());
        if (mode != SYNC_NONE) {
            string modeString(PrettyPrintSyncMode(mode));
            source->setSync(modeString, true);
        }
    }
}

void SyncContext::getConfigTemplateXML(string &xml, string &configname)
{
    try {
        configname = "syncclient_sample_config.xml";
        if (ReadFile(configname, xml)) {
            return;
        }
    } catch (...) {
        Exception::handle();
    }

    /**
     * @TODO read from config directory
     */

    configname = "builtin XML configuration";
    xml = SyncEvolutionXML;
}

static void substTag(string &xml, const string &tagname, const string &replacement, bool replaceElement = false)
{
    string tag;
    size_t index;

    tag.reserve(tagname.size() + 3);
    tag += "<";
    tag += tagname;
    tag += "/>";

    index = xml.find(tag);
    if (index != xml.npos) {
        string tmp;
        tmp.reserve(tagname.size() * 2 + 2 + 3 + replacement.size());
        if (!replaceElement) {
            tmp += "<";
            tmp += tagname;
            tmp += ">";
        }
        tmp += replacement;
        if (!replaceElement) {
            tmp += "</";
            tmp += tagname;
            tmp += ">";
        }
        xml.replace(index, tag.size(), tmp);
    }
}

static void substTag(string &xml, const string &tagname, const char *replacement, bool replaceElement = false)
{
    substTag(xml, tagname, std::string(replacement), replaceElement);
}

template <class T> void substTag(string &xml, const string &tagname, const T replacement, bool replaceElement = false)
{
    stringstream str;
    str << replacement;
    substTag(xml, tagname, str.str(), replaceElement);
}

void SyncContext::getConfigXML(string &xml, string &configname)
{
    getConfigTemplateXML(xml, configname);

    string tag;
    size_t index;
    unsigned long hash = 0;

    tag = "<debug/>";
    index = xml.find(tag);
    if (index != xml.npos) {
        stringstream debug;
        bool logging = !m_sourceListPtr->getLogdir().empty();
        int loglevel = getLogLevel();

        debug <<
            "  <debug>\n"
            // logpath is a config variable set by SyncContext::doSync()
            "    <logpath>$(logpath)</logpath>\n"
            "    <logflushmode>flush</logflushmode>\n"
            "    <logformat>html</logformat>\n"
            "    <folding>auto</folding>\n"
            "    <timestamp>yes</timestamp>\n"
            "    <timestampall>yes</timestampall>\n"
            "    <timedsessionlognames>no</timedsessionlognames>\n"
            "    <subthreadmode>suppress</subthreadmode>\n"
            "    <logsessionstoglobal>yes</logsessionstoglobal>\n"
            "    <singlegloballog>yes</singlegloballog>\n";
        if (logging) {
            debug <<
                "    <sessionlogs>yes</sessionlogs>\n"
                "    <globallogs>yes</globallogs>\n";
            debug << "<msgdump>" << (loglevel >= 5 ? "yes" : "no") << "</msgdump>\n";
            debug << "<xmltranslate>" << (loglevel >= 4 ? "yes" : "no") << "</xmltranslate>\n";
            if (loglevel >= 3) {
                debug <<
                    "    <enable option=\"all\"/>\n"
                    "    <enable option=\"userdata\"/>\n"
                    "    <enable option=\"scripts\"/>\n"
                    "    <enable option=\"exotic\"/>\n";
            }
        } else {
            debug <<
                "    <sessionlogs>no</sessionlogs>\n"
                "    <globallogs>no</globallogs>\n"
                "    <msgdump>no</msgdump>\n"
                "    <xmltranslate>no</xmltranslate>\n"
                "    <disable option=\"all\"/>";
        }
        debug <<
            "  </debug>\n";

        xml.replace(index, tag.size(), debug.str());
    }

    XMLConfigFragments fragments;
    tag = "<datastore/>";
    index = xml.find(tag);
    if (index != xml.npos) {
        stringstream datastores;

        BOOST_FOREACH(SyncSource *source, *m_sourceListPtr) {
            string fragment;
            source->getDatastoreXML(fragment, fragments);
            hash = Hash(source->getName()) % INT_MAX;

            /**
             * @TODO handle hash collisions
             */
            if (!hash) {
                hash = 1;
            }
            datastores << "    <datastore name='" << source->getName() << "' type='plugin'>\n" <<
                "      <dbtypeid>" << hash << "</dbtypeid>\n" <<
                fragment <<
                "    </datastore>\n\n";
        }
        if (datastores.str().empty()) {
            // Add dummy datastore, the engine needs it. sync()
            // checks that we have a valid configuration if it is
            // really needed.
#if 0
            datastores << "<datastore name=\"____dummy____\" type=\"plugin\">"
                "<plugin_module>SyncEvolution</plugin_module>"
                "<fieldmap fieldlist=\"contacts\"/>"
                "<typesupport>"
                "<use datatype=\"vCard30\"/>"
                "</typesupport>"
                "</datastore>";
#endif
        }
        xml.replace(index, tag.size(), datastores.str());
    }

    substTag(xml, "fieldlists", fragments.m_fieldlists.join(), true);
    substTag(xml, "profiles", fragments.m_profiles.join(), true);
    substTag(xml, "datatypes", fragments.m_datatypes.join(), true);
    substTag(xml, "remoterules", fragments.m_remoterules.join(), true);

    substTag(xml, "fakedeviceid", getDevID());
    substTag(xml, "model", getMod());
    substTag(xml, "manufacturer", getMan());
    substTag(xml, "hardwareversion", getHwv());
    // abuse (?) the firmware version to store the SyncEvolution version number
    substTag(xml, "firmwareversion", getSwv());
    substTag(xml, "devicetype", getDevType());
    substTag(xml, "maxmsgsize", std::max(getMaxMsgSize(), 10000ul));
    substTag(xml, "maxobjsize", std::max(getMaxObjSize(), 1024u));
    substTag(xml, "defaultauth", getClientAuthType());

    // if the hash code is changed, that means the content of the
    // config has changed, save the new hash and regen the configdate
    hash = Hash(xml.c_str());
    if (getHashCode() != hash) {
        setConfigDate();
        setHashCode(hash);
        flush();
    }
    substTag(xml, "configdate", getConfigDate().c_str());
}

SharedEngine SyncContext::createEngine()
{
    SharedEngine engine(new sysync::TEngineModuleBridge);

    // Use libsynthesis that we were linked against.  The name of a
    // .so could be given here, too, to use that instead.  This
    // instance of the engine is used outside of the sync session
    // itself. doSync() then creates another engine for the sync
    // itself. That is necessary because the engine shutdown depends
    // on the context of the sync (in particular instantiated sync
    // sources).
    engine.Connect("[]", 0,
                   sysync::DBG_PLUGIN_NONE|
                   sysync::DBG_PLUGIN_INT|
                   sysync::DBG_PLUGIN_DB|
                   sysync::DBG_PLUGIN_EXOT);

    SharedKey configvars = engine.OpenKeyByPath(SharedKey(), "/configvars");
    string logdir = m_sourceListPtr->getLogdir();
    engine.SetStrValue(configvars, "defout_path",
                       logdir.size() ? logdir : "/dev/null");
    engine.SetStrValue(configvars, "conferrpath", "console");
    engine.SetStrValue(configvars, "binfilepath", getRootPath() + "/.synthesis");
    configvars.reset();

    return engine;
}

namespace {
    void GnutlsLogFunction(int level, const char *str)
    {
        SE_LOG_DEBUG(NULL, "GNUTLS", "level %d: %s", level, str);
    }
}

SyncMLStatus SyncContext::sync(SyncReport *report)
{
    SyncMLStatus status = STATUS_OK;

    if (!exists()) {
        SE_LOG_ERROR(NULL, NULL, "No configuration for server \"%s\" found.", m_server.c_str());
        throwError("cannot proceed without configuration");
    }

    // redirect logging as soon as possible
    SourceList sourceList(*this, m_doLogging);
    sourceList.setLogLevel(m_quiet ? SourceList::LOGGING_QUIET :
                           getPrintChanges() ? SourceList::LOGGING_FULL :
                           SourceList::LOGGING_SUMMARY);
    m_sourceListPtr = &sourceList;

    if (getenv("SYNCEVOLUTION_GNUTLS_DEBUG")) {
        // Enable libgnutls debugging without creating a hard dependency on it,
        // because we don't call it directly and might not even be linked against
        // it. Therefore check for the relevant symbols via dlsym().
        void (*set_log_level)(int);
        void (*set_log_function)(void (*func)(int level, const char *str));

        set_log_level = (typeof(set_log_level))dlsym(RTLD_DEFAULT, "gnutls_global_set_log_level");
        set_log_function = (typeof(set_log_function))dlsym(RTLD_DEFAULT, "gnutls_global_set_log_function");

        if (set_log_level && set_log_function) {
            set_log_level(atoi(getenv("SYNCEVOLUTION_GNUTLS_DEBUG")));
            set_log_function(GnutlsLogFunction);
        } else {
            SE_LOG_ERROR(NULL, NULL, "SYNCEVOLUTION_GNUTLS_DEBUG debugging not possible, log functions not found");
        }
    }

    try {
        SyncReport buffer;
        if (!report) {
            report = &buffer;
        }
        report->clear();

        // let derived classes override settings, like the log dir
        prepare();

        // choose log directory
        sourceList.startSession(getLogDir(),
                                getMaxLogDirs(),
                                getLogLevel(),
                                report,
                                "client");

        // create a Synthesis engine, used purely for logging purposes
        // at this time
        SwapEngine swapengine(*this);
        string xml, configname;
        getConfigXML(xml, configname);
        m_engine.InitEngineXML(xml.c_str());

        try {
            // dump some summary information at the beginning of the log
            SE_LOG_DEV(NULL, NULL, "SyncML server account: %s", getUsername());
            SE_LOG_DEV(NULL, NULL, "client: SyncEvolution %s for %s", getSwv(), getDevType());
            SE_LOG_DEV(NULL, NULL, "device ID: %s", getDevID());
            SE_LOG_DEV(NULL, NULL, "%s", EDSAbiWrapperDebug());
            SE_LOG_DEV(NULL, NULL, "%s", SyncSource::backendsDebug().c_str());

            // instantiate backends, but do not open them yet
            initSources(sourceList);
            if (sourceList.empty()) {
                throwError("no sources active, check configuration");
            }

            // request all config properties once: throwing exceptions
            // now is okay, whereas later it would lead to leaks in the
            // not exception safe client library
            SyncConfig dummy;
            set<string> activeSources = sourceList.getSources();
            dummy.copy(*this, &activeSources);

            // start background thread if not running yet:
            // necessary to catch problems with Evolution backend
            startLoopThread();

            // ask for passwords now
            /* iterator over all sync and source properties instead of checking
             * some specified passwords.
             */
            ConfigPropertyRegistry& registry = SyncConfig::getRegistry();
            BOOST_FOREACH(const ConfigProperty *prop, registry) {
                prop->checkPassword(*this, m_server, getConfigNode());
            }
            BOOST_FOREACH(SyncSource *source, sourceList) {
                ConfigPropertyRegistry& registry = SyncSourceConfig::getRegistry();
                BOOST_FOREACH(const ConfigProperty *prop, registry) {
                    prop->checkPassword(*this, m_server, getConfigNode(),
                                        source->getName(), source->getSyncSourceNodes().m_configNode);
                }
            }

            // open each source - failing now is still safe
            BOOST_FOREACH(SyncSource *source, sourceList) {
                source->open();
            }

            // give derived class also a chance to update the configs
            prepare(sourceList);

            // ready to go: dump initial databases and prepare for final report
            sourceList.syncPrepare();

            // run sync session
            status = doSync();
        } catch (...) {
            // handle the exception here while the engine (and logging!) is still alive
            Exception::handle(&status);
            goto report;
        }
    } catch (...) {
        Exception::handle(&status);
    }

 report:
    try {
        // Print final report before cleaning up.
        // Status was okay only if all sources succeeded.
        sourceList.updateSyncReport(*report);
        BOOST_FOREACH(SyncSource *source, sourceList) {
            if (source->getStatus() != STATUS_OK &&
                status == STATUS_OK) {
                status = source->getStatus();
                break;
            }
        }
        sourceList.syncDone(status, report);
    } catch(...) {
        Exception::handle(&status);
    }

    m_sourceListPtr = NULL;
    return status;
}

SyncMLStatus SyncContext::doSync()
{
    struct sigaction new_action, old_action;
    new_action.sa_handler= suspend_handler;
    sigemptyset (&new_action.sa_mask);
    new_action.sa_flags = 0;
    sigaction (SIGINT, &new_action, &old_action);
    SyncMLStatus status = STATUS_OK;
    std::string s;

    // re-init engine with all sources configured
    string xml, configname;
    getConfigXML(xml, configname);
    m_engine.InitEngineXML(xml.c_str());
    SE_LOG_DEV(NULL, NULL, "Full XML configuration:\n%s", xml.c_str());

    // check the settings status (MUST BE DONE TO MAKE SETTINGS READY)
    SharedKey profiles = m_engine.OpenKeyByPath(SharedKey(), "/profiles");
    m_engine.GetStrValue(profiles, "settingsstatus");
    // allow creating new settings when existing settings are not up/downgradeable
    m_engine.SetStrValue(profiles, "overwrite",  "1");
    // check status again
    m_engine.GetStrValue(profiles, "settingsstatus");
    
    // open first profile
    SharedKey profile;
    try {
        profile = m_engine.OpenSubkey(profiles, sysync::KEYVAL_ID_FIRST);
    } catch (NoSuchKey error) {
        // no profile exists  yet, create default profile
        profile = m_engine.OpenSubkey(profiles, sysync::KEYVAL_ID_NEW_DEFAULT);
    }
         
    m_engine.SetStrValue(profile, "serverURI", getSyncURL());
    m_engine.SetStrValue(profile, "serverUser", getUsername());
    m_engine.SetStrValue(profile, "serverPassword", getPassword());
    m_engine.SetInt32Value(profile, "encoding",
                           getWBXML() ? 1 /* WBXML */ : 2 /* XML */);

    // Iterate over all data stores in the XML config
    // and match them with sync sources.
    // TODO: let sync sources provide their own
    // XML snippets (inside <client> and inside <datatypes>).
    SharedKey targets = m_engine.OpenKeyByPath(profile, "targets");
    SharedKey target;

    try {
        target = m_engine.OpenSubkey(targets, sysync::KEYVAL_ID_FIRST);
        while (true) {
            s = m_engine.GetStrValue(target, "dbname");
            SyncSource *source = (*m_sourceListPtr)[s];
            if (source) {
                m_engine.SetInt32Value(target, "enabled", 1);
                int slow = 0;
                int direction = 0;
                string mode = source->getSync();
                if (!strcasecmp(mode.c_str(), "slow")) {
                    slow = 1;
                    direction = 0;
                } else if (!strcasecmp(mode.c_str(), "two-way")) {
                    slow = 0;
                    direction = 0;
                } else if (!strcasecmp(mode.c_str(), "refresh-from-server")) {
                    slow = 1;
                    direction = 1;
                } else if (!strcasecmp(mode.c_str(), "refresh-from-client")) {
                    slow = 1;
                    direction = 2;
                } else if (!strcasecmp(mode.c_str(), "one-way-from-server")) {
                    slow = 0;
                    direction = 1;
                } else if (!strcasecmp(mode.c_str(), "one-way-from-client")) {
                    slow = 0;
                    direction = 2;
                } else {
                    source->throwError(string("invalid sync mode: ") + mode);
                }
                m_engine.SetInt32Value(target, "forceslow", slow);
                m_engine.SetInt32Value(target, "syncmode", direction);

                m_engine.SetStrValue(target, "remotepath", source->getURI());
            } else {
                m_engine.SetInt32Value(target, "enabled", 0);
            }
            target = m_engine.OpenSubkey(targets, sysync::KEYVAL_ID_NEXT);
        }
    } catch (NoSuchKey error) {
    }

    m_retryInterval = getRetryInterval();
    m_retryDuration = getRetryDuration();
    m_retries = 0;

    // run an HTTP client sync session
    boost::shared_ptr<TransportAgent> agent(createTransportAgent());
    if (getUseProxy()) {
        agent->setProxy(getProxyHost());
        agent->setProxyAuth(getProxyUsername(),
                            getProxyPassword());
    }
    agent->setUserAgent(getUserAgent());
    agent->setSSL(findSSLServerCertificate(),
                  getSSLVerifyServer(),
                  getSSLVerifyHost());

    // Close all keys so that engine can flush the modified config.
    // Otherwise the session reads the unmodified values from the
    // created files while the updated values are still in memory.
    target.reset();
    targets.reset();
    profile.reset();
    profiles.reset();

    // reopen profile keys
    profiles = m_engine.OpenKeyByPath(SharedKey(), "/profiles");
    m_engine.GetStrValue(profiles, "settingsstatus");
    profile = m_engine.OpenSubkey(profiles, sysync::KEYVAL_ID_FIRST);
    targets = m_engine.OpenKeyByPath(profile, "targets");

    sysync::TEngineProgressInfo progressInfo;
    sysync::uInt16 stepCmd = sysync::STEPCMD_CLIENTSTART; // first step
    SharedSession session = m_engine.OpenSession();
    SharedBuffer sendBuffer;
    SessionSentinel sessionSentinel(*this, session);

    // Sync main loop: runs until SessionStep() signals end or error.
    // Exceptions are caught and lead to a call of SessionStep() with
    // parameter STEPCMD_ABORT -> abort session as soon as possible.
    bool aborting = false;
    int suspending = 0; 
    time_t sendStart = 0, resendStart = 0;
    sysync::uInt16 previousStepCmd = stepCmd;
    do {
        try {
            // check for suspend, if so, modify step command for next step
            // Since the suspend will actually be committed until it is
            // sending out a message, we can safely delay the suspend to
            // GOTDATA state.
            // After exception occurs, stepCmd will be set to abort to force
            // aborting, must avoid to change it back to suspend cmd.
            if (checkForSuspend() && stepCmd == sysync::STEPCMD_GOTDATA) {
                stepCmd = sysync::STEPCMD_SUSPEND;
            }

            //check for abort, if so, modify step command for next step.
            //We think abort is useful when the server is unresponsive or 
            //too slow to the user; therefore, we can delay abort at other
            //points to this two points (before sending and before receiving
            //the data).
            if (checkForAbort() && (stepCmd == sysync::STEPCMD_RESENDDATA
                        || stepCmd ==sysync::STEPCMD_SENDDATA 
                        || stepCmd == sysync::STEPCMD_NEEDDATA)) {
                stepCmd = sysync::STEPCMD_ABORT;
            }

            // take next step, but don't abort twice: instead
            // let engine contine with its shutdown
            if (stepCmd == sysync::STEPCMD_ABORT) {
                if (aborting) {
                    stepCmd = previousStepCmd;
                } else {
                    aborting = true;
                }
            }
            // same for suspending
            if (stepCmd == sysync::STEPCMD_SUSPEND) {
                if (suspending) {
                    stepCmd = previousStepCmd;
                    suspending++;
                } else {
                    suspending++; 
                }
            }

            m_engine.SessionStep(session, stepCmd, &progressInfo);
            //During suspention we actually insert a STEPCMD_SUSPEND cmd
            //Should restore to the original step here
            if(suspending == 1)
            {
                stepCmd = previousStepCmd;
                continue;
            }
            switch (stepCmd) {
            case sysync::STEPCMD_OK:
                // no progress info, call step again
                stepCmd = sysync::STEPCMD_STEP;
                break;
            case sysync::STEPCMD_PROGRESS:
                // new progress info to show
                // Check special case of interactive display alert
                if (progressInfo.eventtype == sysync::PEV_DISPLAY100) {
                    // alert 100 received from remote, message text is in
                    // SessionKey's "displayalert" field
                    SharedKey sessionKey = m_engine.OpenSessionKey(session);
                    // get message from server to display
                    s = m_engine.GetStrValue(sessionKey,
                                             "displayalert");
                    displayServerMessage(s);
                } else {
                    switch (progressInfo.targetID) {
                    case sysync::KEYVAL_ID_UNKNOWN:
                    case 0 /* used with PEV_SESSIONSTART?! */:
                        displaySyncProgress(sysync::TProgressEventEnum(progressInfo.eventtype),
                                            progressInfo.extra1,
                                            progressInfo.extra2,
                                            progressInfo.extra3);
                        break;
                    default:
                        // specific for a certain sync source:
                        // find it...
                        target = m_engine.OpenSubkey(targets, progressInfo.targetID);
                        s = m_engine.GetStrValue(target, "dbname");
                        SyncSource *source = (*m_sourceListPtr)[s];
                        if (source) {
                            displaySourceProgress(sysync::TProgressEventEnum(progressInfo.eventtype),
                                                  *source,
                                                  progressInfo.extra1,
                                                  progressInfo.extra2,
                                                  progressInfo.extra3);
                        } else {
                            throwError(std::string("unknown target ") + s);
                        }
                        target.reset();
                        break;
                    }
                }
                stepCmd = sysync::STEPCMD_STEP;
                break;
            case sysync::STEPCMD_ERROR:
                // error, terminate (should not happen, as status is
                // already checked above)
                break;
            case sysync::STEPCMD_RESTART:
                // make sure connection is closed and will be re-opened for next request
                // tbd: close communication channel if still open to make sure it is
                //       re-opened for the next request
                stepCmd = sysync::STEPCMD_STEP;
                m_retries = 0;
                break;
            case sysync::STEPCMD_SENDDATA: {
                // send data to remote

                // use OpenSessionKey() and GetValue() to retrieve "connectURI"
                // and "contenttype" to be used to send data to the server
                SharedKey sessionKey = m_engine.OpenSessionKey(session);
                s = m_engine.GetStrValue(sessionKey,
                                         "connectURI");
                agent->setURL(s);
                s = m_engine.GetStrValue(sessionKey,
                                         "contenttype");
                agent->setContentType(s);
                sessionKey.reset();
                
                sendStart = resendStart = time (NULL);
                //register transport callback
                if (m_retryInterval) {
                    agent->setCallback (transport_cb, this, m_retryInterval);
                }
                // use GetSyncMLBuffer()/RetSyncMLBuffer() to access the data to be
                // sent or have it copied into caller's buffer using
                // ReadSyncMLBuffer(), then send it to the server
                sendBuffer = m_engine.GetSyncMLBuffer(session, true);
                agent->send(sendBuffer.get(), sendBuffer.size());
                stepCmd = sysync::STEPCMD_SENTDATA; // we have sent the data
                break;
            }
            case sysync::STEPCMD_RESENDDATA: {
                SE_LOG_INFO (NULL, NULL, "SyncContext: resend previous request #%d", m_retries);
                resendStart = time(NULL);
                /* We are resending previous message, just read from the
                 * previous buffer */
                agent->send(sendBuffer.get(), sendBuffer.size());
                stepCmd = sysync::STEPCMD_SENTDATA; // we have sent the data
                break;
            }
            case sysync::STEPCMD_NEEDDATA:
                switch (agent->wait()) {
                case TransportAgent::ACTIVE:
                    stepCmd = sysync::STEPCMD_SENTDATA; // still sending the data?!
                    break;
               
                case TransportAgent::TIME_OUT: {
                    time_t duration = time(NULL) - sendStart;
                    if(duration > m_retryDuration){
                        SE_LOG_INFO(NULL, NULL,
                                    "Transport giving up after %d retries and %ld:%02ldmin",
                                    m_retries,
                                    (long)(duration / 60),
                                    (long)(duration % 60));
                        SE_THROW_EXCEPTION(TransportException, "timeout, retry period exceeded");
                    }else {
                        m_retries ++;
                        stepCmd = sysync::STEPCMD_RESENDDATA;
                    }
                    break;
                    }
                case TransportAgent::GOT_REPLY: {
                    const char *reply;
                    size_t replylen;
                    string contentType;
                    agent->getReply(reply, replylen, contentType);

                    // sanity check for reply: if known at all, it must be either XML or WBXML
                    if (contentType.empty() ||
                        contentType.find("application/vnd.syncml+wbxml") != contentType.npos ||
                        contentType.find("application/vnd.syncml+xml") != contentType.npos) {
                        // put answer received earlier into SyncML engine's buffer
                        m_retries = 0;
                        sendBuffer.reset();
                        m_engine.WriteSyncMLBuffer(session,
                                                   reply,
                                                   replylen);
                        stepCmd = sysync::STEPCMD_GOTDATA; // we have received response data
                        break;
                    } else {
                        SE_LOG_DEBUG(NULL, NULL, "unexpected content type '%s' in reply, %d bytes:\n%.*s",
                                     contentType.c_str(), (int)replylen, (int)replylen, reply);
                        SE_LOG_ERROR(NULL, NULL, "unexpected reply from server; might be a temporary problem, try again later");
                      } //fall through to network failure case
                }
                /* If this is a network error, it usually failed quickly, retry
                 * immediately has likely no effect. Manually sleep here to wait a while
                 * before retry. Sleep time will be calculated so that the
                 * message sending interval equals m_retryInterval.
                 */
                case TransportAgent::FAILED: {
                    time_t curTime = time(NULL);
                    time_t duration = curTime - sendStart;
                    if (!m_retryInterval || duration > m_retryDuration) {
                        SE_LOG_INFO(NULL, NULL,
                                    "Transport giving up after %d retries and %ld:%02ldmin",
                                    m_retries,
                                    (long)(duration / 60),
                                    (long)(duration % 60));
                        SE_THROW_EXCEPTION(TransportException, "transport failed, retry period exceeded");
                    } else {
                        // Send might have failed because of abort or
                        // suspend request.
                        if (checkForSuspend()) {
                            stepCmd = sysync::STEPCMD_SUSPEND;
                            break;
                        } else if (checkForAbort()) {
                            stepCmd = sysync::STEPCMD_ABORT;
                            break;
                        }

                        // retry send
                        int leftTime = m_retryInterval - (curTime - resendStart);
                        if (leftTime >0 ) {
                            if (sleep (leftTime) > 0) {
                                if (checkForSuspend()) {
                                    stepCmd = sysync::STEPCMD_SUSPEND;
                                } else {
                                    stepCmd = sysync::STEPCMD_ABORT;
                                }
                                break;
                            } 
                        }

                        m_retries ++;
                        stepCmd = sysync::STEPCMD_RESENDDATA;
                    }
                    break;
                }
                default:
                    stepCmd = sysync::STEPCMD_TRANSPFAIL; // communication with server failed
                    break;
                }
            }
            previousStepCmd = stepCmd;
            // loop until session done or aborted with error
        } catch (const BadSynthesisResult &result) {
            if (result.result() == sysync::LOCERR_USERABORT && aborting) {
                SE_LOG_INFO(NULL, NULL, "Aborted as requested.");
                stepCmd = sysync::STEPCMD_DONE;
            } else if (result.result() == sysync::LOCERR_USERSUSPEND && suspending) {
                SE_LOG_INFO(NULL, NULL, "Suspended as requested.");
                stepCmd = sysync::STEPCMD_DONE;
            } else if (aborting) {
                // aborting very early can lead to results different from LOCERR_USERABORT
                // => don't treat this as error
                SE_LOG_INFO(NULL, NULL, "Aborted with unexpected result (%d)",
                            static_cast<int>(result.result()));
                stepCmd = sysync::STEPCMD_DONE;
            } else {
                Exception::handle(&status);
                stepCmd = sysync::STEPCMD_ABORT;
            }
        } catch (...) {
            Exception::handle(&status);
            stepCmd = sysync::STEPCMD_ABORT;
        }
    } while (stepCmd != sysync::STEPCMD_DONE && stepCmd != sysync::STEPCMD_ERROR);

    sigaction (SIGINT, &old_action, NULL);
    return status;
}


void SyncContext::status()
{
    if (!exists()) {
        SE_LOG_ERROR(NULL, NULL, "No configuration for server \"%s\" found.", m_server.c_str());
        throwError("cannot proceed without configuration");
    }

    SourceList sourceList(*this, false);
    initSources(sourceList);
    BOOST_FOREACH(SyncSource *source, sourceList) {
        ConfigPropertyRegistry& registry = SyncSourceConfig::getRegistry();
        BOOST_FOREACH(const ConfigProperty *prop, registry) {
            prop->checkPassword(*this, m_server, getConfigNode(),
                                source->getName(), source->getSyncSourceNodes().m_configNode);
        }
    }
    BOOST_FOREACH(SyncSource *source, sourceList) {
        source->open();
    }

    SyncReport changes;
    checkSourceChanges(sourceList, changes);

    stringstream out;
    changes.prettyPrint(out,
                        SyncReport::WITHOUT_SERVER|
                        SyncReport::WITHOUT_CONFLICTS|
                        SyncReport::WITHOUT_REJECTS|
                        SyncReport::WITH_TOTAL);
    SE_LOG_INFO(NULL, NULL, "Local item changes:\n%s",
                out.str().c_str());

    sourceList.startSession(getLogDir(), 0, 0, NULL, "status");
    LoggerBase::instance().setLevel(Logger::INFO);
    string prevLogdir = sourceList.getPrevLogdir();
    bool found = access(prevLogdir.c_str(), R_OK|X_OK) == 0;

    if (found) {
        try {
            sourceList.setPath(prevLogdir);
            sourceList.dumpDatabases("current", NULL);
            sourceList.dumpLocalChanges(sourceList.getPrevLogdir(), "after", "current");
        } catch(...) {
            Exception::handle();
        }
    } else {
        cout << "Previous log directory not found.\n";
        if (!getLogDir() || !getLogDir()[0]) {
            cout << "Enable the 'logdir' option and synchronize to use this feature.\n";
        }
    }
}

void SyncContext::checkStatus(SyncReport &report)
{
    if (!exists()) {
        SE_LOG_ERROR(NULL, NULL, "No configuration for server \"%s\" found.", m_server.c_str());
        throwError("cannot proceed without configuration");
    }

    SourceList sourceList(*this, false);
    initSources(sourceList);
    BOOST_FOREACH(SyncSource *source, sourceList) {
        ConfigPropertyRegistry& registry = SyncSourceConfig::getRegistry();
        BOOST_FOREACH(const ConfigProperty *prop, registry) {
            prop->checkPassword(*this, m_server, getConfigNode(),
                                source->getName(), source->getSyncSourceNodes().m_configNode);
        }
    }
    BOOST_FOREACH(SyncSource *source, sourceList) {
        source->open();
    }

    checkSourceChanges(sourceList, report);
}

static void logRestoreReport(const SyncReport &report, bool dryrun)
{
    if (!report.empty()) {
        stringstream out;
        report.prettyPrint(out, SyncReport::WITHOUT_SERVER|SyncReport::WITHOUT_CONFLICTS|SyncReport::WITH_TOTAL);
        SE_LOG_INFO(NULL, NULL, "Item changes %s applied to client during restore:\n%s",
                    dryrun ? "to be" : "that were",
                    out.str().c_str());
        SE_LOG_INFO(NULL, NULL, "The same incremental changes will be applied to the server during the next sync.");
        SE_LOG_INFO(NULL, NULL, "Use -sync refresh-from-client to replace the complete data on the server.");
    }
}

void SyncContext::checkSourceChanges(SourceList &sourceList, SyncReport &changes)
{
    changes.setStart(time(NULL));
    BOOST_FOREACH(SyncSource *source, sourceList) {
        if (source->getOperations().m_checkStatus) {
            SyncSourceReport local;

            source->getOperations().m_checkStatus(local);
            changes.addSyncSourceReport(source->getName(), local);
        }
    }
    changes.setEnd(time(NULL));
}

int SyncContext::sleep (int intervals) 
{
    while ( (intervals = ::sleep (intervals)) > 0) {
        if (checkForSuspend() || checkForAbort ()) {
            break;
        }
    }
    return intervals;
}

void SyncContext::restore(const string &dirname, RestoreDatabase database)
{
    if (!exists()) {
        SE_LOG_ERROR(NULL, NULL, "No configuration for server \"%s\" found.", m_server.c_str());
        throwError("cannot proceed without configuration");
    }

    SourceList sourceList(*this, false);
    sourceList.startSession(dirname.c_str(), 0, 0, NULL, "restore");
    LoggerBase::instance().setLevel(Logger::INFO);
    initSources(sourceList);
    BOOST_FOREACH(SyncSource *source, sourceList) {
        ConfigPropertyRegistry& registry = SyncSourceConfig::getRegistry();
        BOOST_FOREACH(const ConfigProperty *prop, registry) {
            prop->checkPassword(*this, m_server, getConfigNode(),
                                source->getName(), source->getSyncSourceNodes().m_configNode);
        }
    }

    string datadump = database == DATABASE_BEFORE_SYNC ? "before" : "after";

    BOOST_FOREACH(SyncSource *source, sourceList) {
        source->open();
    }

    if (!m_quiet) {
        sourceList.dumpDatabases("current", NULL);
        sourceList.dumpLocalChanges(dirname, "current", datadump,
                                    "Data changes to be applied to local data during restore:\n",
                                    "CLIENT_TEST_LEFT_NAME='current data' "
                                    "CLIENT_TEST_REMOVED='after restore' " 
                                    "CLIENT_TEST_REMOVED='to be removed' "
                                    "CLIENT_TEST_ADDED='to be added'");
    }

    SyncReport report;
    try {
        BOOST_FOREACH(SyncSource *source, sourceList) {
            SyncSourceReport sourcereport;
            try {
                SE_LOG_DEBUG(NULL, NULL, "Restoring %s...", source->getName());
                sourceList.restoreDatabase(*source,
                                           datadump,
                                           m_dryrun,
                                           sourcereport);
                SE_LOG_DEBUG(NULL, NULL, "... %s restored.", source->getName());
                report.addSyncSourceReport(source->getName(), sourcereport);
            } catch (...) {
                sourcereport.recordStatus(STATUS_FATAL);
                report.addSyncSourceReport(source->getName(), sourcereport);
                throw;
            }
        }
    } catch (...) {
        logRestoreReport(report, m_dryrun);
        throw;
    }
    logRestoreReport(report, m_dryrun);
}

void SyncContext::getSessions(vector<string> &dirs)
{
    LogDir logging(*this);
    logging.previousLogdirs(getLogDir(), dirs);
}

void SyncContext::readSessionInfo(const string &dir, SyncReport &report)
{
    LogDir logging(*this);
    logging.openLogdir(dir);
    logging.readReport(report);
}

std::string SyncContext::findSSLServerCertificate()
{
    std::string paths = getSSLServerCertificates();
    std::vector< std::string > files;
    boost::split(files, paths, boost::is_any_of(":"));
    BOOST_FOREACH(std::string file, files) {
        if (!file.empty() && !access(file.c_str(), R_OK)) {
            return file;
        }
    }

    return "";
}

SE_END_CXX
