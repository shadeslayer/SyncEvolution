/*
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

#include "CmdlineSyncClient.h"
#ifdef USE_GNOME_KEYRING
extern "C" {
#include <gnome-keyring.h>
}
#endif

#ifdef USE_KDE_KWALLET
#include <QtCore/QCoreApplication>
#include <QtCore/QString>
#include <QtCore/QLatin1String>
#include <QtCore/QDebug>
#include <QtDBus/QDBusConnection>

#include <KApplication>
#include <KAboutData>
#include <KCmdLineArgs>

#include <kwallet.h>
#endif

#include <syncevo/declarations.h>
SE_BEGIN_CXX

using namespace std;

CmdlineSyncClient::CmdlineSyncClient(const string &server,
                                     bool doLogging,
                                     bool useKeyring):
    SyncContext(server, doLogging),
    m_keyring(useKeyring)
{
#ifdef USE_KDE_KWALLET
    //QCoreApplication *app;
    int argc = 1;
    static const char *prog = "syncevolution";
    static char *argv[] = { (char *)&prog, NULL };
    //if (!qApp) {
        //new QCoreApplication(argc, argv);
    //}
    KAboutData aboutData(// The program name used internally.
                         "syncevolution",
                         // The message catalog name
                         // If null, program name is used instead.
                         0,
                         // A displayable program name string.
                         ki18n("Syncevolution"),
                         // The program version string.
                         "1.0",
                         // Short description of what the app does.
                         ki18n("Lets Akonadi synchronize with a SyncML Peer"),
                         // The license this code is released under
                         KAboutData::License_GPL,
                         // Copyright Statement
                         ki18n("(c) 2010"),
                         // Optional text shown in the About box.
                         // Can contain any information desired.
                         ki18n(""),
                         // The program homepage string.
                         "http://www.syncevolution.org/",
                         // The bug report email address
                         "syncevolution@syncevolution.org");

    KCmdLineArgs::init(argc, argv, &aboutData);
    if (!kapp) {
        new KApplication;
        //To stop KApplication from spawning it's own DBus Service ... Will have to patch KApplication about this
        QDBusConnection::sessionBus().unregisterService("org.syncevolution.syncevolution-"+QString::number(getpid()));

    }
#endif
}
/**
 * GNOME keyring distinguishes between empty and unset
 * password keys. This function returns NULL for an
 * empty std::string.
 */
inline const char *passwdStr(const std::string &str)
{
    return str.empty() ? NULL : str.c_str();
}

string CmdlineSyncClient::askPassword(const string &passwordName,
                                      const string &descr,
                                      const ConfigPasswordKey &key)
{
    string password;

#ifdef USE_KDE_KWALLET
    /** here we use server sync url without protocol prefix and
     * user account name as the key in the keyring */
     /* Also since the KWallet's API supports only storing (key,passowrd)
     * or Map<QString,QString> , the former is used */
    bool isKde = true;

#ifdef USE_GNOME_KEYRING
    //When Both GNOME KEYRING and KWALLET are available, Check if this is a KDE Session
    //and Call KWallet if it is. else pick Gnome Keyring by default
    if (!getenv("KDE_FULL_SESSION")) {
        isKde = false;
    }
#endif

    if (isKde && m_keyring) {
        QString walletPassword;
        const QString walletKey = QString::fromStdString(key.user + ',' +
                key.domain + ',' + key.server + ',' + key.object + ',' +
                key.protocol + ',' + key.authtype + ',') + QString::number(key.port);

        const QString wallet_name = KWallet::Wallet::NetworkWallet();
        //QString folder = QString::fromUtf8("Syncevolution");
        const QLatin1String folder("Syncevolution");

        if (!KWallet::Wallet::keyDoesNotExist(wallet_name, folder, walletKey)){
            KWallet::Wallet *wallet = KWallet::Wallet::openWallet(wallet_name, -1, KWallet::Wallet::Synchronous);

        if (wallet && wallet->setFolder(folder) &&
            wallet->readPassword(walletKey, walletPassword) == 0) {
                return walletPassword.toStdString();
            }
        }
    }
#endif

#ifdef USE_GNOME_KEYRING
    /** here we use server sync url without protocol prefix and
     * user account name as the key in the keyring */
    if(m_keyring) {
        /* It is possible to let CmdlineSyncClient decide which of fields in ConfigPasswordKey it would use
         * but currently only use passed key instead */
        GnomeKeyringResult result;
        GList* list;

        result = gnome_keyring_find_network_password_sync(passwdStr(key.user),
                                                          passwdStr(key.domain),
                                                          passwdStr(key.server),
                                                          passwdStr(key.object),
                                                          passwdStr(key.protocol),
                                                          passwdStr(key.authtype),
                                                          key.port,
                                                          &list);

        // if find password stored in gnome keyring
        if(result == GNOME_KEYRING_RESULT_OK && list && list->data ) {
            GnomeKeyringNetworkPasswordData *key_data;
            key_data = (GnomeKeyringNetworkPasswordData*)list->data;
            password = key_data->password;
            gnome_keyring_network_password_list_free(list);
            return password;
        }
    }
    //if not found, then ask user to interactively input password
#endif

    /** if not built with secrets support, directly ask user to
     * input password */
    password = SyncContext::askPassword(passwordName, descr, key);
    return password;
}

bool CmdlineSyncClient::savePassword(const string &passwordName,
                                     const string &password,
                                     const ConfigPasswordKey &key)
{
#ifdef USE_KDE_KWALLET
    bool isKde = true;

#ifdef USE_GNOME_KEYRING
    // When both GNOME KEYRING and KWALLET are available, check if
    // this is a KDE Session and call
    if (getenv("KDE_FULL_SESSION")) {
        isKde = false;
    }
#endif

    if (m_keyring && isKde) {
        /* It is possible to let CmdlineSyncClient decide which of fields in ConfigPasswordKey it would use
         * but currently only use passed key instead */

        // write password to keyring
        const QString walletKey = QString::fromStdString(key.user + ',' +
                key.domain + ',' + key.server + ',' + key.object + ',' +
                key.protocol + ',' + key.authtype + ',')+ QString::number(key.port);
        const QString walletPassword = QString::fromStdString(password);

        bool write_success = false;
        const QString wallet_name = KWallet::Wallet::NetworkWallet();
        const QLatin1String folder("Syncevolution");
        KWallet::Wallet *wallet = KWallet::Wallet::openWallet(wallet_name, -1,
                KWallet::Wallet::Synchronous);
        if (wallet) {
            if (!wallet->hasFolder(folder)) {
              wallet->createFolder(folder);
            }

            if (wallet->setFolder(folder) &&
                wallet->writePassword(walletKey, walletPassword) == 0) {
                    write_success = true;
            }
        }

        if (!write_success) {
            SyncContext::throwError("Try to save " + passwordName + " in KWallet but got an error. ");
        }
        return write_success;
    }

    if (m_keyring) {
        SyncContext::throwError("Try to save " + passwordName +
                " in KWallet but get an error. "
                "This syncevolution binary was compiled without support for storing "
                "passwords in a Wallet. Either store passwords in your configuration "
                "files or enter them interactively on each program run.\n");
    }
#endif

#ifdef USE_GNOME_KEYRING
    if(m_keyring) {
        /* It is possible to let CmdlineSyncClient decide which of fields in ConfigPasswordKey it would use
         * but currently only use passed key instead */
        guint32 itemId;
        GnomeKeyringResult result;
        // write password to keyring
        result = gnome_keyring_set_network_password_sync(NULL,
                                                         passwdStr(key.user),
                                                         passwdStr(key.domain),
                                                         passwdStr(key.server),
                                                         passwdStr(key.object),
                                                         passwdStr(key.protocol),
                                                         passwdStr(key.authtype),
                                                         key.port,
                                                         password.c_str(),
                                                         &itemId);
        /* if set operation is failed */
        if(result != GNOME_KEYRING_RESULT_OK) {
#ifdef GNOME_KEYRING_220
            SyncContext::throwError("Try to save " + passwordName + " in gnome-keyring but get an error. " + gnome_keyring_result_to_message(result));
#else
            /** if gnome-keyring version is below 2.20, it doesn't support 'gnome_keyring_result_to_message'. */
            stringstream value;
            value << (int)result;
            SyncContext::throwError("Try to save " + passwordName + " in gnome-keyring but get an error. The gnome-keyring error code is " + value.str() + ".");
#endif
        } 
        return true;
    }
#endif

    /* if no keyring support, raise the error */
    if(m_keyring) {
        SyncContext::throwError("Try to save " + passwordName + " in gnome-keyring but get an error. " +
                "This syncevolution binary was compiled without support for storing "
                "passwords in a keyring. Either store passwords in your configuration "
                "files or enter them interactively on each program run.\n");
    }
    return false;
}

SE_END_CXX
