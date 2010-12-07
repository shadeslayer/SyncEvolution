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
#ifndef INCL_SYNC_BUTEOTEST
#define INCL_SYNC_BUTEOTEST

#include <syncevo/util.h>
#include <syncevo/declarations.h>
#include <QString>
#include <QObject>
#include <QtDBus>
#include <QDomDocument>
#include "ClientTest.h"

using namespace SyncEvo;

/**
 * ButeoTest is used to invoke buteo to do client test with the help
 * of client test framework. The basic idea is to implement doSync and
 * replace with invocation of buteo's dbus server - 'msyncd'.
 * The main steps are:
 * 1) pre-run: This may include set up sync options for local client
 * and target server, prepare local databases
 * 2) run: run sync by sending dbus calls to 'msyncd' and wait until
 * it finishes
 * 3) post-run: collect sync result and statistics
 */
class ButeoTest : public QObject {
    Q_OBJECT
public:
    ButeoTest(ClientTest &client,
              const string &server,
              const string &logbase,
              const SyncEvo::SyncOptions &options); 

    // prepare sync sources
    void prepareSources(const int *sources, const vector<string> &source2Config);

    // do actually sync
    SyncEvo::SyncMLStatus doSync(SyncEvo::SyncReport *report); 

private slots:
    void syncStatus(QString, int, QString, int);
    void resultsAvailable(QString, QString);
    void serviceUnregistered(QString);

private:

    /** initialize
     */
    static void init();

    /**
     * 1. set deviceid, max-message-size options to /etc/sync/meego-sync-conf.xml
     * 2. set wbxml option, sync mode, enabled selected sources and disable other sources 
     */
    void setupOptions();

    // kill all msyncd  
    void killAllMsyncd();

    // start msyncd  
    int startMsyncd();

    // do actually running 
    bool run();

    // get sync results from buteo and set them to sync report
    void genSyncResults(const QString &text, SyncEvo::SyncReport *report);

    // whether configured sources include contacts
    bool inclContacts();

    // truncate file and write content to file
    static void writeToFile(const QString &filePath, const QString &content );

    //replace the element value with a new value
    static void replaceElement(QString &xml, const QString &elem, const QString &value);

    // build a dom tree from file
    static void buildDomFromFile(QDomDocument &doc, const QString &filePath);

    ClientTest &m_client;
    string m_server;
    string m_logbase;
    SyncEvo::SyncOptions m_options;
    std::set<string> m_configedSources;
    QString m_syncResults;

    //device ids
    static QString m_deviceIds[2];
    //mappings for syncevolution source and buteo storage
    static map<string, string> m_source2storage;
    //flag for initialization
    static bool m_inited;
};


/**
 * Qtcontacts use tracker to store data. However, it can't specify
 * the place where to store them. Since we have to separate client A
 * and B's data, restore and backup their databases
 */
class QtContactsSwitcher {
public:
    /** do preparation */
    static void prepare(ClientTest &client);

    static string getId(ClientTest &client);

    /**
     * prepare storage:
     * 1. terminate tracker
     * 2. copy tracker databases from backup to its default place
     * according to id
     * 3. restart tracker 
     */
    static void restoreStorage(ClientTest &client);

    /**
     * backup storage:
     * 1. terminate tracker
     * 2. copy tracker databases from default place to backup
     * 3. restart tracker
     */
    static void backupStorage(ClientTest &client);
private:
    // get the file path of databases
    static std::string getDatabasePath(int index = 0);

    //terminate tracker daemons
    static void terminate();

    //start tracker daemons
    static void start();

    //copy databases between default place and backup place
    static void copyDatabases(ClientTest &client, bool fromDefault = true);

    //databases used by tracker
    static std::string m_databases[];
    static std::string m_dirs[];
};
#endif
