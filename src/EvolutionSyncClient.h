/*
 * Copyright (C) 2005 Patrick Ohly
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef INCL_EVOLUTIONSYNCCLIENT
#define INCL_EVOLUTIONSYNCCLIENT

#include <common/client/Sync4jClient.h>

#include <string>
using namespace std;

/*
 * This is the main class inside sync4jevolution which
 * looks at the configuration, activates all enabled
 * sources and executes the synchronization.
 *
 * Despite the name it is not a Sync4jClient, but rather
 * uses one.
 */
class EvolutionSyncClient {
    Sync4jClient& m_client;
    const string m_server;
    const string m_configPath;

  public:
    /**
     * @param server     identifies the server config to be used
     */
    EvolutionSyncClient(const string &server);
    ~EvolutionSyncClient();

    /**
     * executes the sync, throws an exception in case of failure
     * @param syncMode   setting this overrides the sync mode from the config
     */
    void sync(SyncMode syncMode = SYNC_NONE);
};

#endif // INCL_EVOLUTIONSYNCCLIENT
