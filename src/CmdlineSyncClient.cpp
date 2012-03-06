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

#include <iostream>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

using namespace std;

CmdlineSyncClient::CmdlineSyncClient(const string &server,
                                     bool doLogging,
                                     bool useKeyring):
    SyncContext(server, doLogging),
    m_keyring(useKeyring)
{
    setUserInterface(this);
}

string CmdlineSyncClient::askPassword(const string &passwordName,
                                      const string &descr,
                                      const ConfigPasswordKey &key)
{
    string password;

    // try to use keyring, if allowed
    if (m_keyring &&
        GetLoadPasswordSignal()(passwordName, descr, key,  password)) {
        // succcess
        return password;
    }

    /**
     * if not built with secrets support or that support failed,
     * directly ask user to input password
     */
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
        password = buffer;
    } else {
        throwError(string("could not read password for ") + descr);
    }

    return password;
}

bool CmdlineSyncClient::savePassword(const string &passwordName,
                                     const string &password,
                                     const ConfigPasswordKey &key)
{
    // use keyring?
    if (m_keyring) {
        if (GetSavePasswordSignal()(passwordName, password, key)) {
            // saved!
            return true;
        }

        /* if no keyring support and it was requested, then raise an error */
        SyncContext::throwError("Cannot save " + passwordName + " as requested. "
                                "This SyncEvolution binary was compiled without support for storing "
                                "passwords in a keyring or wallet, or none of the backends providing that "
                                "functionality were usable. Either store passwords in your configuration "
                                "files or enter them interactively on each program run.\n");
    }

    // let config code store the password
    return false;
}

void CmdlineSyncClient::readStdin(string &content)
{
    if (!ReadFile(cin, content)) {
        throwError("stdin", errno);
    }
}

SE_END_CXX
