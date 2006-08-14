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

#include <config.h>
#include <common/client/Sync4jClient.h>

#include "EvolutionSmartPtr.h"

#include <string>
#include <set>
using namespace std;

class SourceList;

/*
 * This is the main class inside sync4jevolution which
 * looks at the configuration, activates all enabled
 * sources and executes the synchronization.
 *
 */
class EvolutionSyncClient : private SyncClient {
    const string m_server;
    const set<string> m_sources;
    const SyncMode m_syncMode;
    const bool m_doLogging;
    const string m_configPath;
    string m_url;

    /*
     * Variable shared by sync() and prepareSync():
     * stores active sync sources and handles reporting.
     * Not a smart pointer to avoid the need to make SourceList
     * public.
     */
    eptr<SourceList> m_sourceList;

  public:
    /**
     * @param server     identifies the server config to be used
     * @param syncMode   setting this overrides the sync mode from the config
     * @param doLogging  write additional log and datatbase files about the sync
     */
    EvolutionSyncClient(const string &server,
                        SyncMode syncMode = SYNC_NONE,
                        bool doLogging = false,
                        const set<string> &sources = set<string>());
    ~EvolutionSyncClient();

    /**
     * Executes the sync, throws an exception in case of failure.
     * Handles automatic backups and report generation.
     */
    int sync();

    /**************************************************/
    /************ override SyncClient interface *******/
    /**************************************************/

    const char *getClientID() const { return "SyncEvolution"; }
    const char *getClientVersion() const { return VERSION; }
    const char *getManufacturer() const { return "Patrick Ohly"; }
    const char *getClientType() const { return "workstation"; }
    int isUTC() const { return true; }

  protected:
    /* Sync4jClient callbacks */
    int prepareSync(const AccessConfig &config,
                    ManagementNode &node);
    int createSyncSource(const char *name,
                         const SyncSourceConfig &config,
                         ManagementNode &node,
                         SyncSource **source);
    int beginSync();
};

#endif // INCL_EVOLUTIONSYNCCLIENT
