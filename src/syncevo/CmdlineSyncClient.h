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

#ifndef INCL_CMDLINESYNCCLIENT
#define INCL_CMDLINESYNCCLIENT

#include <syncevo/SyncContext.h>
#include <syncevo/Cmdline.h>

#include <syncevo/declarations.h>
SE_BEGIN_CXX

/**
 * a command line sync client for the purpose of
 * supporting a mechanism to save and retrieve password
 * in keyring.
 */
class CmdlineSyncClient : public SyncContext, private UserInterface {
 public:
    CmdlineSyncClient(const string &server,
                      bool doLogging = false);

    /**
     * These 2 functions are from UserInterface and implement it
     * to use keyring to retrieve and save password in the keyring,
     * if enabled.
     */
    virtual string askPassword(const string &passwordName, const string &descr, const ConfigPasswordKey &key);
    virtual bool savePassword(const string &passwordName, const string &password, const ConfigPasswordKey &key); 

    /** read from real stdin */
    virtual void readStdin(string &content);

 private:
    /**
     * special semantic of --daemon=no command line:
     * don't use keyring if option is unset or
     * explicitly false
     */
    bool useKeyring();
};

/**
 * This is a class derived from Cmdline. The purpose
 * is to implement the factory method 'createSyncClient' to create
 * new implemented 'CmdlineSyncClient' objects.
 */
class KeyringSyncCmdline : public Cmdline {
 public:
    KeyringSyncCmdline(int argc, const char * const * argv) :
        Cmdline(argc, argv)
    {}
    /**
     * create a user implemented sync client.
     */
    SyncContext* createSyncClient() {
        return new CmdlineSyncClient(m_server, true);
    }
};

SE_END_CXX
#endif // INCL_CMDLINESYNCCLIENT
