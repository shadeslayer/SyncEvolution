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

#include "DBusSyncClient.h"
#include <syncevo/SyncSource.h>


DBusSyncClient::DBusSyncClient(const string &server,
                               const map<string, int> &source_map,
                               void (*progress) (const char *source,int type,int extra1,int extra2,int extra3,gpointer data),
                               void (*server_message) (const char *message,gpointer data),
                               char* (*need_password) (const char *username, const char *server_url, gpointer data),
                               gboolean (*check_for_suspend)(gpointer data),
                               gpointer userdata) : 
	SyncContext(server, true, getSyncSources (source_map)),
	m_source_map (source_map),
	m_userdata (userdata),
	m_progress (progress),
	m_server_message (server_message),
	m_need_password (need_password),
	m_check_for_suspend (check_for_suspend)
{
}

DBusSyncClient::~DBusSyncClient()
{
}

void DBusSyncClient::prepare(const std::vector<SyncSource *> &sources)
{
	SyncModes modes (SYNC_NONE);

	map<string,int>::const_iterator iter;
	for (iter = m_source_map.begin (); iter != m_source_map.end (); iter++) {
		modes.setSyncMode (iter->first, (SyncMode)iter->second);
	}
	setSyncModes (sources, modes);

}

bool DBusSyncClient::getPrintChanges() const
{
	return false;
}

string DBusSyncClient::askPassword(const string &passwordName, const string &descr, const ConfigPasswordKey &key)
{
	string retval;
	char *password = NULL;
	if (!m_need_password)
		throwError(string("Password query not supported"));

	password = m_need_password (getUsername (), getSyncURL(), m_userdata);
	if (password)
		retval = string (password);
	return retval;
}

void DBusSyncClient::displayServerMessage(const string &message)
{
	m_server_message (message.c_str(), m_userdata);
}

void DBusSyncClient::displaySyncProgress(sysync::TProgressEventEnum type,
                                         int32_t extra1, int32_t extra2, int32_t extra3)
{
	m_progress (NULL, type, extra1, extra2, extra3, m_userdata);
	SyncContext::displaySyncProgress(type, extra1, extra2, extra3);
}

void DBusSyncClient::displaySourceProgress(sysync::TProgressEventEnum type,
                                           SyncSource &source,
                                           int32_t extra1, int32_t extra2, int32_t extra3)
{
	m_progress (g_strdup (source.getName()), type, extra1, extra2,
                    // Synthesis engine doesn't count locally
                    // deleted items during
                    // refresh-from-server. That's a matter of
                    // taste. In SyncEvolution we'd like these
                    // items to show up, so add it here.
                    (type == sysync::PEV_DSSTATS_L &&
                     source.getFinalSyncMode() == SYNC_REFRESH_FROM_SERVER) ? 
                    source.getNumDeleted() :
                    extra3,
                    m_userdata);
	SyncContext::displaySourceProgress(type, source, extra1, extra2, extra3);
}

bool DBusSyncClient::checkForSuspend()
{
	return m_check_for_suspend (m_userdata);
}

int DBusSyncClient::sleep (int intervals)
{
    time_t start = time(NULL);
    while (true) {
        g_main_context_iteration(NULL, FALSE);
        time_t now = time(NULL);
        if (m_check_for_suspend(m_userdata)) {
            return  (intervals - now + start);
        } 
        if (intervals - now + start <=0) {
            return intervals - now +start;
        }
    }
}
