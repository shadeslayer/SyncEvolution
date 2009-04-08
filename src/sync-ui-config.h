#ifndef SYNC_UI_CONFIG_H
#define SYNC_UI_CONFIG_H

#include <gtk/gtk.h>
#include <syncevo-dbus/syncevo-dbus.h>


typedef enum {
	SYNC_TYPE_TWO_WAY,
	SYNC_TYPE_ONE_WAY_FROM_REMOTE,
	SYNC_TYPE_ONE_WAY_FROM_LOCAL,
}SyncType;

typedef struct source_config {
	char *name;
	gboolean enabled;
	char *uri;
} source_config;

typedef struct server_config {
	char *name;
	char *base_url;
	char *web_url;
	char *icon_uri;

	char *username;
	char *password;

	GList *source_configs;
	
	gboolean changed;
	gboolean from_template;
} server_config;

void source_config_free (source_config *source);
void server_config_free (server_config *server);
void server_config_update_from_option (server_config *server, SyncevoOption *option);
void server_config_update_from_entry (server_config *server, GtkEntry *entry);
GPtrArray* server_config_get_option_array (server_config *server);
GPtrArray* server_config_get_source_array (server_config *server, SyncType mode);
source_config* server_config_get_source_config (server_config *server, const char *name);

#endif
