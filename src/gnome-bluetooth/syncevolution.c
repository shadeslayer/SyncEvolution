/*
 *  Copyright (C) 2009 Intel Corporation
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtk.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <bluetooth-plugin.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>

#define SYNCUI_BINARY "sync-ui"
#define SYNCUI_ARG "--show-settings obex+bt://"

/*Only detecting SyncML Client*/
static gboolean
has_config_widget (const char *bdaddr, const char **uuids)
{
	int i;
	if (uuids == NULL) {
		return FALSE;
	}

	for (i = 0; uuids[i] != NULL; i++) {
		if (!g_strcmp0 (uuids[i], "SyncMLClient")) {
			//find whether "sync-ui" is available
			if (g_find_program_in_path (SYNCUI_BINARY) == NULL) {
				return FALSE;
			}
			return TRUE;
		}
	}
	return FALSE;
}

static void
button_clicked (GtkButton *button, gpointer user_data)
{
	const char *bdaddr;
	pid_t midman;
	pid_t syncui;
	char args[sizeof (SYNCUI_ARG) + sizeof ("FF:FF:FF:FF:FF:FF") - 1];

	bdaddr = g_object_get_data (G_OBJECT (button), "bdaddr");
	midman = fork ();
	if (midman == 0) {
		//in midman process
		syncui = fork();
		if (syncui == 0) {
			// in syncui process
			strcpy (args, SYNCUI_ARG);
			strncat (args, bdaddr, sizeof ("FF:FF:FF:FF:FF:FF"));
			if (execlp (SYNCUI_BINARY, args, NULL) == -1){
				g_warning ("SyncEvolution plugin failed to launch sync-ui!");
				exit (-1);
			}
		} else if (syncui == -1) {
			// error in forking sub process
			g_warning ("SyncEvolution plugin failed to launch sync-ui!");
			exit (-1);
		} else {
			//do nothing, just exit. This will cause sync-ui
			//process an orphan.
			exit (0);
		}
	} else if (midman == -1) {
		//error in forking sub process
		g_warning ("SyncEvolution plugin failed to launch sync-ui!");
	} else {
		//in bluetooth-panel process
		if (waitpid (midman, NULL, 0) == -1){
			//error in waitpid
			g_warning ("SyncEvolution plugin failed to launch sync-ui!");
		}
	}
}

static GtkWidget *
get_config_widgets (const char *bdaddr, const char **uuids)
{
	GtkWidget *hbox;
	GtkWidget *button;
	GtkWidget *label1;
	GtkWidget *label2;
	int label_max_width = 40;
	int button_max_width = 10;

	hbox = gtk_hbox_new (FALSE, 6);
	gtk_widget_show (hbox);
	label1 = gtk_label_new (_("Sync in the Sync application"));
	gtk_label_set_max_width_chars (GTK_LABEL(label1), label_max_width);
	label2 = gtk_label_new (_("Sync"));
	gtk_label_set_max_width_chars (GTK_LABEL(label2), button_max_width);
	button = gtk_button_new ();
	gtk_container_add (GTK_CONTAINER(button), label2);
	g_object_set_data_full (G_OBJECT (button), "bdaddr", g_strdup (bdaddr), g_free);
	gtk_widget_show (label1);
	gtk_widget_show_all (button);
	gtk_box_pack_start (GTK_BOX(hbox), label1, FALSE, FALSE, 6);
	gtk_box_pack_end (GTK_BOX(hbox), button, FALSE, FALSE, 6);
	/* And set the signal */
	g_signal_connect (G_OBJECT (button), "clicked",
			  G_CALLBACK (button_clicked), NULL);

	return hbox;
}

static void
device_removed (const char *bdaddr)
{
	//nothing todo
}

static GbtPluginInfo plugin_info = {
	"SyncEvolution",
	has_config_widget,
	get_config_widgets,
	device_removed
};

GBT_INIT_PLUGIN(plugin_info)

