#include "DBusSyncClient.h"
#include "EvolutionSyncSource.h"


DBusSyncClient::DBusSyncClient(const string &server,
                               const set<string> &sources,
                               void (*progress) (const char *source,int type,int extra1,int extra2,int extra3,gpointer data),
                               void (*server_message) (const char *message,gpointer data),
                               char* (*need_password) (const char *message,gpointer data),
                               gpointer userdata) : 
	EvolutionSyncClient(server, true, sources),
	m_userdata (userdata),
	m_progress (progress),
	m_server_message (server_message),
	m_need_password (need_password)
{
}

DBusSyncClient::~DBusSyncClient()
{
}

string DBusSyncClient::askPassword(const string &descr)
{
	if (!m_need_password)
		return NULL;
	
	return string (m_need_password (descr.c_str(), m_userdata));
}

void DBusSyncClient::displayServerMessage(const string &message)
{
	m_server_message (message.c_str(), m_userdata);
}

void DBusSyncClient::displaySyncProgress(sysync::TProgressEventEnum type,
                                         int32_t extra1, int32_t extra2, int32_t extra3)
{
	m_progress (NULL, type, extra1, extra2, extra3, m_userdata);
}

void DBusSyncClient::displaySourceProgress(sysync::TProgressEventEnum type,
                                           EvolutionSyncSource &source,
                                           int32_t extra1, int32_t extra2, int32_t extra3)
{
	m_progress (g_strdup (source.getName()), type, extra1, extra2, extra3, m_userdata);
}
