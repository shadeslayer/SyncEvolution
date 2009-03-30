#include "DBusSyncClient.h"
#include "EvolutionSyncSource.h"


DBusSyncClient::DBusSyncClient(const string &server,
                               const set<string> &sources,
                               void (*progress) (int type,int extra1,int extra2,int extra3,gpointer data),
                               void (*source_progress) (const char *source,int type,int extra1,int extra2,int extra3,gpointer data),
                               void (*server_message) (const char *message,gpointer data),
                               char* (*need_password) (const char *message,gpointer data),
                               gpointer userdata) : 
	EvolutionSyncClient(server, false, sources),
	m_userdata (userdata)
{
}

DBusSyncClient::~DBusSyncClient()
{
}

string DBusSyncClient::askPassword(const string &descr)
{
	if (!need_password)
		return NULL;
	
	return string (need_password (descr.c_str(), m_userdata));
}

void DBusSyncClient::displayServerMessage(const string &message)
{
	server_message (message.c_str(), m_userdata);
}

void DBusSyncClient::displaySyncProgress(sysync::TProgressEventEnum type,
                                         int32_t extra1, int32_t extra2, int32_t extra3)
{
	progress (type, extra1, extra2, extra3, m_userdata);
}

void DBusSyncClient::displaySourceProgress(sysync::TProgressEventEnum type,
                                           EvolutionSyncSource &source,
                                           int32_t extra1, int32_t extra2, int32_t extra3)
{
	source_progress (source.getName(), type, extra1, extra2, extra3, m_userdata);
}
