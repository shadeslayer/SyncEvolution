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
#include "sync-ui.h"

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

        g_free (*str);
        *str = g_strdup (new_str);
    }
}

static void
add_source_config (char *name,
                   GHashTable *syncevo_source_config,
                   GHashTable *source_configs)
{
    source_config *new_conf;

    new_conf = g_slice_new0 (source_config);
    new_conf->name = name;
    new_conf->supported_locally = TRUE;
    new_conf->stats_set = FALSE;
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
    if (!syncevo_config_get_value (config, NULL, "PeerName", &server->pretty_name)) {
        server->pretty_name = server->name;
    }
}

gboolean
source_config_is_usable (source_config *source)
{
    const char *source_uri;

    source_uri = g_hash_table_lookup (source->config, "uri");

    if (!source_config_is_enabled (source) ||
        !source_uri ||
        strlen (source_uri) == 0 ||
        !source->supported_locally) {
        return FALSE;
    }
    return TRUE;
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

gboolean
peer_is_client (SyncevoConfig *config)
{
    char *is_client;

    g_return_val_if_fail (config, FALSE);

    syncevo_config_get_value (config, NULL, "PeerIsClient", &is_client);
    return is_client && g_strcmp0 ("1", is_client) == 0;
}
