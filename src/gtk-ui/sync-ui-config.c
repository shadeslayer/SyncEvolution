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

#include <glib/gi18n.h>

#include <string.h>
#include "sync-ui-config.h"

void
server_config_free (server_config *server)
{
    if (!server)
        return;

    g_free (server->name);
    syncevo_config_free (server->config);

    g_slice_free (server_config, server);
}

void
server_config_update_from_entry (server_config *server, GtkEntry *entry)
{
    char **str;
    const char *new_str;

    /* all entries have a pointer to the correct string in server_config */
    str = g_object_get_data (G_OBJECT (entry), "value");
    g_assert (str);
    new_str = gtk_entry_get_text (entry);

    if ((*str == NULL && strlen (new_str) != 0) ||
        (*str != NULL && strcmp (*str, new_str) != 0)) {

        server->changed = TRUE;

/*
        if (*str == server->password) {
            server->auth_changed = TRUE;
            server->password_changed = TRUE;
        } else if (*str == server->username ||
                   *str == server->base_url) {
            server->auth_changed = TRUE;
        }
*/
        g_free (*str);
        *str = g_strdup (new_str);
    }
}

/*
void
server_config_update_from_option (server_config *server, SyncevoOption *option)
{
    const char *ns, *key, *value;

    syncevo_option_get (option, &ns, &key, &value);

    if (!ns || strlen (ns) == 0) {
        if (strcmp (key, "syncURL") == 0) {
            g_free (server->base_url);
            server->base_url = g_strdup (value);
        } else if (strcmp (key, "username") == 0) {
            g_free (server->username);
            //skip the informative username
            if (value &&
                strcmp (value, "your SyncML server account name") == 0) {
                server->username = g_strdup ("");
            } else {
                server->username = g_strdup (value);
            }
        } else if (strcmp (key, "webURL") == 0) {
            if (server->web_url)
            g_free (server->web_url);
            server->web_url = g_strdup (value);
        } else if (strcmp (key, "iconURI") == 0) {
            g_free (server->icon_uri);
            server->icon_uri = g_strdup (value);
        } else if (strcmp (key, "fromTemplate") == 0) {
            server->from_template = (strcmp (value, "yes") == 0);
        }
    } else {
        source_config *source;
        
        source = server_config_get_source_config (server, ns);
        
        if (strcmp (key, "uri") == 0) {
            g_free (source->uri);
            source->uri = g_strdup (value);
        } else if (strcmp (key, "sync") == 0) {
            if (strcmp (value, "disabled") == 0 ||
                 strcmp (value, "none") == 0) {
                // consider this source not available at all
                source->enabled = FALSE;
            } else {
                source->enabled = TRUE;
            }
        } else if (strcmp (key, "localDB") == 0) {
            source->supported_locally = (strcmp (value, "1") == 0);
        }
    }
}
*/

/*
GPtrArray*
server_config_get_option_array (server_config *server)
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
    option = syncevo_option_new (NULL, g_strdup ("webURL"), g_strdup (server->web_url));
    g_ptr_array_add (options, option);
    option = syncevo_option_new (NULL, g_strdup ("iconURI"), g_strdup (server->icon_uri));
    g_ptr_array_add (options, option);

    // if gnome-keyring password was set, set password option to "-"
    //(meaning 'use AskPassword()'). Otherwise don't touch the password
    if (server->password_changed) {
        option = syncevo_option_new (NULL, g_strdup ("password"), g_strdup ("-"));
        g_ptr_array_add (options, option);
    }

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
*/

/*
GPtrArray*
server_config_get_source_array (server_config *server, SyncMode mode)
{
    GList *l;
    GPtrArray *sources;
    sources = g_ptr_array_new ();

    for (l = server->source_configs; l; l = l->next) {
        SyncevoSource *src;
        source_config* config = (source_config*)l->data;
        
        if (config->enabled && config->supported_locally) {
            src = syncevo_source_new (g_strdup (config->name), mode);
            g_ptr_array_add (sources, src);
        }
    }

    return sources;
}
*/

static void
add_source_config (char *name,
                   GHashTable *syncevo_source_config,
                   GHashTable *source_configs)
{
    source_config *new_conf;

    new_conf = g_slice_new (source_config);
    new_conf->name = name;
    new_conf->supported_locally = TRUE;
    new_conf->config = syncevo_source_config;

    g_hash_table_insert (source_configs, name, new_conf);
}

void
server_config_init (server_config *server, SyncevoConfig *config)
{
    server->config = config;

    /* build source_configs */
    server->source_configs = g_hash_table_new (g_str_hash, g_str_equal);
    syncevo_config_foreach_source (config,
                                   (ConfigFunc)add_source_config,
                                   server->source_configs);

}

static char*
get_report_summary (int local_changes, int remote_changes, int local_rejects, int remote_rejects)
{
    char *rejects, *changes, *msg;

    if (local_rejects + remote_rejects == 0) {
        rejects = NULL;
    } else if (local_rejects == 0) {
        rejects = g_strdup_printf (ngettext ("There was one remote rejection.", 
                                             "There were %d remote rejections.",
                                             remote_rejects),
                                   remote_rejects);
    } else if (remote_rejects == 0) {
        rejects = g_strdup_printf (ngettext ("There was one local rejection.", 
                                             "There were %d local rejections.",
                                             local_rejects),
                                   local_rejects);
    } else {
        rejects = g_strdup_printf (_ ("There were %d local rejections and %d remote rejections."),
                                   local_rejects, remote_rejects);
    }

    if (local_changes + remote_changes == 0) {
        changes = g_strdup_printf (_("Last time: No changes."));
    } else if (local_changes == 0) {
        changes = g_strdup_printf (ngettext ("Last time: Sent one change.",
                                             "Last time: Sent %d changes.",
                                             remote_changes),
                                   remote_changes);
    } else if (remote_changes == 0) {
        // This is about changes made to the local data. Not all of these
        // changes were requested by the remote server, so "applied"
        // is a better word than "received" (bug #5185).
        changes = g_strdup_printf (ngettext ("Last time: Applied one change.",
                                             "Last time: Applied %d changes.",
                                             local_changes),
                                   local_changes);
    } else {
        changes = g_strdup_printf (_("Last time: Applied %d changes and sent %d changes."),
                                   local_changes, remote_changes);
    }

    if (rejects)
        msg = g_strdup_printf ("%s\n%s", changes, rejects);
    else
        msg = g_strdup (changes);
    g_free (rejects);
    g_free (changes);
    return msg;
}

void
source_config_update_label (source_config *source)
{
    char *msg;

    if (!source->label) {
        return;
    }

    msg = get_report_summary (source->local_changes,
                              source->remote_changes,
                              source->local_rejections,
                              source->remote_rejections);
    gtk_label_set_text (GTK_LABEL (source->label), msg);
    g_free (msg);
}

gboolean
source_config_is_enabled (source_config *source)
{
    char *mode;

    mode = g_hash_table_lookup (source->config, "sync");

    if (mode &&
        (strcmp (mode, "none") == 0 ||
         strcmp (mode, "disabled") == 0)) {
        return FALSE;
    }
    return TRUE;
}

server_data*
server_data_new (const char *name, gpointer *data)
{
    server_data *serv_data;

    serv_data = g_slice_new0 (server_data);
    serv_data->data = data;
    serv_data->config = g_slice_new0 (server_config);
    serv_data->config->name = g_strdup (name);

    return serv_data;
}

void
server_data_free (server_data *data, gboolean free_config)
{
    if (!data)
        return;

    if (free_config && data->config) {
        server_config_free (data->config);
    }
    if (data->options_override) {
/*
        g_ptr_array_foreach (data->options_override, (GFunc)syncevo_option_free, NULL);
*/
        g_ptr_array_free (data->options_override, TRUE);
    }
    g_slice_free (server_data, data);
}
