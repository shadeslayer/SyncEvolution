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

#include <string.h>
#include "sync-ui-config.h"

void
source_config_free (source_config *source)
{
    if (!source)
        return;

    g_free (source->name);
    g_free (source->uri);
    g_slice_free (source_config, source);
}

void
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

        if (*str == server->password) {
            server->auth_changed = TRUE;
            server->password_changed = TRUE;
        } else if (*str == server->username ||
                   *str == server->base_url) {
            server->auth_changed = TRUE;
        }

        g_free (*str);
        *str = g_strdup (new_str);
    }
}

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
            server->username = g_strdup (value);
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
                /* consider this source not available at all */
                source->enabled = FALSE;
            } else {
                source->enabled = TRUE;
            }
        } else if (strcmp (key, "localDB") == 0) {
            source->supported_locally = (strcmp (value, "1") == 0);
        }
    }
}

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

    /* if gnome-keyring password was set, set password option to "-"
     * (meaning 'use AskPassword()'). Otherwise don't touch the password */
    if (server->password_changed) {
        option = syncevo_option_new (NULL, g_strdup ("password"), g_strdup ("-"));
        g_ptr_array_add (options, option);
    }

    for (l = server->source_configs; l; l = l->next) {
        source_config *source = (source_config*)l->data;

        /* sources may have been added as place holders */
        if (!source->uri)
            continue;

        option = syncevo_option_new (source->name, g_strdup ("uri"), g_strdup (source->uri));
        g_ptr_array_add (options, option);

        option = syncevo_option_new (source->name, g_strdup ("sync"), 
                                     source->enabled ? g_strdup ("two-way") : g_strdup ("none"));
        g_ptr_array_add (options, option);
    }

    return options;
}

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

void
server_config_disable_unsupported_sources (server_config *server)
{
    GList *l;

    for (l = server->source_configs; l; l = l->next) {
        source_config* config = (source_config*)l->data;

        if (!config->supported_locally) {
            config->enabled = FALSE;
        }
    }
}



static int
source_config_compare (source_config *a, source_config *b)
{
    g_assert (a && a->name);
    g_assert (b && b->name);

    return strcmp (a->name, b->name);
}

source_config*
server_config_get_source_config (server_config *server, const char *name)
{
    GList *l;
    source_config *source = NULL;
    
    g_assert (name);
    
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
    server->source_configs = g_list_insert_sorted (server->source_configs, 
                                                   source,
                                                   (GCompareFunc)source_config_compare);

    return source;
}
