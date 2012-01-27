/*
 * Copyright (C) 2011 Intel Corporation
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

#include "dbus-user-interface.h"

#ifdef USE_GNOME_KEYRING
extern "C" {
#include <gnome-keyring.h>
}
#endif

// redefining "signals" clashes with the use of that word in gtkbindings.h,
// included via notify.h
#define QT_NO_KEYWORDS

#ifdef USE_KDE_KWALLET
#include <QtCore/QCoreApplication>
#include <QtCore/QString>
#include <QtCore/QLatin1String>
#include <QtCore/QByteArray>
#include <QtCore/QDebug>
#include <QtDBus/QDBusConnection>

#include <KApplication>
#include <KAboutData>
#include <KCmdLineArgs>

#include <kwallet.h>
#endif

namespace {
    inline const char *passwdStr(const std::string &str)
    {
        return str.empty() ? NULL : str.c_str();
    }
}

SE_BEGIN_CXX

DBusUserInterface::DBusUserInterface(const std::string &config):
    SyncContext(config, true)
{
}

string DBusUserInterface::askPassword(const string &passwordName,
                                      const string &descr,
                                      const ConfigPasswordKey &key)
{
    string password;

#ifdef USE_KDE_KWALLET
    /** here we use server sync url without protocol prefix and
     * user account name as the key in the keyring */
     /* Also since the KWallet's API supports only storing (key,passowrd)
     * or Map<QString,QString> , the former is used */
    bool isKde=true;
    #ifdef USE_GNOME_KEYRING
    //When Both GNOME KEYRING and KWALLET are available, Check if this is a KDE Session
    //and Call
    if(!getenv("KDE_FULL_SESSION"))
      isKde=false;
    #endif
    if (isKde){
        QString walletPassword;
        QString walletKey = QString(passwdStr(key.user)) + ',' +
                            QString(passwdStr(key.domain))+ ','+
                            QString(passwdStr(key.server))+','+
                            QString(passwdStr(key.object))+','+
                            QString(passwdStr(key.protocol))+','+
                            QString(passwdStr(key.authtype))+','+
                            QString::number(key.port);


            QString wallet_name = KWallet::Wallet::NetworkWallet();
            //QString folder = QString::fromUtf8("Syncevolution");
            const QLatin1String folder("Syncevolution");

            if (!KWallet::Wallet::keyDoesNotExist(wallet_name, folder, walletKey)){
            KWallet::Wallet *wallet = KWallet::Wallet::openWallet(wallet_name, -1, KWallet::Wallet::Synchronous);

            if (wallet){
              if (wallet->setFolder(folder))
                if (wallet->readPassword(walletKey, walletPassword) == 0)
                  return walletPassword.toStdString();
                 }
          }

    }
#endif

#ifdef USE_GNOME_KEYRING
    /** here we use server sync url without protocol prefix and
     * user account name as the key in the keyring */
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

    /** if find password stored in gnome keyring */
    if(result == GNOME_KEYRING_RESULT_OK && list && list->data ) {
        GnomeKeyringNetworkPasswordData *key_data;
        key_data = (GnomeKeyringNetworkPasswordData*)list->data;
        password = key_data->password;
        gnome_keyring_network_password_list_free(list);
        return password;
    }
#endif


    //if not found, return empty
    return "";
}

bool DBusUserInterface::savePassword(const string &passwordName,
                                     const string &password,
                                     const ConfigPasswordKey &key)
{


#ifdef USE_KDE_KWALLET
        /* It is possible to let CmdlineSyncClient decide which of fields in ConfigPasswordKey it would use
         * but currently only use passed key instead */
    bool isKde=true;
    #ifdef USE_GNOME_KEYRING
    //When Both GNOME KEYRING and KWALLET are available, Check if this is a KDE Session
    //and Call
    if(!getenv("KDE_FULL_SESSION"))
      isKde=false;
    #endif
    if(isKde){
        // write password to keyring
        QString walletKey = QString(passwdStr(key.user)) + ',' +
                            QString(passwdStr(key.domain))+ ','+
                            QString(passwdStr(key.server))+','+
                            QString(passwdStr(key.object))+','+
                            QString(passwdStr(key.protocol))+','+
                            QString(passwdStr(key.authtype))+','+
                            QString::number(key.port);
        QString walletPassword = password.c_str();

         bool write_success = false;
         QString wallet_name = KWallet::Wallet::NetworkWallet();
         //QString folder = QString::fromUtf8("Syncevolution");
         const QLatin1String folder("Syncevolution");

         KWallet::Wallet *wallet = KWallet::Wallet::openWallet(wallet_name, -1,
                                            KWallet::Wallet::Synchronous);
          if (wallet){
            if (!wallet->hasFolder(folder))
              wallet->createFolder(folder);

            if (wallet->setFolder(folder))
              if (wallet->writePassword(walletKey, walletPassword) == 0)
                write_success = true;

        }

        if(!write_success) {
            SyncContext::throwError("Try to save " + passwordName + " in kde-wallet but got an error. ");
        }

    return write_success;
    }
#endif

#ifdef USE_GNOME_KEYRING
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
#else
#endif

    /** if no support of gnome-keyring, don't save anything */
    return false;
}

void DBusUserInterface::readStdin(string &content)
{
    throwError("reading stdin in D-Bus server not supported, use --daemon=no in command line");
}

SE_END_CXX
