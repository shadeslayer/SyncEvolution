/*
 * Copyright (C) 2011 Dinesh <saidinesh5@gmail.com>
 * Copyright (C) 2012 Intel Corporation
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

#include <config.h>

#ifdef USE_KDE_KWALLET

#include "KDEPlatform.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QString>
#include <QtCore/QLatin1String>
#include <QtCore/QDebug>
#include <QtDBus/QDBusConnection>

#include <KApplication>
#include <KAboutData>
#include <KCmdLineArgs>

#include <kwallet.h>

#include <syncevo/SyncContext.h>
#include <syncevo/SyncConfig.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

void KDEInitMainSlot(const char *appname)
{
    //QCoreApplication *app;
    int argc = 1;
    static char *argv[] = { const_cast<char *>(appname), NULL };
    KAboutData aboutData(// The program name used internally.
                         "syncevolution",
                         // The message catalog name
                         // If null, program name is used instead.
                         0,
                         // A displayable program name string.
                         ki18n("SyncEvolution"),
                         // The program version string.
                         VERSION,
                         // Short description of what the app does.
                         ki18n("Lets Akonadi synchronize with a SyncML Peer"),
                         // The license this code is released under
                         KAboutData::License_GPL,
                         // Copyright Statement
                         ki18n("(c) 2010-12"),
                         // Optional text shown in the About box.
                         // Can contain any information desired.
                         ki18n(""),
                         // The program homepage string.
                         "http://www.syncevolution.org/",
                         // The bug report email address
                         "syncevolution@syncevolution.org");

    KCmdLineArgs::init(argc, argv, &aboutData);
    if (!kapp) {
        // Don't allow KApplication to mess with SIGINT/SIGTERM.
        // Restore current behavior after construction.
        struct sigaction oldsigint, oldsigterm;
        sigaction(SIGINT, NULL, &oldsigint);
        sigaction(SIGTERM, NULL, &oldsigterm);

        // Explicitly disable GUI mode in the KApplication.  Otherwise
        // the whole binary will fail to run when there is no X11
        // display.
        new KApplication(false);
        //To stop KApplication from spawning it's own DBus Service ... Will have to patch KApplication about this
        QDBusConnection::sessionBus().unregisterService("org.syncevolution.syncevolution-"+QString::number(getpid()));

        sigaction(SIGINT, &oldsigint, NULL);
        sigaction(SIGTERM, &oldsigterm, NULL);
    }
}

/**
 * Here we use server sync url without protocol prefix and
 * user account name as the key in the keyring.
 *
 * Also since the KWallet's API supports only storing (key,password)
 * or Map<QString,QString> , the former is used.
 */
bool KWalletLoadPasswordSlot(const std::string &passwordName,
                             const std::string &descr,
                             const ConfigPasswordKey &key,
                             std::string &password)
{
    // When both (presumably) GNOME keyring and KWallet are
    // available, check if this is a KDE Session before using
    // KWallet instead of GNOME keyring.
    if (GetLoadPasswordSignal().num_slots() > 1 &&
        !getenv("KDE_FULL_SESSION")) {
        return false;
    }

    QString walletPassword;
    QString walletKey = QString(key.user.c_str()) + ',' +
        QString(key.domain.c_str())+ ','+
        QString(key.server.c_str())+','+
        QString(key.object.c_str())+','+
        QString(key.protocol.c_str())+','+
        QString(key.authtype.c_str())+','+
        QString::number(key.port);

    QString wallet_name = KWallet::Wallet::NetworkWallet();
    //QString folder = QString::fromUtf8("Syncevolution");
    const QLatin1String folder("Syncevolution");

    password = "";
    if (!KWallet::Wallet::keyDoesNotExist(wallet_name, folder, walletKey)) {
        KWallet::Wallet *wallet = KWallet::Wallet::openWallet(wallet_name, -1, KWallet::Wallet::Synchronous);
        if (wallet &&
            wallet->setFolder(folder) &&
            wallet->readPassword(walletKey, walletPassword) == 0) {
            password = walletPassword.toStdString();
            return true;
        }
    }

    // not found, ask user
    return false;
}


bool KWalletSavePasswordSlot(const std::string &passwordName,
                             const std::string &password,
                             const ConfigPasswordKey &key)
{
    // see above
    if (GetLoadPasswordSignal().num_slots() > 1 &&
        !getenv("KDE_FULL_SESSION")) {
        return false;
    }

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
        SyncContext::throwError("Saving " + passwordName + " in KWallet failed.");
    }
    return write_success;
}

SE_END_CXX

#endif // USE_KDE_WALLET
