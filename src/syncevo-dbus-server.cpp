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


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#include <dbus/dbus-glib-bindings.h>

#ifdef USE_GNOME_KEYRING
// extern "C" is missing in 2.24.1, leading to
// link errors. Specifying it here is a workaround.
extern "C" {
#include <gnome-keyring.h>
}
#endif

#include "EvolutionSyncSource.h"
#include "syncevo-dbus-server.h"
#include "syncevo-marshal.h"

static gboolean syncevo_start_sync (SyncevoDBusServer *obj, char *server, GPtrArray *sources, GError **error);
static gboolean syncevo_abort_sync (SyncevoDBusServer *obj, char *server, GError **error);
static gboolean syncevo_get_templates (SyncevoDBusServer *obj, GPtrArray **templates, GError **error);
static gboolean syncevo_get_template_config (SyncevoDBusServer *obj, char *templ, GPtrArray **options, GError **error);
static gboolean syncevo_get_servers (SyncevoDBusServer *obj, GPtrArray **servers, GError **error);
static gboolean syncevo_get_server_config (SyncevoDBusServer *obj, char *server, GPtrArray **options, GError **error);
static gboolean syncevo_set_server_config (SyncevoDBusServer *obj, char *server, GPtrArray *options, GError **error);
static gboolean syncevo_remove_server_config (SyncevoDBusServer *obj, char *server, GError **error);
static gboolean syncevo_get_sync_reports (SyncevoDBusServer *obj, char *server, int count, GPtrArray **reports, GError **error);
#include "syncevo-dbus-glue.h"

enum SyncevoDBusError{
	SYNCEVO_DBUS_ERROR_GENERIC_ERROR,
	SYNCEVO_DBUS_ERROR_NO_SUCH_SERVER,
	SYNCEVO_DBUS_ERROR_MISSING_ARGS,
	SYNCEVO_DBUS_ERROR_INVALID_CALL, /* abort called when not syncing, or sync called when already syncing */
};

static GQuark syncevo_dbus_error_quark(void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string("syncevo-dbus-server");
	return quark;
}
#define SYNCEVO_DBUS_ERROR (syncevo_dbus_error_quark())

static GType
syncevo_dbus_error_get_type (void)
{
	static GType etype = 0;
	if (G_UNLIKELY (etype == 0)) {
		static const GEnumValue values[] = {
			{ SYNCEVO_DBUS_ERROR_GENERIC_ERROR, "SYNCEVO_DBUS_ERROR_GENERIC_ERROR", "GenericError" },
			{ SYNCEVO_DBUS_ERROR_NO_SUCH_SERVER, "SYNCEVO_DBUS_ERROR_NO_SUCH_SERVER", "NoSuchServer" },
			{ SYNCEVO_DBUS_ERROR_MISSING_ARGS, "SYNCEVO_DBUS_ERROR_MISSING_ARGS", "MissingArgs" },
			{ SYNCEVO_DBUS_ERROR_INVALID_CALL, "SYNCEVO_DBUS_ERROR_INVALID_CALL", "InvalidCall" },
			{ 0 }
		};
		etype = g_enum_register_static ("SyncevoDBusError", values);
	}
	return etype;
}
#define SYNCEVO_DBUS_ERROR_TYPE (syncevo_dbus_error_get_type ())


#define SYNCEVO_SOURCE_TYPE (dbus_g_type_get_struct ("GValueArray", G_TYPE_STRING, G_TYPE_INT, G_TYPE_INVALID))
typedef GValueArray SyncevoSource;
#define SYNCEVO_OPTION_TYPE (dbus_g_type_get_struct ("GValueArray", G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID))
typedef GValueArray SyncevoOption;
#define SYNCEVO_SERVER_TYPE (dbus_g_type_get_struct ("GValueArray", G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_INVALID))
typedef GValueArray SyncevoServer;

#define SYNCEVO_REPORT_TYPE (dbus_g_type_get_struct ("GValueArray", G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INVALID))
typedef GValueArray SyncevoReport;
#define SYNCEVO_REPORT_ARRAY_TYPE (dbus_g_type_get_struct ("GValueArray", G_TYPE_INT, dbus_g_type_get_collection ("GPtrArray", SYNCEVO_REPORT_TYPE), G_TYPE_INVALID))
typedef GValueArray SyncevoReportArray;

GMainLoop *loop;


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
syncevo_source_add_to_map (SyncevoSource *source, map<string, int> source_map)
{
	const char *str;
	int mode;
	
	syncevo_source_get (source, &str, &mode);
	source_map.insert (make_pair (str, mode));
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

SyncevoServer* syncevo_server_new (char *name, char *url, char *icon, gboolean consumer_ready)
{
	GValue val = {0, };

	g_value_init (&val, SYNCEVO_SERVER_TYPE);
	g_value_take_boxed (&val, dbus_g_type_specialized_construct (SYNCEVO_SERVER_TYPE));
	dbus_g_type_struct_set (&val,
	                        0, name,
	                        1, url,
	                        2, icon,
	                        3, consumer_ready,
	                        G_MAXUINT);

	return (SyncevoServer*) g_value_get_boxed (&val);
}

void syncevo_server_get (SyncevoServer *server, const char **name, const char **url, const char **icon, gboolean *consumer_ready)
{
	if (name) {
		*name = g_value_get_string (g_value_array_get_nth (server, 0));
	}
	if (url) {
		*url = g_value_get_string (g_value_array_get_nth (server, 1));
	}
	if (icon) {
		*icon = g_value_get_string (g_value_array_get_nth (server, 2));
	}
	if (consumer_ready) {
		*consumer_ready = g_value_get_boolean (g_value_array_get_nth (server, 3));
	}
}

void syncevo_server_free (SyncevoServer *server)
{
	if (server) {
		g_boxed_free (SYNCEVO_SERVER_TYPE, server);
	}
}

SyncevoReport* 
syncevo_report_new (char *source)
{
	GValue val = {0, };

	g_value_init (&val, SYNCEVO_REPORT_TYPE);
	g_value_take_boxed (&val, dbus_g_type_specialized_construct (SYNCEVO_REPORT_TYPE));
	dbus_g_type_struct_set (&val,
	                        0, source,
	                        G_MAXUINT);

	return (SyncevoReport*) g_value_get_boxed (&val);
}

static void
insert_int (SyncevoReport *report, int index, int value)
{
	GValue val = {0};

	g_value_init (&val, G_TYPE_INT);
	g_value_set_int (&val, value);
	g_value_array_insert (report, index, &val);
}

void
syncevo_report_set_io (SyncevoReport *report, 
                       int sent_bytes, int received_bytes)
{
	g_return_if_fail (report);

	insert_int (report, 1, sent_bytes);
	insert_int (report, 2, received_bytes);
}


void 
syncevo_report_set_local (SyncevoReport *report, 
                          int adds, int updates, int removes, int rejects)
{
	g_return_if_fail (report);

	insert_int (report, 3, adds);
	insert_int (report, 4, updates);
	insert_int (report, 5, removes);
	insert_int (report, 6, rejects);
}

void
syncevo_report_set_remote (SyncevoReport *report, 
                           int adds, int updates, int removes, int rejects)
{
	g_return_if_fail (report);

	insert_int (report, 7, adds);
	insert_int (report, 8, updates);
	insert_int (report, 9, removes);
	insert_int (report, 10, rejects);
}

void
syncevo_report_set_conflicts (SyncevoReport *report, 
                              int local_won, int remote_won, int duplicated)
{
	g_return_if_fail (report);

	insert_int (report, 11, local_won);
	insert_int (report, 12, remote_won);
	insert_int (report, 13, duplicated);
}

const char*
syncevo_report_get_name (SyncevoReport *report)
{
	g_return_val_if_fail (report, NULL);

	return g_value_get_string (g_value_array_get_nth (report, 0));

}

void
syncevo_report_get_io (SyncevoReport *report, 
                       int *bytes_sent, int *bytes_received)
{
	g_return_if_fail (report);

	if (bytes_sent) {
		*bytes_sent = g_value_get_int (g_value_array_get_nth (report, 1));
	}
	if (bytes_received) {
		*bytes_received = g_value_get_int (g_value_array_get_nth (report, 2));
	}
}

void
syncevo_report_get_local (SyncevoReport *report, 
                          int *adds, int *updates, int *removes, int *rejects)
{
	g_return_if_fail (report);

	if (adds) {
		*adds = g_value_get_int (g_value_array_get_nth (report, 3));
	}
	if (updates) {
		*updates = g_value_get_int (g_value_array_get_nth (report, 4));
	}
	if (removes) {
		*removes = g_value_get_int (g_value_array_get_nth (report, 5));
	}
	if (rejects) {
		*rejects = g_value_get_int (g_value_array_get_nth (report, 6));
	}
}

void
syncevo_report_get_remote (SyncevoReport *report, 
                           int *adds, int *updates, int *removes, int *rejects)
{
	g_return_if_fail (report);

	if (adds) {
		*adds = g_value_get_int (g_value_array_get_nth (report, 7));
	}
	if (updates) {
		*updates = g_value_get_int (g_value_array_get_nth (report, 8));
	}
	if (removes) {
		*removes = g_value_get_int (g_value_array_get_nth (report, 9));
	}
	if (rejects) {
		*rejects = g_value_get_int (g_value_array_get_nth (report, 10));
	}
}

void
syncevo_report_get_conflicts (SyncevoReport *report, 
                              int *local_won, int *remote_won, int *duplicated)
{
	g_return_if_fail (report);

	if (local_won) {
		*local_won = g_value_get_int (g_value_array_get_nth (report, 11));
	}
	if (remote_won) {
		*remote_won = g_value_get_int (g_value_array_get_nth (report, 12));
	}
	if (duplicated) {
		*duplicated = g_value_get_int (g_value_array_get_nth (report, 13));
	}
}

void
syncevo_report_free (SyncevoReport *report)
{
	if (report) {
		g_boxed_free (SYNCEVO_REPORT_TYPE, report);
	}
}

SyncevoReportArray* syncevo_report_array_new (int end_time, GPtrArray *reports)
{
	GValue val = {0, };

	g_value_init (&val, SYNCEVO_REPORT_ARRAY_TYPE);
	g_value_take_boxed (&val, dbus_g_type_specialized_construct (SYNCEVO_REPORT_ARRAY_TYPE));
	dbus_g_type_struct_set (&val,
	                        0, end_time,
	                        1, reports,
	                        G_MAXUINT);
	return (SyncevoReportArray*) g_value_get_boxed (&val);
}

void syncevo_report_array_get (SyncevoReportArray *array, int *end_time, GPtrArray **reports)
{
	g_return_if_fail (array);

	if (end_time) {
		*end_time = g_value_get_int (g_value_array_get_nth (array, 0));
	}
	if (reports) {
		*reports = (GPtrArray*)g_value_get_boxed (g_value_array_get_nth (array, 1));
	}
}

void
syncevo_report_array_free (SyncevoReportArray *array)
{
	if (array) {
		g_boxed_free (SYNCEVO_REPORT_ARRAY_TYPE, array);
	}
}


enum {
	PROGRESS,
	SERVER_MESSAGE,
	LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = {0};

G_DEFINE_TYPE (SyncevoDBusServer, syncevo_dbus_server, G_TYPE_OBJECT);

static gboolean
shutdown ()
{
	g_main_loop_quit (loop);

	return FALSE;
}

static void
update_shutdown_timer (SyncevoDBusServer *obj)
{
	if (obj->shutdown_timeout_src > 0)
		g_source_remove (obj->shutdown_timeout_src);

	obj->shutdown_timeout_src = g_timeout_add_seconds (120,
	                                                   (GSourceFunc)shutdown,
	                                                   NULL);
}

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

#ifdef USE_GNOME_KEYRING
char*
need_password (const char *username,
               const char *server_url,
               gpointer data)
{
	char *password = NULL;
	const char *server = NULL;
	GnomeKeyringResult res;

	server = strstr (server_url, "://");
	if (server)
		server = server + 3;

	if (!server)
		return NULL;

	res = gnome_keyring_find_password_sync (GNOME_KEYRING_NETWORK_PASSWORD,
	                                        &password,
	                                        "user", username,
	                                        "server", server,
	                                        NULL);

	switch (res) {
	case GNOME_KEYRING_RESULT_OK:
	case GNOME_KEYRING_RESULT_NO_MATCH:
		break;
	default:
		g_warning ("Failed to get password from keyring: %s", 
		           gnome_keyring_result_to_message (res));
		break;
	}

	return password;
}
#endif

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

	try {
		SyncReport report;
		ret = (*obj->client).sync(&report);
		if (ret != 0) {
			g_printerr ("sync returned error %d\n", ret);
		}
	} catch (...) {
		g_printerr ("sync failed (non-existing server?)\n");
		ret = -1;
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

	/* shutdown after a moment of inactivity */
	update_shutdown_timer (obj);

	return FALSE;
}

static gboolean  
syncevo_start_sync (SyncevoDBusServer *obj, 
                    char *server,
                    GPtrArray *sources,
                    GError **error)
{
	if (obj->server) {
		*error = g_error_new (SYNCEVO_DBUS_ERROR,
		                      SYNCEVO_DBUS_ERROR_INVALID_CALL,
		                      "Sync already in progress. Concurrent syncs are currently not supported");
		return FALSE;
	}
	if (!server) {
		*error = g_error_new (SYNCEVO_DBUS_ERROR,
		                      SYNCEVO_DBUS_ERROR_MISSING_ARGS, 
		                      "Server argument must be set");
		return FALSE;
	}

	/* don't auto-shutdown while syncing */
	if (obj->shutdown_timeout_src > 0) {
		g_source_remove (obj->shutdown_timeout_src);
		obj->shutdown_timeout_src = 0;
	}

	obj->aborted = FALSE;
	obj->server = g_strdup (server);

	map<string,int> source_map;
	g_ptr_array_foreach (sources, (GFunc)syncevo_source_add_to_map, &source_map);

#ifdef USE_GNOME_KEYRING
	obj->client = new DBusSyncClient (string (server), source_map, 
	                                  emit_progress, emit_server_message, need_password, check_for_suspend,
	                                  obj);
#else
	obj->client = new DBusSyncClient (string (server), source_map, 
	                                  emit_progress, emit_server_message, NULL, check_for_suspend,
	                                  obj);
#endif

	g_idle_add ((GSourceFunc)do_sync, obj); 

	return TRUE;
}

static gboolean 
syncevo_abort_sync (SyncevoDBusServer *obj,
                            char *server,
                            GError **error)
{
	if (!server) {
		*error = g_error_new (SYNCEVO_DBUS_ERROR,
		                      SYNCEVO_DBUS_ERROR_MISSING_ARGS, 
		                      "Server variable must be set");
		return FALSE;
	}

	if ((!obj->server) || strcmp (server, obj->server) != 0) {
		*error = g_error_new (SYNCEVO_DBUS_ERROR,
		                      SYNCEVO_DBUS_ERROR_INVALID_CALL, 
		                      "Not syncing server '%s'", server);
		return FALSE;
	}

	obj->aborted = TRUE;

	return TRUE;
}

static gboolean 
syncevo_get_servers (SyncevoDBusServer *obj,
                     GPtrArray **servers,
                     GError **error)
{
	if (!servers) {
		*error = g_error_new (SYNCEVO_DBUS_ERROR,
		                      SYNCEVO_DBUS_ERROR_MISSING_ARGS, 
		                      "servers argument must be set");
		return FALSE;
	}

	*servers = g_ptr_array_new ();

	SyncConfig::ServerList list = SyncConfig::getServers();

	BOOST_FOREACH(const SyncConfig::ServerList::value_type &server,list) {
		char *name = NULL;
		char *url = NULL;
		char *icon = NULL;
		gboolean ready = TRUE;
		SyncevoServer *srv;

		boost::shared_ptr<SyncConfig> config (SyncConfig::createServerTemplate (server.first));
		url = icon = NULL;
		if (config.get()) {
			url = g_strdup (config->getWebURL().c_str());
			icon = g_strdup (config->getIconURI().c_str());
			ready = config->getConsumerReady();
		}
		name = g_strdup (server.first.c_str());
		srv = syncevo_server_new (name, url, icon, ready);

		g_ptr_array_add (*servers, srv);
	}

	update_shutdown_timer (obj);

	return TRUE;
}

static gboolean 
syncevo_get_templates (SyncevoDBusServer *obj,
                       GPtrArray **templates,
                       GError **error)
{
	if (!templates) {
		*error = g_error_new (SYNCEVO_DBUS_ERROR,
		                      SYNCEVO_DBUS_ERROR_MISSING_ARGS, 
		                      "templates argument must be set");
		return FALSE;
	}

	*templates = g_ptr_array_new ();

	SyncConfig::ServerList list = SyncConfig::getServerTemplates();

	BOOST_FOREACH(const SyncConfig::ServerList::value_type &server,list) {
		char *name, *url, *icon;
		gboolean ready;
		SyncevoServer *temp;

		boost::shared_ptr<SyncConfig> config (SyncConfig::createServerTemplate (server.first));
		name = g_strdup (server.first.c_str());
		url = g_strdup (config->getWebURL().c_str());
		icon = g_strdup (config->getIconURI().c_str());
		ready = config->getConsumerReady();
		temp = syncevo_server_new (name, url, icon, ready);

		g_ptr_array_add (*templates, temp);
	}

	update_shutdown_timer (obj);

	return TRUE;
}

static gboolean 
syncevo_get_template_config (SyncevoDBusServer *obj, 
                             char *templ, 
                             GPtrArray **options, 
                             GError **error)
{
	SyncevoOption *option;
	const char *ready;

	if (!templ || !options) {
		if (options)
			*options = NULL;

		*error = g_error_new (SYNCEVO_DBUS_ERROR,
		                      SYNCEVO_DBUS_ERROR_MISSING_ARGS, 
		                      "Template and options arguments must be given");
		return FALSE;
	}

	boost::shared_ptr<SyncConfig> config (SyncConfig::createServerTemplate (string (templ)));
	if (!config.get()) {
		*options = NULL;
		*error = g_error_new (SYNCEVO_DBUS_ERROR,
		                      SYNCEVO_DBUS_ERROR_NO_SUCH_SERVER, 
		                      "No template '%s' found", templ);
		return FALSE;
	}

	*options = g_ptr_array_new ();
	option = syncevo_option_new (NULL, g_strdup ("syncURL"), g_strdup(config->getSyncURL()));
	g_ptr_array_add (*options, option);
	option = syncevo_option_new (NULL, g_strdup("username"), g_strdup(config->getUsername()));
	g_ptr_array_add (*options, option);
	option = syncevo_option_new (NULL, g_strdup("webURL"), g_strdup(config->getWebURL().c_str()));
	g_ptr_array_add (*options, option);
	option = syncevo_option_new (NULL, g_strdup("iconURI"), g_strdup(config->getIconURI().c_str()));
	g_ptr_array_add (*options, option);

	ready = (config->getConsumerReady() ? "yes" : "no");
	option = syncevo_option_new (NULL, g_strdup("consumerReady"), g_strdup(ready));
	g_ptr_array_add (*options, option);

	option = syncevo_option_new (NULL, g_strdup("fromTemplate"), g_strdup("yes"));
	g_ptr_array_add (*options, option);

	list<string> sources = config->getSyncSources();
	BOOST_FOREACH(const string &name, sources) {
		gboolean local;

		boost::shared_ptr<SyncSourceConfig> source_config = config->getSyncSourceConfig(name);

		option = syncevo_option_new (g_strdup (name.c_str()), g_strdup ("sync"), g_strdup (source_config->getSync()));
		g_ptr_array_add (*options, option);
		option = syncevo_option_new (g_strdup (name.c_str()), g_strdup ("uri"), g_strdup (source_config->getURI()));
		g_ptr_array_add (*options, option);

		/* check whether we have support locally */
		SyncSourceParams params(name, config->getSyncSourceNodes(name), "");
		auto_ptr<SyncSource> syncSource(SyncSource::createSource(params, false));
		try {
			local = FALSE;
			if (syncSource.get()) {
				syncSource->open();
				local = TRUE;
			}
		} catch (...) {}
		option = syncevo_option_new (g_strdup (name.c_str()), 
		                             g_strdup ("localDB"), 
		                             g_strdup_printf ("%d", local));
		g_ptr_array_add (*options, option);
	}

	update_shutdown_timer (obj);

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
		if (options)
			*options = NULL;
		*error = g_error_new (SYNCEVO_DBUS_ERROR,
		                      SYNCEVO_DBUS_ERROR_MISSING_ARGS, 
		                      "Server and options arguments must be given");
		return FALSE;
	}

	boost::shared_ptr<SyncConfig> from;
	boost::shared_ptr<SyncConfig> config(new SyncConfig (string (server)));
	/* if config does not exist, create from template */
	if (!config->exists()) {
		from = SyncConfig::createServerTemplate( string (server));
		if (!from.get()) {
			*options = NULL;
			*error = g_error_new (SYNCEVO_DBUS_ERROR,
			                      SYNCEVO_DBUS_ERROR_NO_SUCH_SERVER,
			                      "No server or template '%s' found", server);
			return FALSE;
		}
		config->copy(*from, NULL);
	}

	*options = g_ptr_array_new ();
	option = syncevo_option_new (NULL, g_strdup ("syncURL"), g_strdup(config->getSyncURL()));
	g_ptr_array_add (*options, option);
	option = syncevo_option_new (NULL, g_strdup("username"), g_strdup(config->getUsername()));
	g_ptr_array_add (*options, option);

	/* get template options if template exists */
	boost::shared_ptr<SyncConfig> templ = SyncConfig::createServerTemplate( string (server));
	if (templ.get()) {
		const char *ready;

		option = syncevo_option_new (NULL, g_strdup("fromTemplate"), g_strdup("yes"));
		g_ptr_array_add (*options, option);
		option = syncevo_option_new (NULL, g_strdup("webURL"), g_strdup(templ->getWebURL().c_str()));
		g_ptr_array_add (*options, option);
		option = syncevo_option_new (NULL, g_strdup("iconURI"), g_strdup(templ->getIconURI().c_str()));
		g_ptr_array_add (*options, option);

		ready = templ->getConsumerReady() ? "yes" : "no";
		option = syncevo_option_new (NULL, g_strdup("consumerReady"), g_strdup (ready));
		g_ptr_array_add (*options, option);
	}

	list<string> sources = config->getSyncSources();
	BOOST_FOREACH(const string &name, sources) {
		gboolean local;

		boost::shared_ptr<SyncSourceConfig> source_config = config->getSyncSourceConfig(name);

		option = syncevo_option_new (g_strdup (name.c_str()), g_strdup ("sync"), g_strdup (source_config->getSync()));
		g_ptr_array_add (*options, option);
		option = syncevo_option_new (g_strdup (name.c_str()), g_strdup ("uri"), g_strdup (source_config->getURI()));
		g_ptr_array_add (*options, option);

		/* check whether we have support locally */
		SyncSourceParams params(name, config->getSyncSourceNodes(name), "");
		auto_ptr<SyncSource> syncSource(SyncSource::createSource(params, false));
		try {
			local = FALSE;
			if (syncSource.get()) {
				syncSource->open();
				local = TRUE;
			}
		} catch (...) {}
		option = syncevo_option_new (g_strdup (name.c_str()), 
		                             g_strdup ("localDB"), 
		                             g_strdup_printf ("%d", local));
		g_ptr_array_add (*options, option);

	}

	update_shutdown_timer (obj);

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
		*error = g_error_new (SYNCEVO_DBUS_ERROR,
		                      SYNCEVO_DBUS_ERROR_MISSING_ARGS, 
		                      "Server and options parameters must be given");
		return FALSE;
	}

	if (obj->server) {
		*error = g_error_new (SYNCEVO_DBUS_ERROR,
		                      SYNCEVO_DBUS_ERROR_GENERIC_ERROR, 
		                      "GetServers is currently not supported when a sync is in progress");
		return FALSE;
	}

	boost::shared_ptr<SyncConfig> from(new SyncConfig (string (server)));
	/* if config does not exist, create from template */
	if (!from->exists()) {
		from = SyncConfig::createServerTemplate( string (server));
		if (!from.get()) {
			from = SyncConfig::createServerTemplate( string ("default"));
		}
	}
	boost::shared_ptr<SyncConfig> config(new SyncConfig(string (server)));
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
			} else if (strcmp (key, "webURL") == 0) {
				config->setWebURL (string (value));
			} else if (strcmp (key, "iconURI") == 0) {
				config->setIconURI (string (value));
			}
		} else if (ns && key) {
			boost::shared_ptr<SyncSourceConfig> source_config = config->getSyncSourceConfig(ns);
			if (strcmp (key, "sync") == 0) {
				source_config->setSync (string (value));
			} else if (strcmp (key, "uri") == 0) {
				source_config->setURI (string (value));
			}
		}
	}
	config->flush();

	update_shutdown_timer (obj);

	return TRUE;
}

static gboolean 
syncevo_remove_server_config (SyncevoDBusServer *obj,
                              char *server,
                              GError **error)
{
	if (!server) {
		*error = g_error_new (SYNCEVO_DBUS_ERROR,
		                      SYNCEVO_DBUS_ERROR_MISSING_ARGS, 
		                      "Server argument must be given");
		return FALSE;
	}

	if (obj->server) {
		*error = g_error_new (SYNCEVO_DBUS_ERROR,
		                      SYNCEVO_DBUS_ERROR_GENERIC_ERROR, 
		                      "RemoveServerConfig is not supported when a sync is in progress");
		return FALSE;
	}

	boost::shared_ptr<SyncConfig> config(new SyncConfig (string (server)));
	if (!config->exists()) {
		*error = g_error_new (SYNCEVO_DBUS_ERROR,
		                      SYNCEVO_DBUS_ERROR_NO_SUCH_SERVER,
		                      "No server '%s' found", server);
		return FALSE;
	}
	config->remove();

	update_shutdown_timer (obj);

	return TRUE;
}

static gboolean 
syncevo_get_sync_reports (SyncevoDBusServer *obj, 
                          char *server, 
                          int count,
                          GPtrArray **reports,
                          GError **error)
{
	if (!server) {
		*error = g_error_new (SYNCEVO_DBUS_ERROR,
		                      SYNCEVO_DBUS_ERROR_MISSING_ARGS, 
		                      "Server argument must be given");
		return FALSE;
	}

	if (!reports) {
		*error = g_error_new (SYNCEVO_DBUS_ERROR,
		                      SYNCEVO_DBUS_ERROR_MISSING_ARGS, 
		                      "reports argument must be given");
		return FALSE;
	}

	SyncContext client (string (server), false);
	vector<string> dirs;
	*reports = g_ptr_array_new ();

	client.getSessions (dirs);
	int start_from = dirs.size () - count;
	int index = 0;

	BOOST_FOREACH (const string &dir, dirs) {
		if (index < start_from) {
			index++;
		} else {
			SyncevoReportArray * session_report;
			GPtrArray *source_reports;
			SyncReport report;

			client.readSessionInfo (dir, report);
			source_reports = g_ptr_array_new ();

			for (SyncReport::iterator it = report.begin(); it != report.end(); ++it) {
				SyncevoReport *source_report;

				SyncSourceReport srcrep = it->second;
				source_report = syncevo_report_new (g_strdup (it->first.c_str ()));

				syncevo_report_set_io (source_report,
				                       srcrep.getItemStat (SyncSourceReport::ITEM_LOCAL, 
				                                           SyncSourceReport::ITEM_ANY, 
				                                           SyncSourceReport::ITEM_SENT_BYTES),
				                       srcrep.getItemStat (SyncSourceReport::ITEM_LOCAL, 
				                                           SyncSourceReport::ITEM_ANY, 
				                                           SyncSourceReport::ITEM_RECEIVED_BYTES));
				syncevo_report_set_local (source_report, 
				                          srcrep.getItemStat (SyncSourceReport::ITEM_LOCAL, 
				                                              SyncSourceReport::ITEM_ADDED, 
				                                              SyncSourceReport::ITEM_TOTAL),
				                          srcrep.getItemStat (SyncSourceReport::ITEM_LOCAL, 
				                                              SyncSourceReport::ITEM_UPDATED, 
				                                              SyncSourceReport::ITEM_TOTAL),
				                          srcrep.getItemStat (SyncSourceReport::ITEM_LOCAL, 
				                                              SyncSourceReport::ITEM_REMOVED, 
				                                              SyncSourceReport::ITEM_TOTAL),
				                          srcrep.getItemStat (SyncSourceReport::ITEM_LOCAL, 
				                                              SyncSourceReport::ITEM_ANY, 
				                                              SyncSourceReport::ITEM_REJECT));
				syncevo_report_set_remote (source_report, 
				                           srcrep.getItemStat (SyncSourceReport::ITEM_REMOTE, 
				                                               SyncSourceReport::ITEM_ADDED, 
				                                               SyncSourceReport::ITEM_TOTAL),
				                           srcrep.getItemStat (SyncSourceReport::ITEM_REMOTE, 
				                                               SyncSourceReport::ITEM_UPDATED, 
				                                               SyncSourceReport::ITEM_TOTAL),
				                           srcrep.getItemStat (SyncSourceReport::ITEM_REMOTE, 
				                                               SyncSourceReport::ITEM_REMOVED, 
				                                               SyncSourceReport::ITEM_TOTAL),
				                           srcrep.getItemStat (SyncSourceReport::ITEM_REMOTE, 
				                                               SyncSourceReport::ITEM_ANY, 
				                                               SyncSourceReport::ITEM_REJECT));
				syncevo_report_set_conflicts (source_report,
				                              srcrep.getItemStat (SyncSourceReport::ITEM_REMOTE, 
				                                                  SyncSourceReport::ITEM_ANY, 
				                                                  SyncSourceReport::ITEM_CONFLICT_CLIENT_WON),
				                              srcrep.getItemStat (SyncSourceReport::ITEM_REMOTE, 
				                                                  SyncSourceReport::ITEM_ANY, 
				                                                  SyncSourceReport::ITEM_CONFLICT_SERVER_WON),
				                              srcrep.getItemStat (SyncSourceReport::ITEM_REMOTE, 
				                                                  SyncSourceReport::ITEM_ANY, 
				                                                  SyncSourceReport::ITEM_CONFLICT_DUPLICATED));
				g_ptr_array_add (source_reports, source_report);
			}
			session_report = syncevo_report_array_new (report.getEnd (), source_reports);
			g_ptr_array_add (*reports, session_report);
		}
	}

	update_shutdown_timer (obj);

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

	/* dbus_glib_syncevo_object_info is provided in the generated glue file */
	dbus_g_object_type_install_info (SYNCEVO_TYPE_DBUS_SERVER, &dbus_glib_syncevo_object_info);

	/* register error domain so clients get proper error names with dbus_g_error_get_name() */
	dbus_g_error_domain_register (SYNCEVO_DBUS_ERROR, NULL, SYNCEVO_DBUS_ERROR_TYPE);
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

	update_shutdown_timer (obj);
}

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
	g_thread_init (NULL);
	g_set_application_name ("SyncEvolution");
	dbus_g_thread_init ();

	server = (SyncevoDBusServer*)g_object_new (SYNCEVO_TYPE_DBUS_SERVER, NULL);

	loop = g_main_loop_new (NULL, FALSE);
	g_main_loop_run (loop);

	g_main_loop_unref (loop);
	g_object_unref (server);
	return 0;
}
