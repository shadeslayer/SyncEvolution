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
#include <syncevo/ObexTransportAgent.h>

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
#include <synthesis/san.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

SourceList *SyncContext::m_sourceListPtr;
SyncContext *SyncContext::m_activeContext;
SuspendFlags SyncContext::s_flags;

static const char *LogfileBasename = "syncevolution-log";

void SyncContext::handleSignal(int sig)
{
    switch (sig) {
    case SIGTERM:
        switch (s_flags.state) {
        case SuspendFlags::CLIENT_ABORT:
            s_flags.message = "Already aborting sync as requested earlier ...";
            break;
        default:
            s_flags.state = SuspendFlags::CLIENT_ABORT;
            s_flags.message = "Aborting sync immediately via SIGTERM ...";
            break;
        }
        break;
    case SIGINT: {
        time_t current;
        time (&current);
        switch (s_flags.state) {
        case SuspendFlags::CLIENT_NORMAL:
            // first time suspend or already aborted
            s_flags.state = SuspendFlags::CLIENT_SUSPEND;
            s_flags.message = "Asking server to suspend...\nPress CTRL-C again quickly (within 2s) to stop sync immediately (can cause problems during next sync!)";
            s_flags.last_suspend = current;
            break;
        case SuspendFlags::CLIENT_SUSPEND:
            // turn into abort?
            if (current - s_flags.last_suspend
                < s_flags.ABORT_INTERVAL) {
                s_flags.state = SuspendFlags::CLIENT_ABORT;
                s_flags.message = "Aborting sync as requested via CTRL-C ...";
            } else {
                s_flags.last_suspend = current;
                s_flags.message = "Suspend in progress...\nPress CTRL-C again quickly (within 2s) to stop sync immediately (can cause problems during next sync!)";
            }
            break;
        case SuspendFlags::CLIENT_ABORT:
            s_flags.message = "Already aborting sync as requested earlier ...";
            break;
        case SuspendFlags::CLIENT_ILLEGAL:
            break;
        break;
        }
    }
    }
}

void SyncContext::printSignals()
{
    if (s_flags.message) {
        SE_LOG_INFO(NULL, NULL, "%s", s_flags.message);
        s_flags.message = NULL;
    }
}

SyncContext::SyncContext()
{
    init();
}

SyncContext::SyncContext(const string &server,
                         bool doLogging) :
    SyncConfig(server),
    m_server(server)
{
    init();
    m_doLogging = doLogging;
}

void SyncContext::init()
{
    m_doLogging = false;
    m_quiet = false;
    m_dryrun = false;
    m_serverMode = false;
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

    // internal prefix for backup directory name: "SyncEvolution-"
    static const char* const DIR_PREFIX;

    /** set m_logdir and adapt m_prefix accordingly */
    void setLogdir(const string &logdir) {
        m_logdir = boost::trim_right_copy_if(logdir, boost::is_any_of("/"));

        string lower = m_client.getServer();
        string context, peer;
        SyncConfig::splitConfigString(lower, peer, context);

        boost::to_lower(peer);
        // escape "_" and "-" the peer name
        peer = escapePeerName(peer);

        if (boost::iends_with(m_logdir, "syncevolution")) {
            // use just the server name as prefix
            m_prefix = peer;
        } else {
            // SyncEvolution-<server>-<yyyy>-<mm>-<dd>-<hh>-<mm>
            m_prefix = DIR_PREFIX;
            m_prefix += peer;
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
    /*
     * get the corresponding peer name encoded in the logging dir name.
     * The dir name must match the format(see startSession). Otherwise,
     * empty string is returned.
     */
    string getPeerNameFromLogdir(const string &dir) {
        // extract the dir name from the fullpath
        string iDirPath, iDirName;
        parseLogDir(dir, iDirPath, iDirName);
        // extract the peer name from the dir name
        string dirPrefix, peerName, dateTime;
        if(parseDirName(iDirName, dirPrefix, peerName, dateTime)) {
            return unescapePeerName(peerName);
        }
        return "";
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
                base << "-"
                     << setfill('0')
                     << setw(4) << tm->tm_year + 1900 << "-"
                     << setw(2) << tm->tm_mon + 1 << "-"
                     << setw(2) << tm->tm_mday << "-"
                     << setw(2) << tm->tm_hour << "-"
                     << setw(2) << tm->tm_min;
                // make sure no directory name has the same date time with others 
                // even for different peers
                std::vector<string> dateTimes;
                if(isDir(m_logdir)) {
                    ReadDir dir(m_logdir);
                    BOOST_FOREACH(const string &entry, dir) {
                        string dirPrefix, peerName, dateTime;
                        if(parseDirName(entry, dirPrefix, peerName, dateTime)) {
                            dateTimes.push_back(dateTime);
                        }
                    }
                    sort(dateTimes.begin(), dateTimes.end());
                }
                int seq = 0;
                while (true) {
                    stringstream path;
                    path << base.str();
                    if (seq) {
                        path << "-" << seq;
                    }
                    if (!binary_search(dateTimes.begin(), dateTimes.end(), path.str())) {
                        m_path = m_logdir + "/";
                        m_path += m_prefix;
                        m_path += path.str();
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
            m_logfile = m_path + "/" + LogfileBasename + ".html";
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
            // Create a status.ini which contains an error.
            // Will be overwritten later on, unless we crash.
            m_info->setProperty("status", "500");
            m_info->setProperty("error", "synchronization process died prematurely");
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
        if (m_report &&
            level <= ERROR &&
            m_report->getError().empty()) {
            va_list argscopy;
            va_copy(argscopy, args);
            string error = StringPrintfV(format, argscopy);
            va_end(argscopy);

            m_report->setError(error);
        }

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

    /**
     * Compare two directory by its creation time encoded in the directory name
     * sort them in ascending order
     */
    bool operator()(const string &str1, const string &str2) {
        string iDirPath1, iStr1;
        string iDirPath2, iStr2;
        parseLogDir(str1, iDirPath1, iStr1);
        parseLogDir(str2, iDirPath2, iStr2);
        string dirPrefix1, peerName1, dateTime1;
        parseDirName(iStr1, dirPrefix1, peerName1, dateTime1);
        string dirPrefix2, peerName2, dateTime2;
        parseDirName(iStr2, dirPrefix2, peerName2, dateTime2);
        return dateTime1 < dateTime2;
    }

private:
    /**
     * extract backup directory name from a full backup path
     * for example, a full path "/home/xxx/.cache/syncevolution/default/funambol-2009-12-08-14-05"
     * is parsed as "/home/xxx/.cache/syncevolution/default" and "funambol-2009-12-08-14-05"
     */
    static void parseLogDir(const string &fullpath, string &dirPath, string &dirName) {
        string iFullpath = boost::trim_right_copy_if(fullpath, boost::is_any_of("/"));
        size_t off = iFullpath.find_last_of('/');
        if(off != iFullpath.npos) {
            dirPath = iFullpath.substr(0, off);
            dirName = iFullpath.substr(off+1);
        } else {
            dirPath = "";
            dirName = iFullpath;
        }
    }

    // escape '-' and '_' for peer name 
    static string escapePeerName(const string &prefix) {
        string escaped = prefix;
        boost::replace_all(escaped, "_", "__");
        boost::replace_all(escaped, "-", "_+");
        return escaped;
    }

    // un-escape '_+' and '__' for peer name 
    static string unescapePeerName(const string &escaped) {
        string prefix = escaped;
        boost::replace_all(prefix, "_+", "-");
        boost::replace_all(prefix, "__", "_");
        return prefix;
    }

    /**
     * parse a directory name into dirPrefix(empty or DIR_PREFIX), peerName, dateTime.
     * If directory name is in the format of '[DIR_PREFIX]-peerName-year-month-day-hour-min'
     * then parsing is sucessful and these 3 strings are correctly set and true is returned. 
     * Otherwise, false is returned. 
     * Here we don't check whether the dir name is matching peer name
     */
    static bool parseDirName(const string &dir, string &dirPrefix, string &peerName, string &dateTime) {
        string iDir = dir;
        if (!boost::starts_with(iDir, DIR_PREFIX)) {
            dirPrefix = "";
        } else {
            dirPrefix = DIR_PREFIX;
            boost::erase_first(iDir, DIR_PREFIX);
        }
        size_t off = iDir.find('-');
        if (off != iDir.npos) {
            peerName = iDir.substr(0, off);
            dateTime = iDir.substr(off);
            // m_prefix doesn't contain peer name or it equals with dirPrefix plus peerName
            return checkDirName(dateTime);
        }
        return false;
    }

    /** 
     * Find all entries in a given directory, return as sorted array of full paths in ascending order.
     * If m_prefix doesn't contain peer name information, then all log dirs for different peers in the
     * logdir are returned.
     */
    void getLogdirs(vector<string> &dirs) {
        if (!isDir(m_logdir)) {
            return;
        }
        ReadDir dir(m_logdir);
        BOOST_FOREACH(const string &entry, dir) {
            string dirPrefix, peerName, dateTime;
            // if directory name could not be parsed, ignore it
            if(parseDirName(entry, dirPrefix, peerName, dateTime)) {
                // if directory name is not satisfied with m_prefix, ignore it
                if (m_prefix == dirPrefix || m_prefix == (dirPrefix + peerName)) {
                    dirs.push_back(m_logdir + "/" + entry);
                }
            }
        }
        // sort vector in ascending order
        // if no peer name
        if(m_prefix.empty() || m_prefix == DIR_PREFIX){
            sort(dirs.begin(), dirs.end(), *this);
        } else {
            sort(dirs.begin(), dirs.end());
        }
    }

    // check the dir name is conforming to what format we write
    static bool checkDirName(const string& value) {
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

const char* const LogDir::DIR_PREFIX = "SyncEvolution-";

// this class owns the sync sources and (together with
// a logdir) handles writing of per-sync files as well
// as the final report 
// It also handles the virtual syncsources that is a combination of several
// real syncsources.
class SourceList : public vector<SyncSource *> {
public:
    enum LogLevel {
        LOGGING_QUIET,    /**< avoid all extra output */
        LOGGING_SUMMARY,  /**< sync report, but no database comparison */
        LOGGING_FULL      /**< everything */
    };

    struct SourceConfigSpecials{
        bool m_forceSlow;
        string m_alias;
        SourceConfigSpecials ():
            m_forceSlow(false),
            m_alias("")
        {
        }
    };

    std::vector<boost::shared_ptr<VirtualSyncSource> >m_virtualDS; /**All configured virtual datastores*/
    std::map<std::string, SourceConfigSpecials > m_configSpecials; /*Indicating whether the corresponding sync source is forced slow*/
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
    std::string url = getSyncURL();
    if (boost::starts_with(url, "http://") ||
        boost::starts_with(url, "https://")) {
#ifdef ENABLE_LIBSOUP
        boost::shared_ptr<SoupTransportAgent> agent(new SoupTransportAgent());
        agent->setConfig(*this);
        return agent;
#elif defined(ENABLE_LIBCURL)
        boost::shared_ptr<CurlTransportAgent> agent(new CurlTransportAgent());
        agent->setConfig(*this);
        return agent;
#endif
    } else if (url.find("obex-bt://") ==0) {
#ifdef ENABLE_BLUETOOTH
        std::string btUrl = url.substr (strlen ("obex-bt://"), std::string::npos);
        boost::shared_ptr<ObexTransportAgent> agent(new ObexTransportAgent(ObexTransportAgent::OBEX_BLUETOOTH));
        agent->setURL (btUrl);
        agent->connect();
        return agent;
#endif
    }

    SE_THROW("unsupported transport type is specified in the configuration");
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
        if (source.getFinalSyncMode() == SYNC_NONE) {
            // not active, suppress output
        } else if (extra2) {
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
        // -1 is used for alerting a restore from backup. Synthesis won't use this
        if (extra1 != -1) {
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
        } else {
            SE_LOG_INFO(NULL, NULL, "%s: restore from backup", source.getName());
            source.recordFinalSyncMode(SYNC_RESTORE_FROM_BACKUP);
        }
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
        if (source.getFinalSyncMode() == SYNC_NONE) {
        } else if (extra2 > 0) {
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
        if (source.getFinalSyncMode() == SYNC_NONE) {
        } else if (extra2 > 0) {
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
        if (source.getFinalSyncMode() == SYNC_NONE) {
        } else if (source.getFinalSyncMode() != SYNC_NONE) {
            SE_LOG_INFO(NULL, NULL, "%s: added %d, updated %d, removed %d",
                        source.getName(), extra1, extra2, extra3);
        }
        break;
    case sysync::PEV_SYNCEND:
        /* sync finished, probably with error in extra1 (0=ok),
           syncmode in extra2 (0=normal, 1=slow, 2=first time), 
           extra3=1 for resumed session) */
        if (source.getFinalSyncMode() == SYNC_NONE) {
            SE_LOG_INFO(NULL, NULL, "%s: inactive", source.getName());
        } else if(source.getFinalSyncMode() == SYNC_RESTORE_FROM_BACKUP) {
            SE_LOG_INFO(NULL, NULL, "%s: restore done %s", 
                        source.getName(),
                        extra1 ? "unsuccessfully" : "successfully" );
        } else {
            SE_LOG_INFO(NULL, NULL, "%s: %s%s sync done %s",
                        source.getName(),
                        extra3 ? "resumed " : "",
                        extra2 == 0 ? "normal" :
                        extra2 == 1 ? "slow" :
                        extra2 == 2 ? "first time" :
                        "unknown",
                        extra1 ? "unsuccessfully" : "successfully");
        }
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
        case 0:
            break;
        default:
            // Printing unknown status codes here is of somewhat questionable value,
            // because even "good" sources will get a bad status when the overall
            // session turns bad. We also don't have good explanations for the
            // status here.
            SE_LOG_ERROR(&source, NULL, "%s", Status2String(SyncMLStatus(extra1)).c_str());
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

SyncContext *SyncContext::findContext(const char *sessionName)
{
    return m_activeContext;
}

void SyncContext::initSources(SourceList &sourceList)
{
    list<string> configuredSources = getSyncSources();

    // Phase 1, check all virtual sync soruces
    BOOST_FOREACH(const string &name, configuredSources) {
        boost::shared_ptr<PersistentSyncSourceConfig> sc(getSyncSourceConfig(name));
        SyncSourceNodes source = getSyncSourceNodes (name);
        SourceType sourceType = SyncSource::getSourceType(source);
        // is the source enabled?
        string sync = sc->getSync();
        bool enabled = sync != "disabled";
        if (enabled) {
            if (sourceType.m_backend == "virtual") {
                //This is a virtual sync source, check and enable the referenced
                //sub syncsources here
                SyncSourceParams params(name, source);
                boost::shared_ptr<VirtualSyncSource> vSource = boost::shared_ptr<VirtualSyncSource> (new VirtualSyncSource (params));
                std::string evoSyncSource = vSource->getDatabaseID();
                bool valid = true;
                std::vector<std::string> mappedSources = unescapeJoinedString (evoSyncSource, ',');
                BOOST_FOREACH (std::string source, mappedSources) {
                    //check whether the mapped source is really available
                    boost::shared_ptr<PersistentSyncSourceConfig> source_config 
                        = getSyncSourceConfig(source);
                    if (!source_config || !source_config->exists()) {
                        SE_LOG_ERROR (NULL, NULL, 
                                "Virtual datasource %s referenced a non-existed datasource %s, check your configuration!",
                                vSource->getName(), source.c_str());
                        valid = false;
                        break;
                    }
                }
                if (valid) {
                    FilterConfigNode::ConfigFilter vFilter;
                    vFilter["sync"] = sync;
                    vFilter["uri"] = sc->getURI();
                    BOOST_FOREACH (std::string source, mappedSources) {
                        setConfigFilter (false, source, vFilter);
                    }
                }
                sourceList.m_virtualDS.push_back (vSource);
            }
        }
    }

    BOOST_FOREACH(const string &name, configuredSources) {
        boost::shared_ptr<PersistentSyncSourceConfig> sc(getSyncSourceConfig(name));

        SyncSourceNodes source = getSyncSourceNodes (name);
        SourceType sourceType = SyncSource::getSourceType(source);

        // is the source enabled?
        string sync = sc->getSync();
        bool enabled = sync != "disabled";
        if (enabled) {
            if (sourceType.m_backend != "virtual") {
                string url = getSyncURL();
                SyncSourceParams params(name,
                        source);
                SyncSource *syncSource =
                    SyncSource::createSource(params);
                if (!syncSource) {
                    throwError(name + ": type unknown" );
                }
                sourceList.push_back(syncSource);
            }
            sourceList.m_configSpecials[name].m_alias = sc->getURI();
        } else {
            // the Synthesis engine is never going to see this source,
            // therefore we have to mark it as 100% complete and
            // "done"
            class DummySyncSource source(name);
            source.recordFinalSyncMode(SYNC_NONE);
            displaySourceProgress(sysync::PEV_PREPARING,
                                  source,
                                  0, 0, 0);
            displaySourceProgress(sysync::PEV_ITEMPROCESSED,
                                  source,
                                  0, 0, 0);
            displaySourceProgress(sysync::PEV_ITEMRECEIVED,
                                  source,
                                  0, 0, 0);
            displaySourceProgress(sysync::PEV_ITEMSENT,
                                  source,
                                  0, 0, 0);
            displaySourceProgress(sysync::PEV_SYNCEND,
                                  source,
                                  0, 0, 0);
        }
    }
}

bool SyncContext::transport_cb (void *udata)
{
    unsigned int interval = reinterpret_cast<uintptr_t>(udata);
    SE_LOG_INFO(NULL, NULL, "Transport timeout after %u:%02umin",
                interval / 60,
                interval % 60);
    // never cancel the transport, the higher levels will deal
    // with the timeout
    return true;
}

void SyncContext::setTransportCallback(int seconds)
{
    m_agent->setCallback(transport_cb,
                         reinterpret_cast<void *>(static_cast<uintptr_t>(seconds)),
                         seconds);
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

    const char *sessioninitscript =
        "    <sessioninitscript><![CDATA[\n"
        "      // these variables are possibly modified by rule scripts\n"
        "      TIMESTAMP mindate; // earliest date remote party can handle\n"
        "      INTEGER retransfer_body; // if set to true, body is re-sent to client when message is moved from outbox to sent\n"
        "      mindate=EMPTY; // no limit by default\n"
        "      retransfer_body=FALSE; // normally, do not retransfer email body (and attachments) when moving items to sent box\n"
        "      INTEGER delayedabort;\n"
        "      delayedabort = FALSE;\n"
        "    ]]></sessioninitscript>\n";

    ostringstream clientorserver;
    if (m_serverMode) {
        clientorserver <<
            "  <server type='plugin'>\n"
            "    <plugin_module>SyncEvolution</plugin_module>\n"
            "    <plugin_sessionauth>yes</plugin_sessionauth>\n"
            "    <plugin_deviceadmin>yes</plugin_deviceadmin>\n"
            "\n" <<
            sessioninitscript <<
            "    <sessiontimeout>300</sessiontimeout>\n"
            "\n"
            "    <defaultauth/>\n"
            "\n"
            "    <datastore/>\n"
            "\n"
            "    <remoterules/>\n"
            "  </server>\n";
    } else {
        clientorserver <<
            "  <client type='plugin'>\n"
            "    <binfilespath>$(binfilepath)</binfilespath>\n"
            "    <defaultauth/>\n"
            "\n" <<
            sessioninitscript <<
            // SyncEvolution has traditionally not folded long lines in
            // vCard.  Testing showed that servers still have problems with
            // it, so avoid it by default
            "    <donotfoldcontent>yes</donotfoldcontent>\n"
            "\n"
            "    <fakedeviceid/>\n"
            "\n"
            "    <datastore/>\n"
            "\n"
            "    <remoterules/>\n"
            "  </client>\n";
    }
    substTag(xml,
             "clientorserver",
             clientorserver.str(),
             true);

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
            "    <filename>" <<
            LogfileBasename << "</filename>" <<
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
                fragment;

            string mode = source->getSync();
            const struct SourceList::SourceConfigSpecials &special =
                m_sourceListPtr->m_configSpecials[source->getName()];
            if (special.m_forceSlow) {
                // we *want* a slow sync, but couldn't tell the client -> force it server-side
                datastores << "      <alertscript> FORCESLOWSYNC(); </alertscript>\n";
            } else if (mode != "slow" &&
                       mode != "refresh-from-server" && // is implemented as "delete local data" + "slow sync",
                                                        // so a slow sync is acceptable in this case
                       !m_serverMode) {
                // We are not expecting a slow sync => refuse to execute one.
                // This is the client check for this, server must be handled
                // differently (TODO).
                datastores <<
                    "      <alertscript><![CDATA[\n"
                    "INTEGER alertcode;\n"
                    "alertcode = ALERTCODE();\n"
                    "if (alertcode == 201) {\n" // SLOWSYNC() cannot be used here, refresh-from-client also sets it
                    "   DEBUGMESSAGE(\"slow sync not expected by SyncEvolution, disabling datastore\");\n"
                    "   ABORTDATASTORE(" << STATUS_UNEXPECTED_SLOW_SYNC <<
                    ");\n"
                    "   // tell UI to abort instead of sending the next message\n"
                    "   SETSESSIONVAR(\"delayedabort\", 1);\n"
                    "}\n"
                    "]]></alertscript>\n";
            }

            if (m_serverMode && !special.m_alias.empty()) {
                datastores << " <alias name='" << special.m_alias << "'/>";
            }

            datastores << "    </datastore>\n\n";
        }

        /*If there is super datastore, add it here*/
        //TODO generate specific superdatastore contents (MB #8753)
        //Now only works for synthesis built-in events+tasks
        BOOST_FOREACH (boost::shared_ptr<VirtualSyncSource> vSource, m_sourceListPtr->m_virtualDS) {
            std::string superType = vSource->getSourceType().m_format;
            std::string evoSyncSource = vSource->getDatabaseID();
            std::vector<std::string> mappedSources = unescapeJoinedString (evoSyncSource, ',');
            //only check the type when user uses forceFormat
            if (vSource->getSourceType().m_forceFormat) {
                if (superType.find_last_of (":") != superType.npos) {
                    superType = superType.substr (superType.find_last_of (":"));
                }
                BOOST_FOREACH (std::string source, mappedSources) {
                    //check the data type
                    SyncSource *subSource = (*m_sourceListPtr)[source];
                    std::string subType = subSource->getPeerMimeType();
                    if (subType.empty()) {
                        subType = subSource->getSourceType().m_format;
                        if (subType.find_last_of (":") != subType.npos) {
                            subType = subType.substr (subType.find_last_of (":"));
                        }
                    }
                    if (superType != subType) {
                        SE_LOG_WARNING (NULL, NULL, 
                                "Virtual datasource %s and sub datasource %s has different data format, will use the format in virtual datasource",
                                vSource->getName(), source.c_str());
                        break;
                    }
                }

            }

            if (mappedSources.size() !=2) {
                vSource->throwError ("virtual data source now only supports events+tasks case");
            } 

            datastores << "    <superdatastore name= '" << vSource->getName() <<"'> \n";
            datastores << "      <contains datastore = '" << mappedSources[0] <<"'>\n"
                << "        <dispatchfilter>F.ISEVENT:=1</dispatchfilter>\n"
                << "        <guidprefix>e</guidprefix>\n"
                << "      </contains>\n"
                <<"\n      <contains datastore = '" << mappedSources[1] <<"'>\n"
                << "        <dispatchfilter>F.ISEVENT:=0</dispatchfilter>\n"
                << "        <guidprefix>t</guidprefix>\n"
                <<"      </contains>\n" ;

            std::string typesupport;
            typesupport = vSource->getDataTypeSupport();
            if (typesupport.empty()) {
                SE_THROW ("datatype format is not set in virtual datasource configuration");
            } 
            datastores << "      <typesupport>\n"
                << typesupport 
                << "      </typesupport>\n";
            datastores <<"\n    </superdatastore>";
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
    substTag(xml, "remoterules",
             string("<remoterule name='EVOLUTION'><deviceid>none - this rule is activated via its name in MAKE/PARSETEXTWITHPROFILE() macro calls</deviceid></remoterule>\n") +
             fragments.m_remoterules.join(),
             true);

    if (m_serverMode) {
        // TODO: set the device ID for an OBEX server
    } else {
        substTag(xml, "fakedeviceid", getDevID());
    }
    substTag(xml, "model", getMod());
    substTag(xml, "manufacturer", getMan());
    substTag(xml, "hardwareversion", getHwv());
    // abuse (?) the firmware version to store the SyncEvolution version number
    substTag(xml, "firmwareversion", getSwv());
    substTag(xml, "devicetype", getDevType());
    substTag(xml, "maxmsgsize", std::max(getMaxMsgSize(), 10000ul));
    substTag(xml, "maxobjsize", std::max(getMaxObjSize(), 1024u));
    if (m_serverMode) {
        const char *user = getUsername();
        const char *password = getPassword();

        if (user[0] || password[0]) {
            // require authentication with the configured password
            substTag(xml, "defaultauth",
                     "<requestedauth>md5</requestedauth>\n"
                     "<requiredauth>basic</requiredauth>\n"
                     "<autononce>yes</autononce>\n",
                     true);
        } else {
            // no authentication required
            substTag(xml, "defaultauth",
                     "<logininitscript>return TRUE</logininitscript>\n"
                     "<requestedauth>none</requestedauth>\n"
                     "<requiredauth>none</requiredauth>\n"
                     "<autononce>yes</autononce>\n",
                     true);
        }
    } else {
        substTag(xml, "defaultauth", getClientAuthType());
    }

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

    // This instance of the engine is used outside of the sync session
    // itself for logging. doSync() then reinitializes it with a full
    // datastore configuration.
    engine.Connect(m_serverMode ?
#ifdef ENABLE_SYNCML_LINKED
                   // use Synthesis client or server engine that we were linked against
                   "[server:]" : "[]",
#else
                   // load engine dynamically
                   "server:libsynthesis.so.0" : "libsynthesis.so.0",
#endif
                   0,
                   sysync::DBG_PLUGIN_NONE|
                   sysync::DBG_PLUGIN_INT|
                   sysync::DBG_PLUGIN_DB|
                   sysync::DBG_PLUGIN_EXOT);

    SharedKey configvars = engine.OpenKeyByPath(SharedKey(), "/configvars");
    string logdir;
    if (m_sourceListPtr) {
        logdir = m_sourceListPtr->getLogdir();
    }
    engine.SetStrValue(configvars, "defout_path",
                       logdir.size() ? logdir : "/dev/null");
    engine.SetStrValue(configvars, "conferrpath", "console");
    engine.SetStrValue(configvars, "binfilepath", getSynthesisDatadir().c_str());
    configvars.reset();

    return engine;
}

namespace {
    void GnutlsLogFunction(int level, const char *str)
    {
        SE_LOG_DEBUG(NULL, "GNUTLS", "level %d: %s", level, str);
    }
}

void SyncContext::initServer(const std::string &sessionID,
                             SharedBuffer data,
                             const std::string &messageType)
{
    m_serverMode = true;
    m_sessionID = sessionID;
    m_initialMessage = data;
    m_initialMessageType = messageType;
    
}

struct SyncContext::SyncMLMessageInfo
SyncContext::analyzeSyncMLMessage(const char *data, size_t len,
                                  const std::string &messageType)
{
    SyncContext sync;
    SwapContext syncSentinel(&sync);
    SourceList sourceList(sync, false);
    sourceList.setLogLevel(SourceList::LOGGING_SUMMARY);
    m_sourceListPtr = &sourceList;
    sync.initServer("", SharedBuffer(), "");
    SwapEngine swapengine(sync);
    sync.initEngine(false);

    sysync::TEngineProgressInfo progressInfo;
    sysync::uInt16 stepCmd = sysync::STEPCMD_GOTDATA;
    SharedSession session = sync.m_engine.OpenSession(sync.m_sessionID);
    SessionSentinel sessionSentinel(sync, session);

    sync.m_engine.WriteSyncMLBuffer(session, data, len);
    SharedKey sessionKey = sync.m_engine.OpenSessionKey(session);
    sync.m_engine.SetStrValue(sessionKey,
                              "contenttype",
                              messageType);

    // analyze main loop: runs until SessionStep() signals reply or error.
    // Will call our SynthesisDBPlugin callbacks, most importantly
    // SyncEvolution_Session_CheckDevice(), which records the device ID
    // for us.
    do {
        sync.m_engine.SessionStep(session, stepCmd, &progressInfo);
        switch (stepCmd) {
        case sysync::STEPCMD_OK:
        case sysync::STEPCMD_PROGRESS:
            stepCmd = sysync::STEPCMD_STEP;
            break;
        default:
            // whatever it is, cannot proceed
            break;
        }
    } while (stepCmd == sysync::STEPCMD_STEP);

    SyncMLMessageInfo info;
    info.m_deviceID = sync.getSyncDeviceID();
    return info;
}

void SyncContext::initEngine(bool logXML)
{
    string xml, configname;
    getConfigXML(xml, configname);
    try {
        m_engine.InitEngineXML(xml.c_str());
    } catch (const BadSynthesisResult &ex) {
        SE_LOG_ERROR(NULL, NULL,
                     "internal error, invalid XML configuration (%s):\n%s",
                     m_sourceListPtr && !m_sourceListPtr->empty() ?
                     "with datastores" :
                     "without datastores",
                     xml.c_str());
        throw;
    }
    if (logXML) {
        SE_LOG_DEV(NULL, NULL, "Full XML configuration:\n%s", xml.c_str());
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

    // careful about scope, this is needed for writing the
    // report below
    SyncReport buffer;

    SwapContext syncSentinel(this);
    try {
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


        /* Must detect server or client session before creating the
         * underlying SynthesisEngine 
         * */
        if ( getPeerIsClient()) {
            m_serverMode = true;
        }

        // create a Synthesis engine, used purely for logging purposes
        // at this time
        SwapEngine swapengine(*this);
        initEngine(false);

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
                prop->checkPassword(*this, m_server, *getProperties());
            }
            BOOST_FOREACH(SyncSource *source, sourceList) {
                ConfigPropertyRegistry& registry = SyncSourceConfig::getRegistry();
                BOOST_FOREACH(const ConfigProperty *prop, registry) {
                    prop->checkPassword(*this, m_server, *getProperties(),
                                        source->getName(), source->getProperties());
                }
            }

            // open each source - failing now is still safe
            BOOST_FOREACH(SyncSource *source, sourceList) {
                if (m_serverMode) {
                    source->enableServerMode();
                }
                source->open();
            }

            // give derived class also a chance to update the configs
            prepare(sourceList);

            // TODO: in server mode don't dump all databases. Wait until
            // the client is logged in successfully and we know which
            // sources it needs.
            // ready to go: dump initial databases and prepare for final report
            sourceList.syncPrepare();
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

bool SyncContext::initSAN() 
{
    sysync::SanPackage san;
    /* Should be nonce sent by the server in the preceeding sync session */
    string nonce = "SyncEvolution";
    /* SyncML Version 1.2 */
    uint16_t protoVersion = 12;
    string uauthb64 = san.B64_H (getUsername(), getPassword());
    /* Client is expected to conduct the sync in the backgroud */
    sysync::UI_Mode mode = sysync::UI_not_specified;

    uint16_t sessionId = 0;
    string serverId = getRemoteIdentifier();
    if(serverId.empty()) {
        serverId = getDevID();
    }
    san.PreparePackage( uauthb64, nonce, protoVersion, mode, 
            sysync::Initiator_Server, sessionId, serverId);

    san.CreateEmptyNotificationBody();
    bool hasSource = false;
     
    std::set<std::string> dataSources = m_sourceListPtr->getSources();

    /* For each virtual datasoruce, generate the SAN accoring to it and ignoring
     * sub datasource in the later phase*/
    BOOST_FOREACH (boost::shared_ptr<VirtualSyncSource> vSource, m_sourceListPtr->m_virtualDS) {
            std::string evoSyncSource = vSource->getDatabaseID();
            std::string sync = vSource->getSync();
            int mode = StringToSyncMode (sync, true);
            std::vector<std::string> mappedSources = unescapeJoinedString (evoSyncSource, ',');
            BOOST_FOREACH (std::string source, mappedSources) {
                dataSources.erase (source);
                if (mode == SYNC_SLOW) {
                    m_sourceListPtr->m_configSpecials[source].m_forceSlow = true;
                }
            }
            dataSources.insert (vSource->getName());
    }
    /* For each source to be notified do the following: */
    BOOST_FOREACH (string name, dataSources) {
        boost::shared_ptr<PersistentSyncSourceConfig> sc(getSyncSourceConfig(name));
        string sync = sc->getSync();
        int mode = StringToSyncMode (sync, true);
        if (mode == SYNC_SLOW) {
            m_sourceListPtr->m_configSpecials[name].m_forceSlow = true;
            mode = SA_SYNC_TWO_WAY;
        }
        if (mode <SYNC_FIRST || mode >SYNC_LAST) {
            SE_LOG_DEV (NULL, NULL, "Ignoring data source %s with an invalid sync mode", name.c_str());
            continue;
        }
        hasSource = true;
        string uri = sc->getURI();

        SourceType sourceType = sc->getSourceType();
        /*If the type is not set by user explictly, let's use backend default
         * value*/
        if(sourceType.m_format.empty()) {
            sourceType.m_format = (*m_sourceListPtr)[name]->getPeerMimeType();
        }
        /*If user did not use force type, we will always use the older type as
         * this is what most phones support*/
        int contentTypeB = StringToContentType (sourceType.m_format, sourceType.m_forceFormat);
        if (contentTypeB == WSPCTC_UNKNOWN) {
            contentTypeB = 0;
            SE_LOG_DEBUG (NULL, NULL, "Unknown datasource mimetype, use 0 as default");
        }
        if ( san.AddSync(mode, (uInt32) contentTypeB, uri.c_str())) {
            SE_LOG_ERROR(NULL, NULL, "SAN: adding server alerted sync element failed");
        };
    }

    if (!hasSource) {
        SE_THROW ("No source enabled for server alerted sync!");
    }

    /* Generate the SAN Package */
    void *buffer;
    size_t sanSize;
    if (san.GetPackage(buffer, sanSize)){
        SE_LOG_ERROR (NULL, NULL, "SAN package generating faield");
        return false;
    }

    try {
        m_agent = createTransportAgent();
        // Time out after the complete retry duration. This is the
        // initial message of a sync, so we don't resend it (just as
        // in a HTTP SyncML client trying to contact server).
        if (m_retryDuration) {
            setTransportCallback(m_retryDuration);
        }

        SE_LOG_INFO (NULL, NULL, "Server sending SAN");
        m_agent->setContentType(TransportAgent::m_contentTypeServerAlertedNotificationDS);
        m_agent->send(reinterpret_cast <char *> (buffer), sanSize);
        //change content type
        m_agent->setContentType(getWBXML() ? TransportAgent::m_contentTypeSyncWBXML :
                                TransportAgent::m_contentTypeSyncML);

        TransportAgent::Status status;
        do {
            status = m_agent->wait();
        } while(status == TransportAgent::ACTIVE);
        if (status == TransportAgent::GOT_REPLY) {
            const char *reply;
            size_t replyLen;
            string contentType;
            m_agent->getReply (reply, replyLen, contentType);

            //sanity check for the reply 
            if (contentType.empty() || 
                contentType.find(TransportAgent::m_contentTypeSyncML) != contentType.npos ||
                contentType.find(TransportAgent::m_contentTypeSyncWBXML) != contentType.npos) {
                SharedBuffer request (reply, replyLen);
                //TODO should generate more reasonable sessionId here
                string sessionId ="";
                initServer (sessionId, request, contentType);
                return true;
            }
        }
    } catch (TransportException e) {
        SE_LOG_ERROR (NULL, NULL, "TransportException while sending SAN package");
    }
    return false;
}

static string Step2String(sysync::uInt16 stepcmd)
{
    switch (stepcmd) {
    case sysync::STEPCMD_CLIENTSTART: return "STEPCMD_CLIENTSTART";
    case sysync::STEPCMD_CLIENTAUTOSTART: return "STEPCMD_CLIENTAUTOSTART";
    case sysync::STEPCMD_STEP: return "STEPCMD_STEP";
    case sysync::STEPCMD_GOTDATA: return "STEPCMD_GOTDATA";
    case sysync::STEPCMD_SENTDATA: return "STEPCMD_SENTDATA";
    case sysync::STEPCMD_SUSPEND: return "STEPCMD_SUSPEND";
    case sysync::STEPCMD_ABORT: return "STEPCMD_ABORT";
    case sysync::STEPCMD_TRANSPFAIL: return "STEPCMD_TRANSPFAIL";
    case sysync::STEPCMD_TIMEOUT: return "STEPCMD_TIMEOUT";
    case sysync::STEPCMD_SAN_CHECK: return "STEPCMD_SAN_CHECK";
    case sysync::STEPCMD_AUTOSYNC_CHECK: return "STEPCMD_AUTOSYNC_CHECK";
    case sysync::STEPCMD_OK: return "STEPCMD_OK";
    case sysync::STEPCMD_PROGRESS: return "STEPCMD_PROGRESS";
    case sysync::STEPCMD_ERROR: return "STEPCMD_ERROR";
    case sysync::STEPCMD_SENDDATA: return "STEPCMD_SENDDATA";
    case sysync::STEPCMD_NEEDDATA: return "STEPCMD_NEEDDATA";
    case sysync::STEPCMD_RESENDDATA: return "STEPCMD_RESENDDATA";
    case sysync::STEPCMD_DONE: return "STEPCMD_DONE";
    case sysync::STEPCMD_RESTART: return "STEPCMD_RESTART";
    case sysync::STEPCMD_NEEDSYNC: return "STEPCMD_NEEDSYNC";
    default: return StringPrintf("STEPCMD %d", stepcmd);
    }
}

SyncMLStatus SyncContext::doSync()
{
    // install signal handlers only if default behavior
    // is currently active, restore when we return
    struct sigaction new_action, old_action;
    memset(&new_action, 0, sizeof(new_action));
    new_action.sa_handler = handleSignal;
    sigemptyset(&new_action.sa_mask);
    sigaction(SIGINT, NULL, &old_action);
    if (old_action.sa_handler == SIG_DFL) {
        sigaction(SIGINT, &new_action, NULL);
    }

    struct sigaction old_term_action;
    sigaction(SIGTERM, NULL, &old_term_action);
    if (old_term_action.sa_handler == SIG_DFL) {
        sigaction(SIGTERM, &new_action, NULL);
    }   

    SyncMLStatus status = STATUS_OK;
    std::string s;

    if (m_serverMode && !m_initialMessage.size()) {
        //This is a server alerted sync !
        if (! initSAN ()) {
            // return a proper error code 
            throwError ("Server Alerted Sync init failed");
        }
    }

    // re-init engine with all sources configured
    string xml, configname;
    initEngine(true);

    SharedKey targets;
    SharedKey target;
    if (m_serverMode) {
        // Server engine has no profiles. All settings have be done
        // via the XML configuration or function parameters (session ID
        // in OpenSession()).
    } else {
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
        targets = m_engine.OpenKeyByPath(profile, "targets");

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
    }

    m_retryInterval = getRetryInterval();
    m_retryDuration = getRetryDuration();
    m_retries = 0;

    //Create the transport agent if not already created
    if(!m_agent) {
        m_agent = createTransportAgent();
    }

    sysync::TEngineProgressInfo progressInfo;
    sysync::uInt16 stepCmd = 
        m_serverMode ?
        sysync::STEPCMD_GOTDATA :
        sysync::STEPCMD_CLIENTSTART;
    SharedSession session = m_engine.OpenSession(m_sessionID);
    SharedBuffer sendBuffer;
    SessionSentinel sessionSentinel(*this, session);

    if (m_serverMode) {
        m_engine.WriteSyncMLBuffer(session,
                                   m_initialMessage.get(),
                                   m_initialMessage.size());
        SharedKey sessionKey = m_engine.OpenSessionKey(session);
        m_engine.SetStrValue(sessionKey,
                             "contenttype",
                             m_initialMessageType);
        m_initialMessage.reset();

        // TODO: set "sendrespuri" session key to control
        // whether the generated messages contain a respURI
        // (not needed for OBEX)
    }

    // Sync main loop: runs until SessionStep() signals end or error.
    // Exceptions are caught and lead to a call of SessionStep() with
    // parameter STEPCMD_ABORT -> abort session as soon as possible.
    bool aborting = false;
    int suspending = 0; 
    time_t sendStart = 0, resendStart = 0;
    int requestNum = 0;
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
                SE_LOG_DEBUG(NULL, NULL, "suspending before SessionStep() in STEPCMD_GOTDATA as requested by user");
                stepCmd = sysync::STEPCMD_SUSPEND;
            }

            // Aborting is useful while waiting for a reply and before
            // sending a message (which will just lead to us waiting
            // for the reply, but possibly after doing some slow network
            // IO for setting up the message send).
            //
            // While processing a message we let the engine run, because
            // that is a) likely to be done soon and b) may reduce the
            // breakage caused by aborting a running sync.
            //
            // This check here covers the "waiting for reply" case.
            if ((stepCmd == sysync::STEPCMD_RESENDDATA ||
                 stepCmd == sysync::STEPCMD_SENTDATA ||
                 stepCmd == sysync::STEPCMD_NEEDDATA) &&
                checkForAbort()) {
                SE_LOG_DEBUG(NULL, NULL, "aborting before SessionStep() in %s as requested by script",
                             Step2String(stepCmd).c_str());
                stepCmd = sysync::STEPCMD_ABORT;
            }

            // take next step, but don't abort twice: instead
            // let engine contine with its shutdown
            if (stepCmd == sysync::STEPCMD_ABORT) {
                if (aborting) {
                    SE_LOG_DEBUG(NULL, NULL, "engine already notified of abort request, reverting to %s",
                                 Step2String(previousStepCmd).c_str());
                    stepCmd = previousStepCmd;
                } else {
                    aborting = true;
                }
            }
            // same for suspending
            if (stepCmd == sysync::STEPCMD_SUSPEND) {
                if (suspending) {
                    SE_LOG_DEBUG(NULL, NULL, "engine already notified of suspend request, reverting to %s",
                                 Step2String(previousStepCmd).c_str());
                    stepCmd = previousStepCmd;
                    suspending++;
                } else {
                    suspending++; 
                }
            }

            if (stepCmd == sysync::STEPCMD_NEEDDATA) {
                // Engine already notified. Don't call it twice
                // with this state, because it doesn't know how
                // to handle this. Skip the SessionStep() call
                // and wait for response.
            } else {
                m_engine.SessionStep(session, stepCmd, &progressInfo);
                reportStepCmd(stepCmd);
            }

            if (stepCmd == sysync::STEPCMD_SENDDATA &&
                checkForScriptAbort(session)) {
                SE_LOG_DEBUG(NULL, NULL, "aborting after SessionStep() in STEPCMD_SENDDATA as requested by script");

                // Catch outgoing message and abort if requested by script.
                // Report which sources are affected, based on their status code.
                list<string> sources;
                BOOST_FOREACH(SyncSource *source, *m_sourceListPtr) {
                    if (source->getStatus() == STATUS_UNEXPECTED_SLOW_SYNC) {
                        sources.push_back(source->getName());
                    }
                }
                if (!sources.empty()) {
                    string sourceparam = boost::join(sources, " ");
                    SE_LOG_ERROR(NULL, NULL,
                                 "Aborting because of unexpected slow sync for source(s): %s",
                                 sourceparam.c_str());
                    SE_LOG_INFO(NULL, NULL,
                                "Doing a slow synchronization may lead to duplicated items or\n"
                                "lost data when the server merges items incorrectly. Choosing\n"
                                "a different synchronization mode may be the better alternative.\n"
                                "Restart synchronization of affected source(s) with one of the\n"
                                "following sync modes to recover from this problem:\n"
                                "    slow, refresh-from-server, refresh-from-client\n\n"
                                "Analyzing the current state:\n"
                                "    syncevolution --status %s %s\n\n"
                                "Running with one of the three modes:\n"
                                "    syncevolution --sync [slow|refresh-from-server|refresh-from-client] %s %s\n",
                                m_server.c_str(), sourceparam.c_str(),
                                m_server.c_str(), sourceparam.c_str());
                } else {
                    // we should not get here, but if we do, at least log something
                    SE_LOG_ERROR(NULL, NULL, "aborting as requested by script");
                }
                stepCmd = sysync::STEPCMD_ABORT;
                continue;
            } else if (stepCmd == sysync::STEPCMD_SENDDATA &&
                       checkForAbort()) {
                // Catch outgoing message and abort if requested by user.
                SE_LOG_DEBUG(NULL, NULL, "aborting after SessionStep() in STEPCMD_SENDDATA as requested by user");
                stepCmd = sysync::STEPCMD_ABORT;
                continue;
            } else if (suspending == 1) {
                //During suspention we actually insert a STEPCMD_SUSPEND cmd
                //Should restore to the original step here
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
                        if (!m_serverMode) {
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
                        }
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

                SharedKey sessionKey = m_engine.OpenSessionKey(session);
                if (m_serverMode) {
                    m_agent->setURL("");
                } else {
                    // use OpenSessionKey() and GetValue() to retrieve "connectURI"
                    // and "contenttype" to be used to send data to the server
                    s = m_engine.GetStrValue(sessionKey,
                                             "connectURI");
                    m_agent->setURL(s);
                }
                s = m_engine.GetStrValue(sessionKey,
                                         "contenttype");
                m_agent->setContentType(s);
                sessionKey.reset();
                
                sendStart = resendStart = time (NULL);
                int timeout = m_serverMode ? m_retryDuration : m_retryInterval;
                if (timeout) {
                    setTransportCallback(timeout);
                }
                requestNum ++;
                // use GetSyncMLBuffer()/RetSyncMLBuffer() to access the data to be
                // sent or have it copied into caller's buffer using
                // ReadSyncMLBuffer(), then send it to the server
                sendBuffer = m_engine.GetSyncMLBuffer(session, true);
                m_agent->send(sendBuffer.get(), sendBuffer.size());
                stepCmd = sysync::STEPCMD_SENTDATA; // we have sent the data
                break;
            }
            case sysync::STEPCMD_RESENDDATA: {
                SE_LOG_INFO (NULL, NULL, "SyncContext: resend previous request #%d", m_retries);
                resendStart = time(NULL);
                /* We are resending previous message, just read from the
                 * previous buffer */
                m_agent->send(sendBuffer.get(), sendBuffer.size());
                stepCmd = sysync::STEPCMD_SENTDATA; // we have sent the data
                break;
            }
            case sysync::STEPCMD_NEEDDATA:
                switch (m_agent->wait()) {
                case TransportAgent::ACTIVE:
                    // Still sending the data?! Don't change anything,
                    // skip SessionStep() above.
                    break;
               
                case TransportAgent::TIME_OUT: {
                    time_t duration = time(NULL) - sendStart;
                    // HTTP SyncML servers cannot resend a HTTP POST
                    // reply.  Other server transports could in theory
                    // resend, but don't have the necessary D-Bus APIs
                    // (MB #6370).
                    if (m_serverMode ||
                        duration > m_retryDuration){
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
                    m_agent->getReply(reply, replylen, contentType);

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
                        if (m_serverMode) {
                            SharedKey sessionKey = m_engine.OpenSessionKey(session);
                            m_engine.SetStrValue(sessionKey,
                                                 "contenttype",
                                                 contentType);
                        }
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
                    if (m_serverMode ||
                        !m_retryInterval || duration > m_retryDuration || requestNum == 1) {
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
                            SE_LOG_DEBUG(NULL, NULL, "suspending after TransportAgent::FAILED as requested by user");
                            stepCmd = sysync::STEPCMD_SUSPEND;
                            break;
                        } else if (checkForAbort()) {
                            SE_LOG_DEBUG(NULL, NULL, "aborting after TransportAgent::FAILED as requested by user");
                            stepCmd = sysync::STEPCMD_ABORT;
                            break;
                        }

                        // retry send
                        int leftTime = m_retryInterval - (curTime - resendStart);
                        if (leftTime >0 ) {
                            if (sleep (leftTime) > 0) {
                                if (checkForSuspend()) {
                                    SE_LOG_DEBUG(NULL, NULL, "suspending after premature exit from sleep() caused by user suspend");
                                    stepCmd = sysync::STEPCMD_SUSPEND;
                                } else {
                                    SE_LOG_DEBUG(NULL, NULL, "aborting after premature exit from sleep() caused by user abort");
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
                SE_LOG_DEBUG(NULL, NULL, "aborting after catching fatal error");
                stepCmd = sysync::STEPCMD_ABORT;
            }
        } catch (...) {
            Exception::handle(&status);
            SE_LOG_DEBUG(NULL, NULL, "aborting after catching fatal error");
            stepCmd = sysync::STEPCMD_ABORT;
        }
    } while (stepCmd != sysync::STEPCMD_DONE && stepCmd != sysync::STEPCMD_ERROR);

    // If we get here without error, then close down connection normally.
    // Otherwise destruct the agent without further communication.
    if (!status && !checkForAbort()) {
        try {
            m_agent->shutdown();
            // TODO: implement timeout for peers which fail to respond
            while (!checkForAbort() &&
                   m_agent->wait(true) == TransportAgent::ACTIVE) {
                // TODO: allow aborting the sync here
            }
        } catch (...) {
            status = handleException();
        }
    }

    m_agent.reset();
    sigaction (SIGINT, &old_action, NULL);
    sigaction (SIGTERM, &old_term_action, NULL);
    return status;
}

SyncMLStatus SyncContext::handleException()
{
    SyncMLStatus res = Exception::handle();
    return res;
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
            prop->checkPassword(*this, m_server, *getProperties(),
                                source->getName(), source->getProperties());
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
            prop->checkPassword(*this, m_server, *getProperties(),
                                source->getName(), source->getProperties());
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

bool SyncContext::checkForScriptAbort(SharedSession session)
{
    try {
        SharedKey sessionKey = m_engine.OpenSessionKey(session);
        SharedKey contextKey = m_engine.OpenKeyByPath(sessionKey, "/sessionvars");
        bool abort = m_engine.GetInt32Value(contextKey, "delayedabort");
        return abort;
    } catch (NoSuchKey) {
        // this is necessary because the session might already have
        // been closed, which removes the variable
        return false;
    } catch (BadSynthesisResult) {
        return false;
    }
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
            prop->checkPassword(*this, m_server, *getProperties(),
                                source->getName(), source->getProperties());
        }
    }

    string datadump = database == DATABASE_BEFORE_SYNC ? "before" : "after";

    BOOST_FOREACH(SyncSource *source, sourceList) {
        // fake a source alert event
        displaySourceProgress(sysync::PEV_ALERTED, *source, -1, 0, 0);
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
                displaySourceProgress(sysync::PEV_SYNCSTART, *source, 0, 0, 0);
                sourceList.restoreDatabase(*source,
                                           datadump,
                                           m_dryrun,
                                           sourcereport);
                displaySourceProgress(sysync::PEV_SYNCEND, *source, 0, 0, 0);
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

string SyncContext::readSessionInfo(const string &dir, SyncReport &report)
{
    LogDir logging(*this);
    logging.openLogdir(dir);
    logging.readReport(report);
    return logging.getPeerNameFromLogdir(dir);
}

SE_END_CXX
