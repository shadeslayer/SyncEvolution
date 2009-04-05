/*
 * TODO
 * 
 * * dbus api should have sync-finished signal (with syncevolutions return code as payload)
 * * configuration setting not done at all
 *    - current design doesn't actualy say how you choose thew current service
 *    - how to save additional info for a service, e.g. icon
 * * redesign main window? (talk with nick/patrick). Issues:
      - unnecessary options included in main window
      - last-sync needs to be source specific if it's easy to turn them on/off
      - should we force the sync type? Either yes or we start treating sync type
        source specifically
      - where to show statistic
 * * service icons
 * * last-sync should be service specific
 * * backup/restore ? 
 * * leaking in dbus async-callbacks 
 * * GTK styling missing:
 *    - titlebar
 *    - a gtkbin with rounded corners
 *    - fade-effect for sync duration
 *  
 * * notes on dbus API:
 *    - should have a signals SyncFinished and possibly SyncStarted
 *      SyncFinished should have syncevolution return code as return param
 *      SyncStarted could have a DBus path that can be used to connect to the 
 *      detailed progress signals
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

#define SYNC_UI_SERVER_KEY "/apps/sync-ui/server"
#define SYNC_UI_LAST_SYNC_KEY "/apps/sync-ui/last-sync"

typedef struct server_config {
	char *name;
	char *base_url;

	char *username;
	char *password;

	gboolean calendar_enabled;
	char *calendar_uri;

	gboolean contacts_enabled;
	char *contacts_uri;

	gboolean notes_enabled;
	char *notes_uri;

	gboolean tasks_enabled;
	char *tasks_uri;
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
	GtkWidget *no_server_box;
	GtkWidget *info_box;

	GtkWidget *progress;
	GtkWidget *sync_btn;
	GtkWidget *restore_btn;
	GtkWidget *change_service_btn;
	GtkWidget *edit_service_btn;

	GtkWidget *server_label;
	GtkWidget *calendar_check;
	GtkWidget *contacts_check;
	GtkWidget *tasks_check;
	GtkWidget *notes_check;

	GtkWidget *services_table;
	GtkWidget *loading_services_label;

	GtkWidget *service_name_label;
	GtkWidget *username_entry;
	GtkWidget *password_entry;
	GtkWidget *url_entry;
	GtkWidget *calendar_uri_entry;
	GtkWidget *contacts_uri_entry;
	GtkWidget *notes_uri_entry;
	GtkWidget *tasks_uri_entry;

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
static void init_services_window (app_data *data);
static void show_settings_dialog (app_data *data, server_config *config);

static void
remove_child (GtkWidget *widget, GtkContainer *container)
{
	gtk_container_remove (container, widget);
}

static void 
change_service_clicked_cb (GtkButton *btn, app_data *data)
{
	init_services_window (data);
	gtk_window_present (GTK_WINDOW (data->services_win));
}

static void 
edit_services_clicked_cb (GtkButton *btn, app_data *data)
{
	show_settings_dialog (data, data->current_service);
}

static void
service_settings_response_cb (GtkDialog *dialog, gint response, app_data *data)
{
	/* TODO: save settings on apply */
	gtk_widget_hide (GTK_WIDGET (data->service_settings_dlg));
}

static void 
sync_clicked_cb (GtkButton *btn, app_data *data)
{
	GPtrArray *sources;
	GList *list;
	GError *error;
	GtkWidget *info;
	if (data->syncing) {
		syncevo_service_abort_sync (data->service, data->current_service->name, &error);
		if (error) {
			gtk_container_foreach (GTK_CONTAINER (data->info_box), 
										  (GtkCallback)remove_child,
										  data->info_box);
			info = gtk_label_new ("Error: Failed to cancel");
			gtk_container_add (GTK_CONTAINER (data->info_box), info);
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
		msg = g_strdup ("Never synchronized");
		delay = -1;
	} else if (diff < 30) {
		msg = g_strdup ("Last synced seconds ago");
		delay = 30;
	} else if (diff < 90) {
		msg = g_strdup ("Last synced a minute ago");
		delay = 60;
	} else if (diff < 60 * 60) {
		msg = g_strdup_printf ("Last synced %d minutes ago", (diff + 30) / 60);
		delay = 60;
	} else if (diff < 60 * 90) {
		msg = g_strdup ("Last synced an hour ago");
		delay = 60 * 60;
	} else if (diff < 60 * 60 * 24) {
		msg = g_strdup_printf ("Last synced %d hours ago", (diff + 60 * 30) / (60 * 60));
		delay = 60 * 60;
	} else if (diff < 60 * 60 * 24 - (60 * 30)) {
		msg = g_strdup ("Last synced a day ago");
		delay = 60 * 60 * 24;
	} else {
		msg = g_strdup_printf ("Last synced %d days ago", (diff + 24 * 60 * 30) / (60 * 60 * 24));
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
	char *str;
	
	switch (state) {
	case SYNC_UI_STATE_GETTING_SERVER:
		gtk_widget_hide (data->server_box);
		gtk_widget_hide (data->no_server_box);
		gtk_container_foreach (GTK_CONTAINER (data->info_box), 
									  (GtkCallback)remove_child,
									  data->info_box);
		
		gtk_widget_set_sensitive (data->sync_btn, FALSE);
		gtk_widget_set_sensitive (data->restore_btn, FALSE);
		break;
	case SYNC_UI_STATE_NO_SERVER:
		gtk_widget_hide (data->server_box);
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
		gtk_widget_hide (data->no_server_box);
		gtk_container_foreach (GTK_CONTAINER (data->info_box), 
									  (GtkCallback)remove_child,
									  data->info_box);
		gtk_widget_set_sensitive (data->sync_btn, TRUE);
		gtk_button_set_label (GTK_BUTTON (data->sync_btn), "Sync now");
		gtk_widget_set_sensitive (data->restore_btn, FALSE);
		gtk_widget_set_sensitive (data->change_service_btn, TRUE);
		gtk_widget_set_sensitive (data->edit_service_btn, TRUE);

		str = g_strdup_printf ("<b>%s</b>", data->current_service->name);
		gtk_label_set_markup (GTK_LABEL (data->server_label), str);
		g_free (str);

		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->calendar_check), 
												data->current_service->calendar_enabled);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->contacts_check), 
												data->current_service->contacts_enabled);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->notes_check), 
												data->current_service->notes_enabled);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->tasks_check), 
												data->current_service->tasks_enabled);

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
	data->info_box = GTK_WIDGET (gtk_builder_get_object (builder, "info_box"));

	data->progress = GTK_WIDGET (gtk_builder_get_object (builder, "progressbar"));
	data->change_service_btn = GTK_WIDGET (gtk_builder_get_object (builder, "change_service_btn"));
	data->edit_service_btn = GTK_WIDGET (gtk_builder_get_object (builder, "edit_service_btn"));
	data->sync_btn = GTK_WIDGET (gtk_builder_get_object (builder, "sync_btn"));
	data->restore_btn = GTK_WIDGET (gtk_builder_get_object (builder, "restore_btn"));

	data->server_label = GTK_WIDGET (gtk_builder_get_object (builder, "sync_service_label"));
	data->calendar_check = GTK_WIDGET (gtk_builder_get_object (builder, "calendar_check"));
	data->contacts_check = GTK_WIDGET (gtk_builder_get_object (builder, "contacts_check"));
	data->tasks_check = GTK_WIDGET (gtk_builder_get_object (builder, "tasks_check"));
	data->notes_check = GTK_WIDGET (gtk_builder_get_object (builder, "notes_check"));

	data->services_table = GTK_WIDGET (gtk_builder_get_object (builder, "services_table"));
	data->loading_services_label = GTK_WIDGET (gtk_builder_get_object (builder, "loading_services_label"));

	data->service_name_label = GTK_WIDGET (gtk_builder_get_object (builder, "service_name_label"));
	data->username_entry = GTK_WIDGET (gtk_builder_get_object (builder, "username_entry"));
	data->password_entry = GTK_WIDGET (gtk_builder_get_object (builder, "password_entry"));
	data->url_entry = GTK_WIDGET (gtk_builder_get_object (builder, "url_entry"));
	data->calendar_uri_entry = GTK_WIDGET (gtk_builder_get_object (builder, "calendar_uri_entry"));
	data->contacts_uri_entry = GTK_WIDGET (gtk_builder_get_object (builder, "contacts_uri_entry"));
	data->notes_uri_entry = GTK_WIDGET (gtk_builder_get_object (builder, "notes_uri_entry"));
	data->tasks_uri_entry = GTK_WIDGET (gtk_builder_get_object (builder, "tasks_uri_entry"));

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


static void
add_server_option (SyncevoOption *option, server_config *server)
{
	const char *ns, *key, *value;

	syncevo_option_get (option, &ns, &key, &value);
	g_debug ("Got option %s: %s = %s", ns, key, value);

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
	} else if (strcmp (ns, "addressbook") == 0) {
		if (strcmp (key, "uri") == 0) {
			server->contacts_uri = g_strdup (value);
		}
		if (strcmp (key, "sync") == 0) {
			if (strcmp (value, "disabled") == 0 ||
				 strcmp (value, "none") == 0) {
				/* consider this source not available at all */
				server->contacts_enabled = FALSE;
			} else {
				server->contacts_enabled = TRUE;
			}
		}
	} else if (strcmp (ns, "calendar") == 0) {
		if (strcmp (key, "uri") == 0) {
			server->calendar_uri = g_strdup (value);
		}
		if (strcmp (key, "sync") == 0) {
			if (strcmp (value, "disabled") == 0 ||
				 strcmp (value, "none") == 0) {
				server->calendar_enabled = FALSE;
			} else {
				server->calendar_enabled = TRUE;
			}
		}
	} else if (strcmp (ns, "memo") == 0) {
		if (strcmp (key, "uri") == 0) {
			server->notes_uri = g_strdup (value);
		}
		if (strcmp (key, "sync") == 0) {
			if (strcmp (value, "disabled") == 0 ||
				 strcmp (value, "none") == 0) {
				/* consider this source not available at all */
				server->notes_enabled = FALSE;
			} else {
				server->notes_enabled = TRUE;
			}
		}
	} else if (strcmp (ns, "todo") == 0) {
		if (strcmp (key, "uri") == 0) {
			server->tasks_uri = g_strdup (value);
		}
		if (strcmp (key, "sync") == 0) {
			if (strcmp (value, "disabled") == 0 ||
				 strcmp (value, "none") == 0) {
				/* consider this source not available at all */
				server->tasks_enabled = FALSE;
			} else {
				server->tasks_enabled = TRUE;
			}
		}
	}
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
	
	if (config->name) {
		char *markup;
		markup = g_strdup_printf ("<big>%s</big>", config->name);
		gtk_label_set_markup (GTK_LABEL (data->service_name_label), markup);
		g_free (markup);
	}
	gtk_entry_set_text (GTK_ENTRY (data->username_entry), 
	                    config->username ? config->username : "");
	gtk_entry_set_text (GTK_ENTRY (data->password_entry),
	                    config->password ? config->password : "");
	gtk_entry_set_text (GTK_ENTRY (data->url_entry),
	                    config->base_url ? config->base_url : "");
	gtk_entry_set_text (GTK_ENTRY (data->calendar_uri_entry),
	                    config->calendar_uri ? config->calendar_uri : "");
	gtk_entry_set_text (GTK_ENTRY (data->contacts_uri_entry), 
	                    config->contacts_uri ? config->contacts_uri : "");
	gtk_entry_set_text (GTK_ENTRY (data->notes_uri_entry),
	                    config->notes_uri ? config->notes_uri : "");
	gtk_entry_set_text (GTK_ENTRY (data->tasks_uri_entry), 
	                    config->tasks_uri ? config->tasks_uri : "");
	
	gtk_window_present (GTK_WINDOW (data->service_settings_dlg));
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
		g_warning ("Failed to get templates: %s", 
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


static void init_services_window (app_data *data)
{
	gtk_widget_hide (data->services_table);
	gtk_widget_show (data->loading_services_label);
	
	
	syncevo_service_get_templates_async (data->service,
	                                     (SyncevoGetServerConfigCb)get_templates_cb,
	                                     data);
}

static void
init_configuration (app_data *data)
{
	GConfClient* client;
	GError *error = NULL;
	char *server = NULL;
	char *last_sync = NULL;

	client = gconf_client_get_default ();

	last_sync = gconf_client_get_string (client, SYNC_UI_LAST_SYNC_KEY, &error);
	if (error) {
		g_warning ("Could not read last sync time from gconf: %s", error->message);
		g_error_free (error);
		error = NULL;
	}
	if (last_sync) {
		data->last_sync = strtol (last_sync, NULL, 0);
		refresh_last_synced_label (data);
		g_free (last_sync);
	}

	server = gconf_client_get_string (client, SYNC_UI_SERVER_KEY, &error);
	if (error) {
		g_warning ("Could not read current server name from gconf: %s", error->message);
		g_error_free (error);
		error = NULL;
	}
	if (!server) {
		set_app_state (data, SYNC_UI_STATE_NO_SERVER);
		return;
	}

	data->current_service = g_slice_new0 (server_config);
	data->current_service->name = server;

	set_app_state (data, SYNC_UI_STATE_GETTING_SERVER);

	syncevo_service_get_server_config_async (data->service, 
	                                         server,
	                                         (SyncevoGetServerConfigCb)get_server_config_cb, 
	                                         data);
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
		source_prog->prepare_current = CLAMP (extra1, 0, extra2);
		source_prog->prepare_total = extra2;

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
		source_prog->prepare_current = CLAMP (extra1, 0, extra2);
		source_prog->prepare_total = extra2;

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
