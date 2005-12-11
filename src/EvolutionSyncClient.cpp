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

#include "EvolutionSyncClient.h"
#include "EvolutionSyncSource.h"

#include <spdm/DMTree.h>

#include <list>
#include <memory>
#include <vector>
using namespace std;

EvolutionSyncClient::EvolutionSyncClient(const string &server) :
    m_client(Sync4jClient::getInstance()),
    m_server(server),
    m_configPath(string("evolution/") + server)
{
    m_client.setDMConfig(m_configPath.c_str());
}

EvolutionSyncClient::~EvolutionSyncClient()
{
    Sync4jClient::dispose();
}

void EvolutionSyncClient::sync( SyncMode syncMode )
{
    class sourcelist : public list<EvolutionSyncSource *> {
    public:
        ~sourcelist() {
            for( iterator it = begin();
                 it != end();
                 ++it ) {
                delete *it;
            }
        }
    } sources;
    DMTree config(m_configPath.c_str());

    // find server URL (part of change id)
    string serverPath = m_configPath + "/spds/syncml";
    auto_ptr<ManagementNode> serverNode(config.getManagementNode(serverPath.c_str()));
    string url = EvolutionSyncSource::getPropertyValue(*serverNode, "syncURL");

    // find sources
    string sourcesPath = m_configPath + "/spds/sources";
    auto_ptr<ManagementNode> sourcesNode(config.getManagementNode(sourcesPath.c_str()));
    int index, numSources = sourcesNode->getChildrenMaxCount();
    char **sourceNamesPtr = sourcesNode->getChildrenNames();

    // copy source names into format that will be
    // freed in case of exception
    vector<string> sourceNames;
    for ( index = 0; index < numSources; index++ ) {
        sourceNames.push_back(sourceNamesPtr[index]);
        delete [] sourceNamesPtr[index];
    }
    delete [] sourceNamesPtr;
    
    // iterate over sources
    for ( index = 0; index < numSources; index++ ) {
        // is the source enabled?
        string sourcePath(sourcesPath + "/" + sourceNames[index]);
        auto_ptr<ManagementNode> sourceNode(config.getManagementNode(sourcePath.c_str()));
        string disabled = EvolutionSyncSource::getPropertyValue(*sourceNode, "disabled");
        if (disabled != "T" && disabled != "t") {
            // create it
            string type = EvolutionSyncSource::getPropertyValue(*sourceNode, "type");
            EvolutionSyncSource *syncSource =
                EvolutionSyncSource::createSource(
                    sourceNames[index],
                    string("sync4jevolution:") + url + "/" + EvolutionSyncSource::getPropertyValue(*sourceNode, "name"),
                    EvolutionSyncSource::getPropertyValue(*sourceNode, "evolutionsource"),
                    type
                    );
            if (!syncSource) {
                throw sourceNames[index] + ": type " +
                    ( type.size() ? string("not configured") :
                      string("'") + type + "' empty or unknown" );
            }
            syncSource->setPreferredSyncMode( syncMode );
            sources.push_back(syncSource);

            // also open it; failing now is still safe
            syncSource->open();
        }
    }

    if (!sources.size()) {
        LOG.info( "no sources configured, done" );
        return;
    }

    // build array as sync wants it, then sync
    // (no exceptions allowed here)
    SyncSource **sourceArray = new SyncSource *[sources.size() + 1];
    index = 0;
    for ( list<EvolutionSyncSource *>::iterator it = sources.begin();
          it != sources.end();
          ++it ) {
        sourceArray[index] = *it;
        ++index;
    }
    sourceArray[index] = NULL;
    int res = m_client.sync( sourceArray );
    delete [] sourceArray;

    // TODO: force slow sync in case of failure or failed Evolution source
    
    if (res) {
        if (lastErrorCode) {
            throw lastErrorCode;
        }
        // no error code?!
        lastErrorCode = res;
        if (!lastErrorMsg[0]) {
            strcpy(lastErrorMsg, "sync() failed without setting an error description");
        }
        throw res;
    }
}
