/*
 * Copyright (C) 2010 Intel Corporation
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

#include "syncevo/util.h"
#include "client-test-buteo.h"
#include <libsyncprofile/SyncResults.h>
#include <libsyncprofile/ProfileEngineDefs.h>
#include <libsyncprofile/Profile.h>
#include <libsyncprofile/SyncProfile.h>
#include <syncmlcommon/SyncMLCommon.h>
#include <QDomDocument>
#include <QtDBus>

// 3 databases used by tracker to store contacts
// empty string is used as separator
const string trackerdb_old[5] = {"meta.db", 
                                 "contents.db",
                                 "fulltext.db", // 3 databases used by tracker
                                 "", // separator
                                 "hcontacts.db" // database to record deleted contact items 
};
const string trackerdb_new[5] = {"meta.db", 
                                 "meta.db-shm",
                                 "meta.db-wal", // 3 databases used by tracker
                                 "", // separator
                                 "hcontacts.db" // database to record deleted contact items 
                                };
string QtContactsSwitcher::m_databases[5] = {};
string QtContactsSwitcher::m_dirs[2] = {"/.cache/tracker/",
                                       "/.sync/sync-app/"};

using namespace Buteo;
using namespace SyncEvo;

// execute a command. If 'check' is true, throw an exception when
// execution encounters error(s)
static void execCmd(const std::string &cmd, bool check = true)
{
    int result = Execute(cmd, ExecuteFlags(EXECUTE_NO_STDERR | EXECUTE_NO_STDOUT));
    if (result < 0 && check) {
        throw runtime_error("failed to excute command: " + cmd);
    }
}

bool ButeoTest::m_inited = false;
QString ButeoTest::m_deviceIds[2];
map<string, string> ButeoTest::m_source2storage;

ButeoTest::ButeoTest(ClientTest &client, 
                     const string &server,
                     const string &logbase,
                     const SyncEvo::SyncOptions &options) :
    m_client(client), m_server(server), m_logbase(logbase), m_options(options)
{
    init();
}

void ButeoTest::init()
{
    if (!m_inited) {
        m_inited = true;
        // generate device ids
        for(int i = 0; i < sizeof(m_deviceIds)/sizeof(m_deviceIds[0]); i++) {
            QString id;
            UUID uuid;
            QTextStream(&id) << "sc-pim-" << uuid.c_str();
            m_deviceIds[i] = id;
        }

        // insert source -> storage mappings
        m_source2storage.insert(std::make_pair("qt_vcard30", "hcontacts"));
        m_source2storage.insert(std::make_pair("kcal_ical20", "hcalendar"));
        m_source2storage.insert(std::make_pair("kcal_itodo20", "htodo"));
        m_source2storage.insert(std::make_pair("kcal_text", "hnotes"));

        //init qcoreapplication to use qt
        static const char *argv[] = { "SyncEvolution" };
        static int argc = 1;
        new QCoreApplication(argc, (char **)argv);
    }
}

void ButeoTest::prepareSources(const int *sources,
                               const vector<string> &source2Config) 
{
    for(int i = 0; sources[i] >= 0; i++) {
        string source = source2Config[sources[i]];
        map<string, string>::iterator it = m_source2storage.find(source);
        if (it != m_source2storage.end()) {
            m_configedSources.insert(it->second);
        } else {
            throw runtime_error("unsupported source '" + source + "'");
        }
    }
}

SyncMLStatus ButeoTest::doSync(SyncReport *report) 
{
    SyncMLStatus status = STATUS_OK;

    // kill msyncd
    killAllMsyncd();
    //set sync options
    setupOptions();
    // restore qtcontacts if needed
    if (inclContacts()) {
        QtContactsSwitcher::restoreStorage(m_client);
    }

    //start msyncd
    int pid = startMsyncd();
    //kill 'sh' process which is the parent of 'msyncd'
    stringstream cmd;
    cmd << "kill -9 " << pid;
    //run sync
    if (!run()) {
        execCmd(cmd.str(), false);
        killAllMsyncd();
        return STATUS_FATAL;
    }

    execCmd(cmd.str(), false);
    killAllMsyncd();
    // save qtcontacts if needed
    if (inclContacts()) {
        QtContactsSwitcher::backupStorage(m_client);
    }
    //get sync results
    genSyncResults(m_syncResults, report);
    return report->getStatus();
}

void ButeoTest::setupOptions()
{
    // 1. set deviceid, max-message-size options to /etc/sync/meego-sync-conf.xml
    QString meegoSyncmlConf = "/etc/sync/meego-syncml-conf.xml";
    QFile syncmlFile(meegoSyncmlConf);
    if (!syncmlFile.open(QIODevice::ReadOnly)) {
        throw runtime_error("can't open syncml config");
    }
    // don't invoke buteo-syncml API for it doesn't support flushing
    QString syncmlContent(syncmlFile.readAll());
    syncmlFile.close();
    int id = 0;
    if (!boost::ends_with(m_server, "_1")) {
        id = 1; 
    }

    //specify the db path which saves anchors related info, then we can wipe
    //out it if want to slow sync.
    replaceElement(syncmlContent, "dbpath", QString((m_server + ".db").c_str()));
    replaceElement(syncmlContent, "local-device-name", m_deviceIds[id]);

    QString msgSize;
    QTextStream(&msgSize) << m_options.m_maxMsgSize;
    replaceElement(syncmlContent, "max-message-size", msgSize);

    writeToFile(meegoSyncmlConf, syncmlContent);

    // 2. set storage 'Notebook Name' for calendar, todo and notes
    // for contacts, we have to set corresponding tracker db
    string storageDir = getHome() + "/.sync/profiles/storage/"; 
    BOOST_FOREACH(const string &source, m_configedSources) {
        if (boost::iequals(source, "hcalendar") ||
                boost::iequals(source, "htodo") ||
                boost::iequals(source, "hnotes")) {
            string filePath = storageDir + source + ".xml";
            QDomDocument doc(m_server.c_str());
            buildDomFromFile(doc, filePath.c_str());
            QString notebookName;
            QTextStream(&notebookName) << "client_test_" << id;
            Profile profile(doc.documentElement());
            profile.setKey("Notebook Name", notebookName);
            writeToFile(filePath.c_str(), profile.toString());
        }
    }
    
    // 3. set wbxml option, sync mode, enabled selected sources and disable other sources 
    QDomDocument doc(m_server.c_str());
    //copy profile
    string profileDir = getHome() + "/.sync/profiles/sync/";
    string profilePath = profileDir + m_server + ".xml";
    size_t pos = m_server.rfind('_');
    if (pos != m_server.npos) {
        string prefix = m_server.substr(0, pos);
        stringstream cmd;
        cmd << "cp " << profileDir
            << prefix << ".xml "
            << profilePath;
        execCmd(cmd.str());
    }
    buildDomFromFile(doc, profilePath.c_str());
    SyncProfile syncProfile(doc.documentElement());
    syncProfile.setName(m_server.c_str());
    QList<Profile *> storages = syncProfile.storageProfilesNonConst();
    QListIterator<Profile *> it(storages);
    while (it.hasNext()) {
        Profile * profile = it.next();
        set<string>::iterator configedIt = m_configedSources.find(profile->name().toStdString());
        if (configedIt != m_configedSources.end()) {
            profile->setKey(KEY_ENABLED, "true");
        } else {
            profile->setKey(KEY_ENABLED, "false");
        }
    }

    // set syncml client
    Profile * syncml = syncProfile.subProfile("syncml", "client");
    if (syncml) {
        // set whether using wbxml
        syncml->setBoolKey(PROF_USE_WBXML, m_options.m_isWBXML);
        // set sync mode
        QString syncMode;
        switch(m_options.m_syncMode) {
        case SYNC_NONE:
            break;
        case SYNC_TWO_WAY:
            syncMode = VALUE_TWO_WAY;
            break;
        case SYNC_ONE_WAY_FROM_CLIENT:
            // work around here since buteo doesn't support refresh mode now
            syncMode = VALUE_TO_REMOTE;
            break;
        case SYNC_REFRESH_FROM_CLIENT:
            // don't support, no workaround here
            throw runtime_error("Buteo doesn't support refresh mode");
        case SYNC_ONE_WAY_FROM_SERVER:
            syncMode = VALUE_FROM_REMOTE;
            break;
        case SYNC_REFRESH_FROM_SERVER: {
            //workaround here since buteo doesn't support refresh-from-server
            //wipe out anchors and remove tracker database
            //so we will do refresh-from-server by slow sync
            stringstream cmd1;
            cmd1 << "rm -f " << m_server << ".db";
            execCmd(cmd1.str(), false);
            if (inclContacts()) {
                execCmd("tracker-control -r", false);
                stringstream cmd2;
                cmd2 << "rm -f " 
                    << getHome() << "/.cache/tracker/*.db "
                    << getHome() << "/.cache/tracker/*.db_"
                    << m_client.getClientB() ? "1" : "2";
                execCmd(cmd2.str(), false);
            }
            syncMode = VALUE_TWO_WAY;
            break;
        }
        case SYNC_SLOW: {
            //workaround here since buteo doesn't support explicite slow-sync
            //wipe out anchors so we will do slow sync
            stringstream cmd;
            cmd << "rm -f " << m_server << ".db";
            execCmd(cmd.str(), false);
            syncMode = VALUE_TWO_WAY;
            break;
        }
        default:
            break;
        }
        syncml->setKey(KEY_SYNC_DIRECTION, syncMode);
    }
    writeToFile(profilePath.c_str(), syncProfile.toString());
}

void ButeoTest::killAllMsyncd()
{
    execCmd("killall -9 msyncd", false);
}

int ButeoTest::startMsyncd()
{
    int pid = fork();
    if (pid == 0) {
        //child
        stringstream cmd;
        cmd << "msyncd >" << m_logbase << ".log 2>&1";
        if (execlp("sh", "sh", "-c", cmd.str().c_str(), (char *)0) < 0 ) {
            exit(1);
        }
    } else if (pid < 0) {
        throw runtime_error("can't fork process");
    }
    // wait for msyncd get prepared
    execCmd("sleep 2", false);
    return pid;
}

bool ButeoTest::run()
{
    static const QString msyncdService = "com.meego.msyncd";
    static const QString msyncdObject = "/synchronizer";
    static const QString msyncdInterface = "com.meego.msyncd";

    QDBusConnection conn = QDBusConnection::sessionBus();
    std::auto_ptr<QDBusInterface> interface(new QDBusInterface(msyncdService, msyncdObject, msyncdInterface, conn));
    if (!interface->isValid()) {
        QString error = interface->lastError().message();
        return false;
    }

    // add watcher for watching unregistering service
    std::auto_ptr<QDBusServiceWatcher> dbusWatcher(new QDBusServiceWatcher(msyncdService, conn, QDBusServiceWatcher::WatchForUnregistration));
    dbusWatcher->connect(dbusWatcher.get(), SIGNAL(serviceUnregistered(QString)),
                           this, SLOT(serviceUnregistered(QString)));

    //connect signals
    interface->connect(interface.get(), SIGNAL(syncStatus(QString, int, QString, int)),
                       this, SLOT(syncStatus(QString, int, QString, int)));
    interface->connect(interface.get(), SIGNAL(resultsAvailable(QString, QString)),
                       this, SLOT(resultsAvailable(QString, QString)));

    // start sync
    QDBusReply<bool> reply = interface->call(QString("startSync"), m_server.c_str());
    if (reply.isValid() && !reply.value()) {
        return false;
    }

    // wait sync completed
    return QCoreApplication::exec() == 0;
}

void ButeoTest::genSyncResults(const QString &text, SyncReport *report)
{
    QDomDocument domResults;
    if (domResults.setContent(text, true)) {
        SyncResults syncResults(domResults.documentElement());
        switch(syncResults.majorCode()) {
        case SyncResults::SYNC_RESULT_SUCCESS:
            report->setStatus(STATUS_OK);
            break;
        case SyncResults::SYNC_RESULT_FAILED:
            report->setStatus(STATUS_FATAL);
            break;
        case SyncResults::SYNC_RESULT_CANCELLED:
            report->setStatus(STATUS_FATAL);
            break;
        };
        QList<TargetResults> targetResults = syncResults.targetResults();
        QListIterator<TargetResults> it(targetResults);
        while (it.hasNext()) {
            // get item sync info
            TargetResults target = it.next();
            SyncSourceReport targetReport;
            // temporary set this mode due to no this information in report
            targetReport.recordFinalSyncMode(m_options.m_syncMode);
            ItemCounts itemCounts = target.localItems();
            targetReport.setItemStat(SyncSourceReport::ITEM_LOCAL,
                                     SyncSourceReport::ITEM_ADDED,
                                     SyncSourceReport::ITEM_TOTAL,
                                     itemCounts.added);
            targetReport.setItemStat(SyncSourceReport::ITEM_LOCAL,
                                     SyncSourceReport::ITEM_UPDATED,
                                     SyncSourceReport::ITEM_TOTAL,
                                     itemCounts.modified);
            targetReport.setItemStat(SyncSourceReport::ITEM_LOCAL,
                                     SyncSourceReport::ITEM_REMOVED,
                                     SyncSourceReport::ITEM_TOTAL,
                                     itemCounts.deleted);

            // get item info for remote
            itemCounts = target.remoteItems();
            targetReport.setItemStat(SyncSourceReport::ITEM_REMOTE,
                                     SyncSourceReport::ITEM_ADDED,
                                     SyncSourceReport::ITEM_TOTAL,
                                     itemCounts.added);
            targetReport.setItemStat(SyncSourceReport::ITEM_REMOTE,
                                     SyncSourceReport::ITEM_UPDATED,
                                     SyncSourceReport::ITEM_TOTAL,
                                     itemCounts.modified);
            targetReport.setItemStat(SyncSourceReport::ITEM_REMOTE,
                                     SyncSourceReport::ITEM_REMOVED,
                                     SyncSourceReport::ITEM_TOTAL,
                                     itemCounts.deleted);
            // set to sync report
            report->addSyncSourceReport(target.targetName().toStdString(), targetReport);
        }
    } else {
        report->setStatus(STATUS_FATAL);
    }
}

void ButeoTest::syncStatus(QString profile, int status, QString message, int moreDetails)
{
    if (profile == m_server.c_str()) {
        switch(status) {
        case 0: // QUEUED
        case 1: // STARTED
        case 2: // PROGRESS
            break;
        case 3: // ERROR
        case 5: // ABORTED
            QCoreApplication::exit(1);
            break;
        case 4: // DONE
            QCoreApplication::exit(0);
            break;
        default:
            ;
        }
    }
}

void ButeoTest::resultsAvailable(QString profile, QString syncResults)
{
    if (profile == m_server.c_str()) {
        m_syncResults = syncResults;
    }
}

void ButeoTest::serviceUnregistered(QString service)
{
    QCoreApplication::exit(1);
}

bool ButeoTest::inclContacts()
{
    set<string>::iterator sit = m_configedSources.find("hcontacts");
    if (sit != m_configedSources.end()) {
        return true;
    }
    return false;
}

void ButeoTest::writeToFile(const QString &filePath, const QString &content)
{
    // clear tempoary file firstly
    stringstream rmCmd;
    rmCmd << "rm -f " << filePath.toStdString() << "_tmp";
    execCmd(rmCmd.str(), false);

    // open temporary file and serialize dom to the file
    QFile file(filePath + "_tmp");
    if (!file.open(QIODevice::WriteOnly)) {
        stringstream msg;
        msg << "can't open file '" << filePath.toStdString() << "' with 'write' mode";
        throw runtime_error(msg.str());
    }
    if (file.write(content.toUtf8()) == -1) {
        file.close();
        stringstream msg;
        msg << "can't write file '" << filePath.toStdString() << "'";
        throw runtime_error(msg.str());
    }
    file.close();

    // move temp file to destination file
    stringstream mvCmd;
    mvCmd << "mv " << filePath.toStdString() << "_tmp "
          << filePath.toStdString();
    execCmd(mvCmd.str());
}

void ButeoTest::replaceElement(QString &xml, const QString &elem, const QString &value)
{
    // TODO: use DOM to parse xml
    // currently this could work
    QString startTag = "<" + elem +">";
    QString endTag = "</" + elem +">";

    int start = xml.indexOf(startTag);
    if ( start == -1) {
        return;
    }
    int end = xml.indexOf(endTag, start);
    int pos = start + startTag.size();

    xml.replace(pos, end - pos, value);
}

void ButeoTest::buildDomFromFile(QDomDocument &doc, const QString &filePath)
{
    // open it 
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        stringstream msg;
        msg << "can't open profile file '" << filePath.toStdString() << "'";
        throw runtime_error(msg.str());
    }

    // parse it
    if (!doc.setContent(&file)) {
        file.close();
        stringstream msg;
        msg << "can't parse profile file '" << filePath.toStdString() << "'";
        throw runtime_error(msg.str());
    }
    file.close();
}

static bool isButeo()
{
    static bool checked = false;
    static bool useButeo = false;

    if (!checked) {
        const char *buteo = getenv("CLIENT_TEST_BUTEO");
        if (buteo && 
                (boost::equals(buteo, "1") || boost::iequals(buteo, "t"))) {
            useButeo = true;
        }
        checked = true;
    }

    return useButeo;
}

string QtContactsSwitcher::getId(ClientTest &client) {
    if (client.getClientB()) {
        return "1";
    }
    return "2";
}

void QtContactsSwitcher::prepare(ClientTest &client) {
    // check if version of tracker is equal or greater than 0.9.26
    // set tracker databases according it's version
    FILE *fp;
    int version;
    char cmd[] = "tracker-control -V | awk 'NR==2 {print $2}' | awk '{split($0,A,\".\"); X=256*256*A[1]+256*A[2]+A[3];print X;}'";
    char buffer[10];
    fp = popen(cmd,"r");
    fgets(buffer,sizeof(buffer),fp);
    sscanf(buffer,"%d", &version);
    pclose(fp);
    if (version >= 2330) {
        QtContactsSwitcher::setDatabases(trackerdb_new);
    } else {
        QtContactsSwitcher::setDatabases(trackerdb_old);
    }

    int index = 0;
    for (int i = 0; i < sizeof(m_databases)/sizeof(m_databases[0]); i++) {
        if (m_databases[i].empty()) {
            index++;
            continue;
        }

        stringstream cmd;
        cmd << "rm -f " << getDatabasePath(index) << m_databases[i]
            << "_";
        execCmd(cmd.str() + "1", false);
        execCmd(cmd.str() + "2", false);
   }
   execCmd("tracker-control -r", false);
}

void QtContactsSwitcher::restoreStorage(ClientTest &client)
{
    // if CLIENT_TEST_BUTEO is not enabled, skip it for LocalTests may also use it
    if (!isButeo()) {
        return;
    }

    string id = getId(client);
    terminate();
    copyDatabases(client, false);
    start();
}

void QtContactsSwitcher::backupStorage(ClientTest &client)
{
    // if CLIENT_TEST_BUTEO is not enabled, skip it for LocalTests may also use it
    if (!isButeo()) {
        return;
    }

    string id = getId(client);
    terminate();
    copyDatabases(client);
    start();
}

void QtContactsSwitcher::setDatabases(const string databases[])
{
    for (int i = 0; i < sizeof(m_databases)/sizeof(m_databases[0]); i++) {
        m_databases[i] = databases[i];
    }
}
string QtContactsSwitcher::getDatabasePath(int index)
{
    string m_path = getHome() + m_dirs[index];
    return m_path;
}

void QtContactsSwitcher::copyDatabases(ClientTest &client, bool fromDefault)
{
    static string m_cmds[] = {"",
                              "",
                              "",
                              "",
                              "\"delete from deleteditems;\""};

    string id = getId(client);

    int index = 0;
    for (int i = 0; i < sizeof(m_databases)/sizeof(m_databases[0]); i++) {
        if (m_databases[i].empty()) {
            index++;
            continue;
        }
        string src = getDatabasePath(index) + m_databases[i];
        string dest = src + "_" + id;
        if (!fromDefault) {
            // in this case, we copy *_1/2.db to default db
            // if *_1/2.db doesn't exist, we copy default with initial commands
            if (access(dest.c_str(), F_OK) < 0) {
                if (access(src.c_str(), F_OK) >= 0) {
                    if (!m_cmds[i].empty()) {
                        stringstream temp;
                        temp << "sqlite3 " << src << " " << m_cmds[i];
                        execCmd(temp.str(), false);
                    }
                }
            } else {
                string tmp = src;
                src = dest;
                dest = tmp;
            }
        }
        stringstream cmd;
        cmd << "cp -f " << src << " " << dest;
        execCmd(cmd.str(), false);
    }
}

void QtContactsSwitcher::terminate()
{
    execCmd("tracker-control -t");
}

void QtContactsSwitcher::start()
{
    // sleep one second to let tracker daemon get prepared
    execCmd("tracker-control -s");
    execCmd("sleep 2");
}
