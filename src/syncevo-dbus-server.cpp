
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>


#include <dbus/dbus-glib-bindings.h>

#include "syncevo-dbus-server.h"
#include "syncevo-marshal.h"

static gboolean syncevo_start_sync (SyncevoDBusServer *obj, char *server, GPtrArray *sources, GError **error);
static gboolean syncevo_abort_sync (SyncevoDBusServer *obj, char *server, GError **error);
static gboolean syncevo_set_password (SyncevoDBusServer *obj, char *server, char *password, GError **error);
static gboolean syncevo_get_templates (SyncevoDBusServer *obj, GPtrArray **templates, GError **error);
static gboolean syncevo_get_template_config (SyncevoDBusServer *obj, char *templ, GPtrArray **options, GError **error);
static gboolean syncevo_get_servers (SyncevoDBusServer *obj, GPtrArray **servers, GError **error);
static gboolean syncevo_get_server_config (SyncevoDBusServer *obj, char *server, GPtrArray **options, GError **error);
static gboolean syncevo_set_server_config (SyncevoDBusServer *obj, char *server, GPtrArray *options, GError **error);
#include "syncevo-dbus-glue.h"

#define SYNCEVO_SOURCE_TYPE (dbus_g_type_get_struct ("GValueArray", G_TYPE_STRING, G_TYPE_INT, G_TYPE_INVALID))
typedef GValueArray SyncevoSource;

#define SYNCEVO_OPTION_TYPE (dbus_g_type_get_struct ("GValueArray", G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID))
typedef GValueArray SyncevoOption;

#define SYNCEVO_SERVER_TYPE (dbus_g_type_get_struct ("GValueArray", G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID))
typedef GValueArray SyncevoServer;

SyncevoSource*
syncevo_source_new (char *name, int mode)
{
	GValue val = {0, };

	g_value_init (&val, SYNCEVO_SOURCE_TYPE);
	g_value_take_boxed (&val, dbus_g_type_specialized_construct (SYNCEVO_SOURCE_TYPE));
	dbus_g_type_struct_set (&val, 0, name, 1, mode, G_MAXUINT);

	return (SyncevoSource*) g_value_get_boxed (&val);
}

void
syncevo_source_get (SyncevoSource *source, const char **name, int *mode)
{
	if (name) {
		*name = g_value_get_string (g_value_array_get_nth (source, 0));
	}
	if (mode) {
		*mode = g_value_get_int (g_value_array_get_nth (source, 1));
	}
}

static void
syncevo_source_add_to_set (SyncevoSource *source, set<string> source_set)
{
	const char *str;
	
	syncevo_source_get (source, &str, NULL);
	source_set.insert (g_strdup (str));
}

void
syncevo_source_free (SyncevoSource *source)
{
	if (source) {
		g_boxed_free (SYNCEVO_SOURCE_TYPE, source);
	}
}

SyncevoOption*
syncevo_option_new (char *ns, char *key, char *value)
{
	GValue val = {0, };

	g_value_init (&val, SYNCEVO_OPTION_TYPE);
	g_value_take_boxed (&val, dbus_g_type_specialized_construct (SYNCEVO_OPTION_TYPE));
	dbus_g_type_struct_set (&val, 0, ns, 1, key, 2, value, G_MAXUINT);

	return (SyncevoOption*) g_value_get_boxed (&val);
}

void
syncevo_option_get (SyncevoOption *option, const char **ns, const char **key, const char **value)
{
	if (ns) {
		*ns = g_value_get_string (g_value_array_get_nth (option, 0));
	}
	if (key) {
		*key = g_value_get_string (g_value_array_get_nth (option, 1));
	}
	if (value) {
		*value = g_value_get_string (g_value_array_get_nth (option, 2));
	}
}

void
syncevo_option_free (SyncevoOption *option)
{
	if (option) {
		g_boxed_free (SYNCEVO_OPTION_TYPE, option);
	}
}

SyncevoServer* syncevo_server_new (char *name, char *note)
{
	GValue val = {0, };

	g_value_init (&val, SYNCEVO_SERVER_TYPE);
	g_value_take_boxed (&val, dbus_g_type_specialized_construct (SYNCEVO_SERVER_TYPE));
	dbus_g_type_struct_set (&val, 0, name, 1, note, G_MAXUINT);

	return (SyncevoServer*) g_value_get_boxed (&val);
}

void syncevo_server_get (SyncevoServer *temp, const char **name, const char **note)
{
	if (name) {
		*name = g_value_get_string (g_value_array_get_nth (temp, 0));
	}
	if (note) {
		*note = g_value_get_string (g_value_array_get_nth (temp, 1));
	}
}

void syncevo_server_free (SyncevoServer *temp)
{
	if (temp) {
		g_boxed_free (SYNCEVO_SERVER_TYPE, temp);
	}
}

enum {
	PROGRESS,
	SERVER_MESSAGE,
	NEED_PASSWORD,
	LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = {0};

G_DEFINE_TYPE (SyncevoDBusServer, syncevo_dbus_server, G_TYPE_OBJECT);

void
emit_progress (const char *source,
                      int type,
                      int extra1,
                      int extra2,
                      int extra3,
                      gpointer data)
{
	SyncevoDBusServer *obj = (SyncevoDBusServer *)data;

	g_signal_emit (obj, signals[PROGRESS], 0,
	               obj->server,
	               source,
	               type,
	               extra1,
	               extra2,
	               extra3);
}

void
emit_server_message (const char *message,
                     gpointer data)
{
	SyncevoDBusServer *obj = (SyncevoDBusServer *)data;

	g_signal_emit (obj, signals[SERVER_MESSAGE], 0,
	               obj->server,
	               message);
}

char*
need_password (const char *message,
               gpointer data)
{
	SyncevoDBusServer *obj = (SyncevoDBusServer *)data;

	g_signal_emit (obj, signals[NEED_PASSWORD], 0);

	/* TODO */
	return NULL;
}

gboolean 
check_for_suspend (gpointer data)
{
	SyncevoDBusServer *obj = (SyncevoDBusServer *)data;

	return obj->aborted;
}

static gboolean 
do_sync (SyncevoDBusServer *obj)
{
	int ret;

	SyncReport report;

	ret = (*obj->client).sync(&report);
	if (ret != 0) {
		g_printerr ("SyncEvolution returned error %d\n", ret);
	}

	/* adding a progress signal on top of synthesis ones */
	g_signal_emit (obj, signals[PROGRESS], 0,
	               obj->server,
	               NULL,
	               -1,
	               ret,
	               0,
	               0);

	delete obj->client;
	g_free (obj->server);
	obj->server = NULL;
	obj->sources = NULL;

	return FALSE;
}

static gboolean  
syncevo_start_sync (SyncevoDBusServer *obj, 
                    char *server,
                    GPtrArray *sources,
                    GError **error)
{
	if (obj->server) {
		*error = g_error_new (g_quark_from_static_string ("syncevo-dbus-server"),
		                      1, "Sync already in progress. Concurrent syncs are currently not supported");
		return FALSE;
	}
	if (!server) {
		*error = g_error_new (g_quark_from_static_string ("syncevo-dbus-server"),
		                      1, "Server variable must be set");
		return FALSE;
	}

	obj->aborted = FALSE;
	obj->server = g_strdup (server);

	set<string> source_set = set<string>();
	g_ptr_array_foreach (sources, (GFunc)syncevo_source_add_to_set, &source_set);
	obj->client = new DBusSyncClient (string (server), source_set, 
	                                  emit_progress, emit_server_message, need_password, check_for_suspend,
	                                  obj);

	g_idle_add ((GSourceFunc)do_sync, obj); 

	return TRUE;
}

static gboolean 
syncevo_abort_sync (SyncevoDBusServer *obj,
                            char *server,
                            GError **error)
{
	if (!server) {
		*error = g_error_new (g_quark_from_static_string ("syncevo-dbus-server"),
		                      1, "Server variable must be set");
		return FALSE;
	}

	if ((!obj->server) || strcmp (server, obj->server) != 0) {
		*error = g_error_new (g_quark_from_static_string ("syncevo-dbus-server"),
		                      1, "Not syncing server '%s'", server);
		return FALSE;
	}

	obj->aborted = TRUE;

	return TRUE;
}

static gboolean 
syncevo_set_password (SyncevoDBusServer *obj,
                              char *server,
                              char *password,
                              GError **error)
{
	*error = g_error_new (g_quark_from_static_string ("syncevo-dbus-server"),
	                      1, "SetPassword not supported yet");

	return FALSE;
	
}

static gboolean 
syncevo_get_servers (SyncevoDBusServer *obj,
                     GPtrArray **servers,
                     GError **error)
{
	if (!servers) {
		return FALSE;
	}

	*servers = g_ptr_array_new ();

	EvolutionSyncConfig::ServerList list = EvolutionSyncConfig::getServers();

	BOOST_FOREACH(const EvolutionSyncConfig::ServerList::value_type &server,list) {
		char *name, *note;
		SyncevoServer *temp;

		name = g_strdup (server.first.c_str());
		note = g_strdup (server.second.c_str());
		temp = syncevo_server_new (name, note);
		g_ptr_array_add (*servers, temp);
	}
	
	return TRUE;
}

static gboolean 
syncevo_get_templates (SyncevoDBusServer *obj,
                       GPtrArray **templates,
                       GError **error)
{
	if (!templates) {
		return FALSE;
	}

	*templates = g_ptr_array_new ();

	EvolutionSyncConfig::ServerList list = EvolutionSyncConfig::getServerTemplates();

	BOOST_FOREACH(const EvolutionSyncConfig::ServerList::value_type &server,list) {
		char *name, *note;
		SyncevoServer *temp;

		name = g_strdup (server.first.c_str());
		note = g_strdup (server.second.c_str());
		temp = syncevo_server_new (name, note);
		g_ptr_array_add (*templates, temp);
	}
	
	return TRUE;
}

static gboolean 
syncevo_get_template_config (SyncevoDBusServer *obj, 
                             char *templ, 
                             GPtrArray **options, 
                             GError **error)
{
	SyncevoOption *option;

	if (!templ || !options) {
		*error = g_error_new (g_quark_from_static_string ("syncevo-dbus-server"),
		                      1, "Template and options parameters must be given");
		return FALSE;
	}

	if (obj->server) {
		*error = g_error_new (g_quark_from_static_string ("syncevo-dbus-server"),
		                      1, "GetTemplateConfig is currently not supported when a sync is in progress");
		return FALSE;
	}
	
	*options = g_ptr_array_new ();

	boost::shared_ptr<EvolutionSyncConfig> config (EvolutionSyncConfig::createServerTemplate (string (templ)));
	if (!config.get()) {
		*error = g_error_new (g_quark_from_static_string ("syncevo-dbus-server"),
		                      1, "No template '%s' found", templ);
		return FALSE;
	}
	option = syncevo_option_new (NULL, g_strdup ("syncURL"), g_strdup(config->getSyncURL()));
	g_ptr_array_add (*options, option);
	option = syncevo_option_new (NULL, g_strdup("username"), g_strdup(config->getUsername()));
	g_ptr_array_add (*options, option);
	option = syncevo_option_new (NULL, g_strdup("password"), g_strdup(config->getPassword()));
	g_ptr_array_add (*options, option);

	list<string> sources = config->getSyncSources();
	BOOST_FOREACH(const string &name, sources) {
		boost::shared_ptr<EvolutionSyncSourceConfig> source_config = config->getSyncSourceConfig(name);

		option = syncevo_option_new (g_strdup (name.c_str()), g_strdup ("sync"), g_strdup (source_config->getSync()));
		g_ptr_array_add (*options, option);
		option = syncevo_option_new (g_strdup (name.c_str()), g_strdup ("uri"), g_strdup (source_config->getURI()));
		g_ptr_array_add (*options, option);

	}

	return TRUE;
}

static gboolean 
syncevo_get_server_config (SyncevoDBusServer *obj,
                           char *server,
                           GPtrArray **options,
                           GError **error)
{
	SyncevoOption *option;

	if (!server || !options) {
		*error = g_error_new (g_quark_from_static_string ("syncevo-dbus-server"),
		                      1, "Server and options parameters must be given");
		return FALSE;
	}

	if (obj->server) {
		*error = g_error_new (g_quark_from_static_string ("syncevo-dbus-server"),
		                      1, "GetServerConfig is currently not supported when a sync is in progress");
		return FALSE;
	}

	*options = g_ptr_array_new ();

	boost::shared_ptr<EvolutionSyncConfig> from(new EvolutionSyncConfig (string (server)));
	/* if config does not exist, create from template */
	if (!from->exists()) {
		from = EvolutionSyncConfig::createServerTemplate( string (server));
		if (!from.get()) {
			*error = g_error_new (g_quark_from_static_string ("syncevo-dbus-server"),
			                      1, "No server or template '%s' found", server);
			return FALSE;
		}
	}
	boost::shared_ptr<EvolutionSyncConfig> config(new EvolutionSyncConfig(string (server)));
	config->copy(*from, NULL);

	option = syncevo_option_new (NULL, g_strdup ("syncURL"), g_strdup(config->getSyncURL()));
	g_ptr_array_add (*options, option);
	option = syncevo_option_new (NULL, g_strdup("username"), g_strdup(config->getUsername()));
	g_ptr_array_add (*options, option);
	option = syncevo_option_new (NULL, g_strdup("password"), g_strdup(config->getPassword()));
	g_ptr_array_add (*options, option);

	list<string> sources = config->getSyncSources();
	BOOST_FOREACH(const string &name, sources) {
		boost::shared_ptr<EvolutionSyncSourceConfig> source_config = config->getSyncSourceConfig(name);

		option = syncevo_option_new (g_strdup (name.c_str()), g_strdup ("sync"), g_strdup (source_config->getSync()));
		g_ptr_array_add (*options, option);
		option = syncevo_option_new (g_strdup (name.c_str()), g_strdup ("uri"), g_strdup (source_config->getURI()));
		g_ptr_array_add (*options, option);

	}

	return TRUE;
}


static gboolean 
syncevo_set_server_config (SyncevoDBusServer *obj,
                                   char *server,
                                   GPtrArray *options,
                                   GError **error)
{
	int i;
	
	if (!server || !options) {
		*error = g_error_new (g_quark_from_static_string ("syncevo-dbus-server"),
		                      1, "Server and options parameters must be given");
		return FALSE;
	}

	if (obj->server) {
		*error = g_error_new (g_quark_from_static_string ("syncevo-dbus-server"),
		                      1, "GetServers is currently not supported when a sync is in progress");
		return FALSE;
	}

	boost::shared_ptr<EvolutionSyncConfig> from(new EvolutionSyncConfig (string (server)));
	/* if config does not exist, create from template */
	if (!from->exists()) {
		from = EvolutionSyncConfig::createServerTemplate( string (server));
		if (!from.get()) {
			*error = g_error_new (g_quark_from_static_string ("syncevo-dbus-server"),
			                      1, "No server or template '%s' found", server);
			return FALSE;
		}
	}
	boost::shared_ptr<EvolutionSyncConfig> config(new EvolutionSyncConfig(string (server)));
	config->copy(*from, NULL);
	
	for (i = 0; i < (int)options->len; i++) {
		const char *ns, *key, *value;
		SyncevoOption *option = (SyncevoOption*)g_ptr_array_index (options, i);

		syncevo_option_get (option, &ns, &key, &value);

		if ((!ns || strlen (ns) == 0) && key) {
			if (strcmp (key, "syncURL") == 0) {
				config->setSyncURL (string (value));
			} else if (strcmp (key, "username") == 0) {
				config->setUsername (string (value));
			} else if (strcmp (key, "password") == 0) {
				config->setPassword (string (value));
			}
		} else if (ns && key) {
			boost::shared_ptr<EvolutionSyncSourceConfig> source_config = config->getSyncSourceConfig(ns);
			if (strcmp (key, "sync") == 0) {
				source_config->setSync (string (value));
			} else if (strcmp (key, "uri") == 0) {
				source_config->setURI (string (value));
			}
		}
	}
	config->flush();

	return TRUE;
}

static void
syncevo_dbus_server_class_init(SyncevoDBusServerClass *klass)
{
	GError *error = NULL;

	klass->connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
	if (klass->connection == NULL)
	{
		g_warning("Unable to connect to dbus: %s", error->message);
		g_error_free (error);
		return;
	}

	signals[PROGRESS] = g_signal_new ("progress",
	                                  G_TYPE_FROM_CLASS (klass),
	                                  (GSignalFlags)(G_SIGNAL_RUN_FIRST | G_SIGNAL_NO_RECURSE),
	                                  G_STRUCT_OFFSET (SyncevoDBusServerClass, progress),
	                                  NULL, NULL,
	                                  syncevo_marshal_VOID__STRING_STRING_INT_INT_INT_INT,
	                                  G_TYPE_NONE, 
	                                  6, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT);
	signals[SERVER_MESSAGE] = g_signal_new ("server-message",
	                                  G_TYPE_FROM_CLASS (klass),
	                                  (GSignalFlags)(G_SIGNAL_RUN_FIRST | G_SIGNAL_NO_RECURSE),
	                                  G_STRUCT_OFFSET (SyncevoDBusServerClass, server_message),
	                                  NULL, NULL,
	                                  syncevo_marshal_VOID__STRING_STRING,
	                                  G_TYPE_NONE, 
	                                  2, G_TYPE_STRING, G_TYPE_STRING);
	signals[NEED_PASSWORD] = g_signal_new ("need-password",
	                                  G_TYPE_FROM_CLASS (klass),
	                                  (GSignalFlags)(G_SIGNAL_RUN_FIRST | G_SIGNAL_NO_RECURSE),
	                                  G_STRUCT_OFFSET (SyncevoDBusServerClass, need_password),
	                                  NULL, NULL,
	                                  g_cclosure_marshal_VOID__VOID,
	                                  G_TYPE_NONE, 
	                                  0);

	/* dbus_glib_syncevo_object_info is provided in the generated glue file */
	dbus_g_object_type_install_info (SYNCEVO_TYPE_DBUS_SERVER, &dbus_glib_syncevo_object_info);
}

static void
syncevo_dbus_server_init(SyncevoDBusServer *obj)
{
	GError *error = NULL;
	DBusGProxy *proxy;
	guint request_ret;
	SyncevoDBusServerClass *klass = SYNCEVO_DBUS_SERVER_GET_CLASS (obj);

	dbus_g_connection_register_g_object (klass->connection,
	                                     "/org/Moblin/SyncEvolution",
	                                     G_OBJECT (obj));

	proxy = dbus_g_proxy_new_for_name (klass->connection,
	                                   DBUS_SERVICE_DBUS,
	                                   DBUS_PATH_DBUS,
	                                   DBUS_INTERFACE_DBUS);

	if(!org_freedesktop_DBus_request_name (proxy,
	                                       "org.Moblin.SyncEvolution",
	                                       0, &request_ret,
	                                       &error)) {
		g_warning("Unable to register service: %s", error->message);
		g_error_free (error);
	}
	g_object_unref (proxy);

}

GMainLoop *loop;

void niam(int sig)
{
	g_main_loop_quit (loop);
}

int main()
{
	SyncevoDBusServer *server;

	signal(SIGTERM, niam);
	signal(SIGINT, niam);

	g_type_init ();

	server = (SyncevoDBusServer*)g_object_new (SYNCEVO_TYPE_DBUS_SERVER, NULL);

	loop = g_main_loop_new (NULL, FALSE);
	g_main_loop_run (loop);

	g_main_loop_unref (loop);
	g_object_unref (server);
	return 0;
}
