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

/* This is an implementation of SyncContext
 * that is a DBus service. Internally it uses a 
 * SyncevoDBusServer GObject to handle the DBus side
 * of things.
 * */


#ifndef INCL_DBUSSYNCCLIENT
#define INCL_DBUSSYNCCLIENT

#include "config.h"

#include <synthesis/sync_declarations.h>
#include <syncevo/SyncContext.h>

#include <string>
#include <set>
#include <map>

using namespace SyncEvo;

class DBusSyncClient : public SyncContext {

public:
	DBusSyncClient(const string &server,
				   const map<string, int> &source_map,
				   void (*progress) (const char *source,int type,int extra1,int extra2,int extra3,gpointer data) = NULL,
				   void (*server_message) (const char *message,gpointer data) = NULL,
				   char* (*need_password) (const char *username, const char *server_url, gpointer data) = NULL,
				   gboolean (*check_for_suspend)(gpointer data) = NULL,
				   gpointer userdata = NULL);

	~DBusSyncClient();

protected:
	virtual void prepare(const std::vector<SyncSource *> &sources);

	virtual bool getPrintChanges() const;

	virtual string askPassword(const string &passwordName, const string &descr, const ConfigPasswordKey &key);

	virtual void displayServerMessage(const string &message);

	virtual void displaySyncProgress(sysync::TProgressEventEnum type,
	                                 int32_t extra1, int32_t extra2, int32_t extra3);

	virtual void displaySourceProgress(sysync::TProgressEventEnum type,
	                                   SyncSource &source,
	                                   int32_t extra1, int32_t extra2, int32_t extra3);

	virtual bool checkForSuspend();

    virtual int sleep (int intervals);

private:
	map<string, int> m_source_map;
	gpointer m_userdata;

	void (*m_progress) (const char *source,int type,int extra1,int extra2,int extra3,gpointer data);
	void (*m_server_message) (const char *message, gpointer data);
	char* (*m_need_password) (const char *username, const char *server_url, gpointer data);
	gboolean (*m_check_for_suspend) (gpointer data);

	static set<string> getSyncSources (const map<string, int> &source_map)
	{
		set<string> sources;
		map<string,int>::const_iterator iter;

		for (iter = source_map.begin (); iter != source_map.end (); iter++)
			sources.insert (iter->first);

		return sources;
	}
};

#endif
