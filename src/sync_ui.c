/*
 * TODO
 * 
 * * configuration needs a bit of thought
 *    - current design doesn't actually say how you choose thew current service
 *    - how to save additional info for a service, e.g. icon, last-synced time
 *    - "reset service settings" is probably a good idea... 
 *      how to implement: the brute force method is deleting the config dir...
 *    - should I use GetDefaults when creating a new server configuration?
 * * redesign main window? (talk with nick/patrick). Issues:
      - unnecessary options included in main window
      - last-sync needs to be source specific if it's easy to turn them on/off
      - should we force the sync type? Either yes or we start treating sync type
        source specifically
      - where to show statistic
 * * service icons everywhere
 * * where to save last-sync -- it's not a sync ui thing really, it should be 
 *   in server.... (see "how to save additional info for a service")
 * * backup/restore ? 
 * * leaking dbus params in dbus async-callbacks 
 * * GTK styling missing:
 *    - titlebar
 *    - a gtkbin with rounded corners
 *    - fade-effect for sync duration
 * * could set progress bar to pulse mode when e.g. send_current goes over send_total
 * 
 * * notes on dbus API:
 *    - should have a signals SyncFinished and possibly SyncStarted
 *      SyncFinished should have syncevolution return code as return param
 *      
 * */


#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <gconf/gconf-client.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <syncevo-dbus/syncevo-dbus.h>

/* for return value definitions */
/* TODO: would be nice to have a non-synthesis-dependent API but for now it's like this... */
#include <synthesis/syerror.h>
#include <synthesis/engine_defs.h>

#define SYNC_UI_GCONF_DIR "/apps/sync-ui"
#define SYNC_UI_SERVER_KEY SYNC_UI_GCONF_DIR"/server"
#define SYNC_UI_LAST_SYNC_KEY SYNC_UI_GCONF_DIR"/last-sync"

typedef struct source_config {
	char *name;
	gboolean enabled;
	char *uri;
} source_config;

typedef struct server_config {
	char *name;
	char *base_url;

	char *username;
	char *password;

	GList *source_configs;
	
	gboolean changed;
} server_config;

typedef struct source_progress {
	char* name;
	
	/* progress */
	int prepare_current;
	int prepare_total;
	int send_current;
	int send_total;
	int receive_current;
	int receive_total;
	
	/* statistics */
	int added_local;
	int modified_local;
	int deleted_local;
	int added_remote;
	int modified_remote;
	int deleted_remote;
	int bytes_uploaded;
	int bytes_downloaded;
} source_progress;

typedef enum app_state {
	SYNC_UI_STATE_GETTING_SERVER,
	SYNC_UI_STATE_NO_SERVER,
	SYNC_UI_STATE_SERVER_OK,
	SYNC_UI_STATE_SERVER_FAILURE,
	SYNC_UI_STATE_SYNCING,
} app_state;

/* absolute progress amounts 0-100 */
const float sync_progress_clicked = 0.02;
const float sync_progress_session_start = 0.04;
const float sync_progress_sync_start = 0.06;    /* prepare/send/receive cycles start here */
const float sync_progress_sync_end = 0.96;

/* how are prepare/send/receive weighed -- sum should be 100 */
const float sync_weight_prepare = 0.50;
const float sync_weight_send = 0.25;
const float sync_weight_receive = 0.25;

/* non-abolute progress amounts for prepare/send/receive (for all sources) */
#define SYNC_PROGRESS_PREPARE ((sync_progress_sync_end - sync_progress_sync_start) * sync_weight_prepare)
#define SYNC_PROGRESS_SEND ((sync_progress_sync_end - sync_progress_sync_start) * sync_weight_send)
#define SYNC_PROGRESS_RECEIVE ((sync_progress_sync_end - sync_progress_sync_start) * sync_weight_receive)


typedef struct app_data {
	GtkWidget *sync_win;
	GtkWidget *services_win;
	GtkWidget *service_settings_dlg;

	GtkWidget *server_box;
	GtkWidget *server_failure_box;
	GtkWidget *no_server_box;
	GtkWidget *info_box;

	GtkWidget *progress;
	GtkWidget *sync_btn;
	GtkWidget *restore_btn;
	GtkWidget *change_service_btn;
	GtkWidget *edit_service_btn;

	GtkWidget *server_label;
	GtkWidget *sources_box;

	GtkWidget *services_table;
	GtkWidget *loading_services_label;

	GtkWidget *service_name_label;
	GtkWidget *username_entry;
	GtkWidget *password_entry;
	GtkWidget *server_settings_table;

	gboolean syncing;
	glong last_sync;
	guint last_sync_src_id;
	GList *source_progresses;


	SyncevoService *service;

	server_config *current_service;

	GPtrArray *templates;
} app_data;

static void set_sync_progress (app_data *data, float progress, char *status);
static void set_app_state (app_data *data, app_state state);
static void show_services_window (app_data *data);
static void show_settings_dialog (app_data *data, server_config *config);

static void
source_config_free (source_config *source)
{
	if (!source)
		return;

	g_free (source->name);
	g_free (source->uri);
	g_slice_free (source_config, source);
}

static void
server_config_free (server_config *server)
{
	if (!server)
		return;

	g_free (server->name);
	g_free (server->base_url);
	g_free (server->username);
	g_free (server->password);
	g_list_foreach (server->source_configs, (GFunc)source_config_free, NULL);
	g_list_free (server->source_configs);
	g_slice_free (server_config, server);
}

static void
remove_child (GtkWidget *widget, GtkContainer *container)
{
	gtk_container_remove (container, widget);
}

static void 
change_service_clicked_cb (GtkButton *btn, app_data *data)
{
	show_services_window (data);
}

static void 
edit_services_clicked_cb (GtkButton *btn, app_data *data)
{
	show_settings_dialog (data, data->current_service);
}

static void
update_config_from_entry (GtkWidget *widget, server_config *server)
{
	char **str;
	gboolean *enabled;
	const char *new_str;

	if (!GTK_IS_ENTRY (widget))
		return;

	/* all entries have a pointer to the correct string in server_config */
	str = g_object_get_data (G_OBJECT (widget), "value");
	g_assert (str);

	new_str = gtk_entry_get_text (GTK_ENTRY (widget));

	if ((*str == NULL || strlen (*str) == 0) &&
	    (new_str == NULL || strlen (new_str) == 0))
	return;

	/* source entries have a pointer to enabled flag in server_config */
	enabled = g_object_get_data (G_OBJECT (widget), "enabled");

	if (*str == NULL || strcmp (*str, new_str) != 0) {
		g_free (*str);
		*str = g_strdup (new_str);
		/* set source enabled if uri is set */
		if (enabled != NULL && *str != NULL && strlen (*str) > 0) {
			*enabled = TRUE;
		}
		server->changed = TRUE;
	}
}

static GPtrArray*
get_option_array (server_config *server)
{
	GPtrArray *options;
	GList *l;
	SyncevoOption *option;
	
	g_assert (server);
	options = g_ptr_array_new ();
	
	option = syncevo_option_new (NULL, g_strdup ("syncURL"), g_strdup (server->base_url));
	g_ptr_array_add (options, option);

	option = syncevo_option_new (NULL, g_strdup ("username"), g_strdup (server->username));
	g_ptr_array_add (options, option);

	option = syncevo_option_new (NULL, g_strdup ("password"), g_strdup (server->password));
	g_ptr_array_add (options, option);

	for (l = server->source_configs; l; l = l->next) {
		source_config *source = (source_config*)l->data;

		option = syncevo_option_new (source->name, g_strdup ("uri"), g_strdup (source->uri));
		g_ptr_array_add (options, option);

		option = syncevo_option_new (source->name, g_strdup ("sync"), 
		                             source->enabled ? g_strdup ("two-way") : g_strdup ("none"));
		g_ptr_array_add (options, option);
	}

	return options;
}

static void
set_server_config_cb (SyncevoService *service, GError *error, app_data *data)
{
	GConfClient* client;
	GConfChangeSet *set;
	GError *err = NULL;
	
	if (error) {
		g_warning ("Failed to set server config: %s", error->message);
		g_error_free (error);
		return;
	}

	client = gconf_client_get_default ();
	set = gconf_change_set_new ();
	gconf_change_set_set_string (set, SYNC_UI_SERVER_KEY, data->current_service->name);
	gconf_change_set_set_string (set, SYNC_UI_LAST_SYNC_KEY, "-1");
	if (!gconf_client_commit_change_set (client, set, FALSE, &err)) {
		g_warning ("Failed to commit gconf changes: %s", err->message);
		g_error_free (err);
	}
	gconf_change_set_unref (set);
}

static void
service_settings_response_cb (GtkDialog *dialog, gint response, app_data *data)
{
	GPtrArray *options;
	server_config *server;

	server = g_object_get_data (G_OBJECT (data->service_settings_dlg), "server");

	if (response == 1) {
		/* reset */
		g_debug ("TODO: reset settings for service");
		/* wipe the ~/.config/syncevolution/server dir? */
		return;
	}

	gtk_widget_hide (GTK_WIDGET (data->service_settings_dlg));

	if (response != GTK_RESPONSE_APPLY) {
		if (server != data->current_service)
			server_config_free (server);
		/* cancelled or closed */
		return;
	}

	/* response == GTK_RESPONSE_APPLY */
	gtk_widget_hide (GTK_WIDGET (data->services_win));

	update_config_from_entry (data->username_entry, server);
	update_config_from_entry (data->password_entry, server);

	gtk_container_foreach (GTK_CONTAINER (data->server_settings_table), 
						   (GtkCallback)update_config_from_entry, server);

	data->current_service = server;

	if (!server->changed) {
		GConfClient* client;
		GConfChangeSet *set;
		GError *err = NULL;

		/* no need to save first, set the gconf key right away */
		client = gconf_client_get_default ();
		set = gconf_change_set_new ();
		gconf_change_set_set_string (set, SYNC_UI_SERVER_KEY, data->current_service->name);
		gconf_change_set_set_string (set, SYNC_UI_LAST_SYNC_KEY, "-1");
		if (!gconf_client_commit_change_set (client, set, FALSE, &err)) {
			g_warning ("Failed to commit gconf changes: %s", err->message);
			g_error_free (err);
		}
		gconf_change_set_unref (set);
	} else {
		/* save the server, let callback change current server gconf key */
		options = get_option_array (server);
		syncevo_service_set_server_config_async (data->service, 
												 server->name,
												 options,
												 (SyncevoSetServerConfigCb)set_server_config_cb, 
												 data);
		/* TODO free options */
		g_ptr_array_free (options, TRUE);
	}
}

static void 
sync_clicked_cb (GtkButton *btn, app_data *data)
{
	GPtrArray *sources;
	GList *list;
	GError *error = NULL;
	GtkWidget *info;
	if (data->syncing) {
		syncevo_service_abort_sync (data->service, data->current_service->name, &error);
		if (error) {
			gtk_container_foreach (GTK_CONTAINER (data->info_box), 
										  (GtkCallback)remove_child,
										  data->info_box);
			info = gtk_label_new ("Error: Failed to cancel");
			gtk_container_add (GTK_CONTAINER (data->info_box), info);
			gtk_widget_show_all (data->info_box);
			return;
		}
		gtk_widget_set_sensitive (data->sync_btn, FALSE);
		set_sync_progress (data, -1.0, "Canceling sync");
	} else {

		/* empty source progress list */
		list = data->source_progresses;
		for (list = data->source_progresses; list; list = list->next) {
			g_free (((source_progress*)list->data)->name);
			g_slice_free (source_progress, list->data);
		}
		g_list_free (data->source_progresses);
		data->source_progresses = NULL;

		sources = g_ptr_array_new ();
		syncevo_service_start_sync (data->service, 
									data->current_service->name,
									sources,
									&error);
		if (error) {
			gtk_container_foreach (GTK_CONTAINER (data->info_box), 
										  (GtkCallback)remove_child,
										  data->info_box);
			info = gtk_label_new ("Error: Failed to start sync");
			gtk_container_add (GTK_CONTAINER (data->info_box), info);
			gtk_widget_show_all (data->info_box);
		} else {
			/* stop updates of "last synced" */
			if (data->last_sync_src_id > 0)
				g_source_remove (data->last_sync_src_id);
			set_sync_progress (data, sync_progress_clicked, "Starting sync");
			set_app_state (data, SYNC_UI_STATE_SYNCING);
		}
	}
}

static gboolean
refresh_last_synced_label (app_data *data)
{
	GTimeVal val;
	glong diff;
	char *msg;
	int delay;

	g_get_current_time (&val);
	diff = val.tv_sec - data->last_sync;

	if (data->last_sync <= 0) {
		msg = g_strdup (""); /* we don't know */
		delay = -1;
	} else if (diff < 30) {
		msg = g_strdup ("Last synced seconds ago");
		delay = 30;
	} else if (diff < 90) {
		msg = g_strdup ("Last synced a minute ago");
		delay = 60;
	} else if (diff < 60 * 60) {
		msg = g_strdup_printf ("Last synced %ld minutes ago", (diff + 30) / 60);
		delay = 60;
	} else if (diff < 60 * 90) {
		msg = g_strdup ("Last synced an hour ago");
		delay = 60 * 60;
	} else if (diff < 60 * 60 * 24) {
		msg = g_strdup_printf ("Last synced %ld hours ago", (diff + 60 * 30) / (60 * 60));
		delay = 60 * 60;
	} else if (diff < 60 * 60 * 24 - (60 * 30)) {
		msg = g_strdup ("Last synced a day ago");
		delay = 60 * 60 * 24;
	} else {
		msg = g_strdup_printf ("Last synced %ld days ago", (diff + 24 * 60 * 30) / (60 * 60 * 24));
		delay = 60 * 60 * 24;
	}

	set_sync_progress (data, 0.0, msg);
	g_free (msg);

	if (data->last_sync_src_id > 0)
		g_source_remove (data->last_sync_src_id);
	if (delay > 0)
		data->last_sync_src_id = g_timeout_add_seconds (delay, (GSourceFunc)refresh_last_synced_label, data);

	return FALSE;
}


static void
set_sync_progress (app_data *data, float progress, char *status)
{
	g_debug ("progress: %f %s", progress, status);
	if (progress >= 0)
		gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (data->progress), progress);
	if (status)
		gtk_progress_bar_set_text (GTK_PROGRESS_BAR (data->progress), status);
}

static void
set_app_state (app_data *data, app_state state)
{
	GtkWidget *info;
	
	switch (state) {
	case SYNC_UI_STATE_GETTING_SERVER:
		gtk_widget_show (data->server_box);
		gtk_widget_hide (data->server_failure_box);
		gtk_widget_hide (data->no_server_box);
		gtk_container_foreach (GTK_CONTAINER (data->info_box), 
									  (GtkCallback)remove_child,
									  data->info_box);
		
		gtk_widget_set_sensitive (data->sync_btn, FALSE);
		gtk_widget_set_sensitive (data->restore_btn, FALSE);
		break;
	case SYNC_UI_STATE_NO_SERVER:
		gtk_widget_hide (data->server_box);
		gtk_widget_hide (data->server_failure_box);
		gtk_widget_show (data->no_server_box);
		gtk_container_foreach (GTK_CONTAINER (data->info_box), 
									  (GtkCallback)remove_child,
									  data->info_box);
		gtk_widget_set_sensitive (data->sync_btn, FALSE);
		gtk_widget_set_sensitive (data->restore_btn, FALSE);
		gtk_widget_set_sensitive (data->change_service_btn, TRUE);
		gtk_widget_set_sensitive (data->edit_service_btn, FALSE);
		break;
	case SYNC_UI_STATE_SERVER_FAILURE:
		gtk_widget_hide (data->server_box);
		gtk_widget_hide (data->no_server_box);
		gtk_widget_show (data->server_failure_box);
		gtk_container_foreach (GTK_CONTAINER (data->info_box), 
									  (GtkCallback)remove_child,
									  data->info_box);
		info = gtk_label_new ("Error: Failed to contact synchronization DBus service");
		gtk_container_add (GTK_CONTAINER (data->info_box), info);
		
		gtk_widget_set_sensitive (data->sync_btn, FALSE);
		gtk_widget_set_sensitive (data->restore_btn, FALSE);
		gtk_widget_set_sensitive (data->change_service_btn, FALSE);
		gtk_widget_set_sensitive (data->edit_service_btn, FALSE);
		break;
	case SYNC_UI_STATE_SERVER_OK:
		gtk_widget_show (data->server_box);
		gtk_widget_hide (data->server_failure_box);
		gtk_widget_hide (data->no_server_box);
		gtk_container_foreach (GTK_CONTAINER (data->info_box), 
									  (GtkCallback)remove_child,
									  data->info_box);
		gtk_widget_set_sensitive (data->sync_btn, TRUE);
		gtk_button_set_label (GTK_BUTTON (data->sync_btn), "Sync now");
		gtk_widget_set_sensitive (data->restore_btn, FALSE);
		gtk_widget_set_sensitive (data->change_service_btn, TRUE);
		gtk_widget_set_sensitive (data->edit_service_btn, TRUE);

		data->syncing = FALSE;
		break;
		
	case SYNC_UI_STATE_SYNCING:
		gtk_widget_set_sensitive (data->sync_btn, TRUE);
		gtk_button_set_label (GTK_BUTTON (data->sync_btn), "Cancel sync");
		gtk_widget_set_sensitive (data->restore_btn, FALSE);
		gtk_widget_set_sensitive (data->change_service_btn, FALSE);
		gtk_widget_set_sensitive (data->edit_service_btn, FALSE);
		
		data->syncing = TRUE;
		break;
	default:
		g_assert_not_reached ();
	}
}

static gboolean
init_ui (app_data *data)
{
	GtkBuilder *builder;
	GError *error = NULL;

	builder = gtk_builder_new ();
	gtk_builder_add_from_file (builder, GLADEDIR "ui.xml", &error);
	if (error) {
		g_printerr ("Failed to load user interface from %s\n", GLADEDIR "ui.xml");
		g_error_free (error);
		g_object_unref (builder);
		return FALSE;
	}

	data->sync_win = GTK_WIDGET (gtk_builder_get_object (builder, "sync_win"));
	data->services_win = GTK_WIDGET (gtk_builder_get_object (builder, "services_win"));
	data->service_settings_dlg = GTK_WIDGET (gtk_builder_get_object (builder, "service_settings_dlg"));

	data->server_box = GTK_WIDGET (gtk_builder_get_object (builder, "server_box"));
	data->no_server_box = GTK_WIDGET (gtk_builder_get_object (builder, "no_server_box"));
	data->server_failure_box = GTK_WIDGET (gtk_builder_get_object (builder, "server_failure_box"));
	data->info_box = GTK_WIDGET (gtk_builder_get_object (builder, "info_box"));

	data->progress = GTK_WIDGET (gtk_builder_get_object (builder, "progressbar"));
	data->change_service_btn = GTK_WIDGET (gtk_builder_get_object (builder, "change_service_btn"));
	data->edit_service_btn = GTK_WIDGET (gtk_builder_get_object (builder, "edit_service_btn"));
	data->sync_btn = GTK_WIDGET (gtk_builder_get_object (builder, "sync_btn"));
	data->restore_btn = GTK_WIDGET (gtk_builder_get_object (builder, "restore_btn"));

	data->server_label = GTK_WIDGET (gtk_builder_get_object (builder, "sync_service_label"));
	data->sources_box = GTK_WIDGET (gtk_builder_get_object (builder, "sources_box"));

	data->services_table = GTK_WIDGET (gtk_builder_get_object (builder, "services_table"));
	data->loading_services_label = GTK_WIDGET (gtk_builder_get_object (builder, "loading_services_label"));

	data->service_name_label = GTK_WIDGET (gtk_builder_get_object (builder, "service_name_label"));
	data->username_entry = GTK_WIDGET (gtk_builder_get_object (builder, "username_entry"));
	data->password_entry = GTK_WIDGET (gtk_builder_get_object (builder, "password_entry"));
	data->server_settings_table = GTK_WIDGET (gtk_builder_get_object (builder, "server_settings_table"));

	g_signal_connect (data->sync_win, "destroy",
	                  G_CALLBACK (gtk_main_quit), NULL);
	g_signal_connect (data->service_settings_dlg, "response",
	                  G_CALLBACK (service_settings_response_cb), data);
	g_signal_connect (data->change_service_btn, "clicked",
	                  G_CALLBACK (change_service_clicked_cb), data);
	g_signal_connect (data->edit_service_btn, "clicked",
	                  G_CALLBACK (edit_services_clicked_cb), data);
	g_signal_connect (data->sync_btn, "clicked", 
	                  G_CALLBACK (sync_clicked_cb), data);
	g_signal_connect_swapped (GTK_WIDGET (gtk_builder_get_object (builder, "services_close_btn")),
	                          "clicked", G_CALLBACK (gtk_widget_hide), data->services_win);

	return TRUE;
}

static source_config*
get_source_config (server_config *server, const char *name)
{
	GList *l;
	source_config *source = NULL;
	
	/* return existing source config if found */
	for (l = server->source_configs; l; l = l->next) {
		source = (source_config*)l->data;
		if (strcmp (source->name, name) == 0) {
			return source; 
		}
	}
	
	/* create new source config */
	source = g_slice_new0 (source_config);
	source->name = g_strdup (name);
	server->source_configs = g_list_append (server->source_configs, source);
	return source;
}

static void
add_server_option (SyncevoOption *option, server_config *server)
{
	const char *ns, *key, *value;

	syncevo_option_get (option, &ns, &key, &value);

	if (strlen (ns) == 0) {
		if (strcmp (key, "syncURL") == 0) {
			server->base_url = g_strdup (value);
		}
		if (strcmp (key, "username") == 0) {
			server->username = g_strdup (value);
		}
		if (strcmp (key, "password") == 0) {
			server->password = g_strdup (value);
		}
	} else {
		source_config *source;
		
		source = get_source_config (server, ns);
		
		if (strcmp (key, "uri") == 0) {
			source->uri = g_strdup (value);
		}
		if (strcmp (key, "sync") == 0) {
			if (strcmp (value, "disabled") == 0 ||
				 strcmp (value, "none") == 0) {
				/* consider this source not available at all */
				source->enabled = FALSE;
			} else {
				source->enabled = TRUE;
			}
		}
	}
}

static void
update_service_ui (app_data *data)
{
	GList *l;
	g_assert (data->current_service);

	gtk_container_foreach (GTK_CONTAINER (data->sources_box),
	                       (GtkCallback)remove_child,
	                       data->sources_box);


	if (data->current_service->name) {
		char *str;
		str = g_strdup_printf ("<b>%s</b>", data->current_service->name);
		gtk_label_set_markup (GTK_LABEL (data->server_label), str);
		g_free (str);
	}
	
	for (l = data->current_service->source_configs; l; l = l->next) {
		source_config *source = (source_config*)l->data;
		GtkWidget *check;
		
		check = gtk_check_button_new_with_label (source->name);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), source->enabled);
		gtk_widget_set_sensitive (check, FALSE);
		gtk_box_pack_start_defaults (GTK_BOX (data->sources_box), check);
	}
	gtk_widget_show_all (data->sources_box);

}

static void
get_server_config_cb (SyncevoService *service, GPtrArray *options, GError *error, app_data *data)
{
	if (error) {
		g_warning ("Failed to get server '%s' configuration: %s", 
					  data->current_service->name,
					  error->message);
		g_error_free (error);
		
		set_app_state (data, SYNC_UI_STATE_SERVER_FAILURE);
		
		return;
	}

	g_ptr_array_foreach (options, (GFunc)add_server_option, data->current_service);
	
	update_service_ui (data);
	set_app_state (data, SYNC_UI_STATE_SERVER_OK);
}

static void
show_link_button_url (GtkLinkButton *link)
{
	const char *url;
	int res;
	
	url = gtk_link_button_get_uri (GTK_LINK_BUTTON (link));
	res = gnome_vfs_url_show (url);
	if (res != GNOME_VFS_OK) {
		g_warning ("gnome_vfs_url_show('%s') failed: error %d", url, res);
	}
}

static void
show_settings_dialog (app_data *data, server_config *config)
{
	GList *l;
	GtkWidget *label, *entry;
	int i = 0;

	gtk_container_foreach (GTK_CONTAINER (data->server_settings_table),
	                       (GtkCallback)remove_child,
	                       data->server_settings_table);
	gtk_table_resize (GTK_TABLE (data->server_settings_table), 
	                  2, g_list_length (config->source_configs) + 1);

	if (config->name) {
		char *markup;
		markup = g_strdup_printf ("<big>%s</big>", config->name);
		gtk_label_set_markup (GTK_LABEL (data->service_name_label), markup);
		g_free (markup);
	}
	gtk_entry_set_text (GTK_ENTRY (data->username_entry), 
	                    config->username ? config->username : "");
	g_object_set_data (G_OBJECT (data->username_entry), "value", &config->username);

	gtk_entry_set_text (GTK_ENTRY (data->password_entry),
	                    config->password ? config->password : "");
	g_object_set_data (G_OBJECT (data->password_entry), "value", &config->password);

	label = gtk_label_new ("Server URL");
	gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
	gtk_table_attach (GTK_TABLE (data->server_settings_table), label,
	                  0, 1, i, i + 1, GTK_FILL, GTK_EXPAND, 0, 0);

	entry = gtk_entry_new_with_max_length (100);
	gtk_entry_set_text (GTK_ENTRY (entry), 
	                    config->base_url ? config->base_url : "");
	g_object_set_data (G_OBJECT (entry), "value", &config->base_url);
	gtk_table_attach_defaults (GTK_TABLE (data->server_settings_table), entry,
	                           1, 2, i, i + 1);

	for (l = config->source_configs; l; l = l->next) {
		source_config *source = (source_config*)l->data;
		char *str;
		i++;
		
		str = g_strdup_printf ("%s URI", source->name);
		label = gtk_label_new (str);
		g_free (str);
		gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
		gtk_table_attach (GTK_TABLE (data->server_settings_table), label,
		                  0, 1, i, i + 1, GTK_FILL, GTK_EXPAND, 0, 0);

		entry = gtk_entry_new_with_max_length (100);
		gtk_entry_set_text (GTK_ENTRY (entry), 
		                    source->uri ? source->uri : "");
		g_object_set_data (G_OBJECT (entry), "value", &source->uri);
		g_object_set_data (G_OBJECT (entry), "enabled", &source->enabled);
		gtk_table_attach_defaults (GTK_TABLE (data->server_settings_table), entry,
		                           1, 2, i, i + 1);
	}
	gtk_widget_show_all (data->server_settings_table);

	g_object_set_data (G_OBJECT (data->service_settings_dlg), "server", config);
	config->changed = FALSE;

	gtk_window_present (GTK_WINDOW (data->service_settings_dlg));
}

static void
ensure_default_sources_exist(server_config *server)
{
	int i;
	GList *l;
	source_config *source;
	char *defaults[] = {"addressbook", "calendar", "memo", "todo"};
	gboolean default_found[] = {FALSE, FALSE, FALSE, FALSE};

	for (l = server->source_configs; l; l = l->next) {
		source = (source_config*)l->data;
		
		for (i = 0; i < 4; i++){
			if (strcmp (source->name, defaults[i]) == 0) {
				default_found[i] = TRUE;
			}
		}
	}
	
	for (i = 0; i < 4; i++){
		if (!default_found[i]) {
			/* create new source config */
			source = g_slice_new0 (source_config);
			source->name = g_strdup (defaults[i]);
			server->source_configs = g_list_append (server->source_configs, source);
		}
	}
	
}

typedef struct server_data {
	const char *server_name;
	app_data *data;
} server_data;

static void
get_server_config_for_template_cb (SyncevoService *service, GPtrArray *options, GError *error, server_data *data)
{
	server_config *config;

	if (error) {
		g_warning ("Failed to get server '%s' configuration: %s", 
					  "",
					  error->message);
		g_error_free (error);
		
		return;
	}

	config = g_slice_new0 (server_config);
	config->name = g_strdup (data->server_name);
	g_ptr_array_foreach (options, (GFunc)add_server_option, config);
	ensure_default_sources_exist (config);
	
	show_settings_dialog (data->data, config);
	
	g_slice_free (server_data ,data);
}

static void
setup_service_clicked (GtkButton *btn, app_data *data)
{
	SyncevoServer *templ;
	server_data *serv_data;

	templ = g_object_get_data (G_OBJECT (btn), "template");

	serv_data = g_slice_new0 (server_data);
	serv_data->data = data;
	syncevo_server_get (templ, &serv_data->server_name, NULL);
	
	syncevo_service_get_server_config_async (data->service, 
	                                         (char*)serv_data->server_name,
	                                         (SyncevoGetServerConfigCb)get_server_config_for_template_cb, 
	                                         serv_data);
}

static void
add_template_to_table (app_data *data, int row, SyncevoServer *temp)
{
	GtkTable *table;
	GtkWidget *label, *box, *link, *btn;
	const char *name, *note;
	
	
	table = GTK_TABLE (data->services_table);
	syncevo_server_get (temp, &name, &note);
	
	label = gtk_label_new (name);
	gtk_table_attach (table, label, 1, 2, row, row+1,
	                  GTK_SHRINK|GTK_FILL, GTK_EXPAND|GTK_FILL, 5, 0);
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);

	box = gtk_hbox_new (FALSE, 0);
	gtk_table_attach (table, box, 2, 3, row, row+1,
	                  GTK_EXPAND|GTK_FILL, GTK_EXPAND|GTK_FILL, 5, 0);

	if (g_str_has_prefix (note, "http://")) {
		link = gtk_link_button_new_with_label (note, "Launch website");
		g_signal_connect (link, "clicked", 
		                  G_CALLBACK (show_link_button_url), NULL);
	} else {
		link = gtk_label_new (note);
	}
	gtk_box_pack_start (GTK_BOX (box), link, FALSE, FALSE, 0);

	btn = gtk_button_new_with_label ("Setup now");
	g_signal_connect (btn, "clicked",
	                  G_CALLBACK (setup_service_clicked), data);
	gtk_table_attach (table, btn, 3, 4, row, row+1,
	                  GTK_SHRINK|GTK_FILL, GTK_EXPAND|GTK_FILL, 5, 0);
	g_object_set_data_full (G_OBJECT (btn), "template", temp, (GDestroyNotify)syncevo_server_free);
}


static void
get_templates_cb (SyncevoService *service, GPtrArray *temps, GError *error, app_data *data)
{
	int i;

	if (error) {
		g_warning ("%s: Failed to get templates: %s", 
                           data->current_service->name,
                           error->message);
		g_error_free (error);
		/* TODO ? */
		return;
	}

	gtk_container_foreach (GTK_CONTAINER (data->services_table),
	                       (GtkCallback)remove_child,
	                       data->services_table);

	gtk_table_resize (GTK_TABLE (data->services_table), temps->len, 4);

	for (i = 0; i < temps->len; i++) {
		add_template_to_table (data, i, (SyncevoServer*)g_ptr_array_index (temps, i));
	}
	g_ptr_array_free (temps, TRUE);

	gtk_widget_show_all (data->services_table);
	gtk_widget_hide (data->loading_services_label);
}


static void show_services_window (app_data *data)
{
	gtk_widget_hide (data->services_table);
	gtk_widget_show (data->loading_services_label);
	
	syncevo_service_get_templates_async (data->service,
	                                     (SyncevoGetServerConfigCb)get_templates_cb,
	                                     data);
	gtk_window_present (GTK_WINDOW (data->services_win));
}

static void
gconf_change_cb (GConfClient *client, guint id, GConfEntry *entry, app_data *data)
{
	char *server = NULL;
	char *last_sync = NULL;
	GError *error = NULL;

	last_sync = gconf_client_get_string (client, SYNC_UI_LAST_SYNC_KEY, &error);
	if (error) {
		g_warning ("Could not read last sync time from gconf: %s", error->message);
		g_error_free (error);
		error = NULL;
	}
	if (last_sync) {
		data->last_sync = strtol (last_sync, NULL, 0);
		g_free (last_sync);
	}
	refresh_last_synced_label (data);

	server = gconf_client_get_string (client, SYNC_UI_SERVER_KEY, &error);
	if (error) {
		g_warning ("Could not read current server name from gconf: %s", error->message);
		g_error_free (error);
		error = NULL;
	}
	
	server_config_free (data->current_service);
	if (!server) {
		data->current_service = NULL; 
		set_app_state (data, SYNC_UI_STATE_NO_SERVER);
	} else {
		data->current_service = g_slice_new0 (server_config);
		data->current_service->name = server;
		set_app_state (data, SYNC_UI_STATE_GETTING_SERVER);
		syncevo_service_get_server_config_async (data->service, 
		                                         server,
		                                         (SyncevoGetServerConfigCb)get_server_config_cb, 
		                                         data);
	}
}

static void
init_configuration (app_data *data)
{
	GConfClient* client;

	client = gconf_client_get_default ();
	gconf_client_add_dir (client, SYNC_UI_GCONF_DIR, GCONF_CLIENT_PRELOAD_RECURSIVE, NULL);
	gconf_client_notify_add (client, SYNC_UI_GCONF_DIR, (GConfClientNotifyFunc)gconf_change_cb,
	                         data, NULL, NULL);

	/* fake gconf change to init values */
	gconf_change_cb (client, 0, NULL, data);
}

static void
calc_and_update_progress (app_data *data, char *msg)
{
	float progress;
	GList *list;
	int count = 0;
	
	progress = 0.0;
	for (list = data->source_progresses; list; list = list->next) {
		source_progress *p = (source_progress*)list->data;
		if (p->prepare_total > 0)
			progress += SYNC_PROGRESS_PREPARE * p->prepare_current / p->prepare_total;
		if (p->send_total > 0)
			progress += SYNC_PROGRESS_SEND * p->send_current / p->send_total;
		if (p->receive_total > 0)
			progress += SYNC_PROGRESS_RECEIVE * p->receive_current / p->receive_total;
		count++;
	}
	
	set_sync_progress (data, sync_progress_sync_start + (progress / count), msg);
}

static void
refresh_statistics (app_data *data)
{
	GList *list;

	for (list = data->source_progresses; list; list = list->next) {
		source_progress *p = (source_progress*)list->data;
		g_debug ("TODO: statistics for '%s':", p->name);
		g_debug ("      data: TX: %d B, RX %d B", p->bytes_uploaded, p->bytes_downloaded);
		g_debug ("      sent to server: %d new, %d updated, %d deleted", p->added_remote, p->modified_remote, p->deleted_remote);
		g_debug ("      received from server: %d new, %d updated, %d deleted", p->added_local, p->modified_local, p->deleted_local);
	}
}

static source_progress*
find_source_progress (GList *source_progresses, char *name)
{
	GList *list;

	for (list = source_progresses; list; list = list->next) {
		if (strcmp (((source_progress*)list->data)->name, name) == 0) {
			return (source_progress*)list->data;
		}
	}
	return NULL;
}

static void
sync_progress_cb (SyncevoService *service,
						char *server,
						char *source,
						int type,
						int extra1, int extra2, int extra3,
						app_data *data)
{
	static source_progress *source_prog;
	char *msg;
	GTimeVal val;
	GConfClient* client;
	GError *error = NULL;

	switch(type) {
	case PEV_SESSIONSTART:
		/* double check we're in correct state*/
		set_app_state (data, SYNC_UI_STATE_SYNCING);
		set_sync_progress (data, sync_progress_session_start, NULL);
		break;
	case PEV_SESSIONEND:

		/* TODO should check for sync success before doing this 
		 * -- should have a new signal with syncevolution return code in it */

		set_app_state (data, SYNC_UI_STATE_SERVER_OK);
		g_get_current_time (&val);
		data->last_sync = val.tv_sec;

		/* save last sync time in gconf (using string since there's no 'longint' in gconf)*/
		client = gconf_client_get_default ();
		msg = g_strdup_printf("%ld", data->last_sync);

		gconf_client_set_string (client, SYNC_UI_LAST_SYNC_KEY, msg, &error);
		g_free (msg);
		if (error) {
			g_warning ("Could not save last sync time to gconf: %s", error->message);
			g_error_free (error);
		}

		refresh_last_synced_label (data);
		refresh_statistics (data);
		break;

	case PEV_ALERTED:
		source_prog = g_slice_new0 (source_progress);
		source_prog->name = g_strdup (source);
		data->source_progresses = g_list_append (data->source_progresses, source_prog);
		break;

	case PEV_PREPARING:
		/* find the right source (try last used one first) */
		if (strcmp (source_prog->name, source) != 0) {
			source_prog = find_source_progress (data->source_progresses, source);
			if (!source_prog) {
				g_warning ("No alert received for source '%s'", source);
				return;
			}
		}
		source_prog->prepare_current = CLAMP (extra1, 0, extra2);
		source_prog->prepare_total = extra2;

		msg = g_strdup_printf ("Preparing '%s'", source);
		calc_and_update_progress(data, msg);
		g_free (msg);
		break;

	case PEV_ITEMSENT:
		/* find the right source (try last used one first) */
		if (strcmp (source_prog->name, source) != 0) {
			source_prog = find_source_progress (data->source_progresses, source);
			if (!source_prog) {
				g_warning ("No alert received for source '%s'", source);
				return;
			}
		}
		source_prog->send_current = CLAMP (extra1, 0, extra2);
		source_prog->send_total = extra2;

		msg = g_strdup_printf ("Sending '%s'", source);
		calc_and_update_progress (data, msg);
		g_free (msg);
		break;

	case PEV_ITEMRECEIVED:
		/* find the right source (try last used one first) */
		if (strcmp (source_prog->name, source) != 0) {
			source_prog = find_source_progress (data->source_progresses, source);
			if (!source_prog) {
				g_warning ("No alert received for source '%s'", source);
				return;
			}
		}
		source_prog->receive_current = CLAMP (extra1, 0, extra2);
		source_prog->receive_total = extra2;

		msg = g_strdup_printf ("Receiving '%s'", source);
		  calc_and_update_progress (data, msg);
		  g_free (msg);
		  break;

	case PEV_SYNCEND:
		switch (extra1) {
		case 0:
				/* source sync ok */
				break;
		case LOCERR_USERABORT:
				g_debug ("user abort");
				break;
		case LOCERR_USERSUSPEND:
				g_debug ("user suspend");
				break;
		default:
				;
		}
		break;
	case PEV_DSSTATS_L:
		source_prog = find_source_progress (data->source_progresses, source);
		if (!source_prog) {
			g_warning ("No alert received for source '%s'", source);
			return;
		}
		source_prog->added_local = extra1;
		source_prog->modified_local = extra2;
		source_prog->deleted_local = extra3;
		break;
	case PEV_DSSTATS_R:
		source_prog = find_source_progress (data->source_progresses, source);
		if (!source_prog) {
			g_warning ("No alert received for source '%s'", source);
			return;
		}
		source_prog->added_remote = extra1;
		source_prog->modified_remote = extra2;
		source_prog->deleted_remote = extra3;
		break;
	case PEV_DSSTATS_E:
		source_prog = find_source_progress (data->source_progresses, source);
		if (!source_prog) {
			g_warning ("No alert received for source '%s'", source);
			return;
		}
		if (extra1 > 0 || extra2 >0) {
			g_warning ("%d locally rejected item, %d remotely rejected item", extra1, extra2);
		}
		break;
	case PEV_DSSTATS_D:
		source_prog = find_source_progress (data->source_progresses, source);
		if (!source_prog) {
			g_warning ("No alert received for source '%s'", source);
			return;
		}
		source_prog->bytes_uploaded = extra1;
		source_prog->bytes_downloaded = extra2;
		break;
	default:
		;
	}
}

int 
main (int argc, char *argv[]) {
	app_data *data;

	gtk_init(&argc, &argv);

	data = g_slice_new0 (app_data);
	if (!init_ui (data)) {
		return (1);
	}

	data->service = syncevo_service_get_default();
	g_signal_connect (data->service, "progress", 
	                  G_CALLBACK (sync_progress_cb), data);
	init_configuration (data);

	gtk_window_present (GTK_WINDOW (data->sync_win));

	gtk_main();
	return 0;
}
