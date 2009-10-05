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

#include <syncevo/declarations.h>
SE_BEGIN_CXX

using namespace std;

CmdlineSyncClient::CmdlineSyncClient(const string &server,
                                     bool doLogging,
                                     const set<string> &sources,
                                     bool useKeyring):
    SyncContext(server, doLogging, sources),
    m_keyring(useKeyring)
{
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

        /** if find password stored in gnome keyring */
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
    /** if not built with gnome_keyring support, directly ask user to
     * input password */
    password = SyncContext::askPassword(passwordName, descr, key);
    return password;
}

bool CmdlineSyncClient::savePassword(const string &passwordName, 
                                     const string &password, 
                                     const ConfigPasswordKey &key)
{
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
#else
    /* if no keyring support, raise the error */
    if(m_keyring) {
        SyncContext::throwError("Try to save " + passwordName + " in gnome-keyring but get an error. " +
                "This syncevolution binary was compiled without support for storing "
                "passwords in a keyring. Either store passwords in your configuration "
                "files or enter them interactively on each program run.\n");
    }
#endif
    return false;
}

SE_END_CXX
