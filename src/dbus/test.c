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

/* test syncevo dbus client wrappers */

#include "syncevo-server.h"

#include <synthesis/syerror.h>
#include <synthesis/engine_defs.h>

/*
// This is the old progress callback, here for future reference...
static void
progress_cb (SyncevoService *service,
             char *server,
             char *source,
             int type,
             int extra1, int extra2, int extra3,
             GMainLoop *loop)
{
    char *mode, *speed;
    int percent;

    switch(type) {
    case -1:
        g_print ("Finished syncing %s with return value %d\n", server, extra1);
        g_main_loop_quit (loop);
        break;
    case PEV_SESSIONSTART:
        g_debug ("  progress: %s: session start", server);
        break;
    case PEV_SESSIONEND:
        g_debug ("  progress: %s: session end", server);
        break;
    case PEV_SENDSTART:
        g_debug ("  progress: %s: send start", server);
        break;
    case PEV_SENDEND:
        g_debug ("  progress: %s: send end", server);
        break;
    case PEV_RECVSTART:
        g_debug ("  progress: %s: receive start", server);
        break;
    case PEV_RECVEND:
        g_debug ("  progress: %s: receive end", server);
        break;

    case PEV_ALERTED:
        switch (extra1) {
            case 0: speed = ""; break;
            case 1: speed = "slow "; break;
            case 2: speed = "first time slow "; break;
            default: g_assert_not_reached();
        }
        switch (extra3) {
            case 0: mode = "two-way"; break;
            case 1: mode = "from server"; break;
            case 2: mode = "from client"; break;
            default: g_assert_not_reached();
        }
        g_debug ("  source progress: %s/%s: alert (%s%s)", server, source, speed, mode);
        break;
    case PEV_PREPARING:
        percent = CLAMP (100 * extra1 / extra2, 0, 100);
        g_debug ("  source progress: %s/%s: preparing (%d%%)", server, source, percent);
        break;
    case PEV_ITEMSENT:
        percent = CLAMP (100 * extra1 / extra2, 0, 100);
        g_debug ("  source progress: %s/%s: item sent (%d%%)", server, source, percent);
        break;
    case PEV_ITEMRECEIVED:
        percent = CLAMP (100 * extra1 / extra2, 0, 100);
        g_debug ("  source progress: %s/%s: item received (%d%%)", server, source, percent);
        break;
    case PEV_ITEMPROCESSED:
        g_debug ("  source progress: %s/%s: item processed (added %d, updated %d, deleted %d)", server, source, extra1, extra2, extra3);
        break;
    case PEV_SYNCSTART:
        g_debug ("  source progress: %s/%s: sync started", server, source);
        break;
    case PEV_SYNCEND:
        switch (extra1) {
        case 0:
            g_debug ("  source progress: %s/%s: sync finished", server, source);
            break;
        case LOCERR_USERABORT:
            g_debug ("  source progress: %s/%s: sync aborted by user", server, source);
            break;
        case LOCERR_USERSUSPEND:
            g_debug ("  source progress: %s/%s: sync suspended by user", server, source);
            break;
        default: 
            g_debug ("  source progress: %s/%s: sync finished with error %d", server, source, extra1);
        }
        break;

    default:
        if(source)
            g_debug ("  source progress: %s/%s: unknown type (%d)", server, source, type);
        else
            g_debug ("  progress: %s: unknown type (%d)", server, type);
        g_debug ("            %d, %d, %d", extra1, extra2, extra3);
    }
}

*/

static void
get_template_configs_cb(SyncevoServer *server,
               char **config_names,
               GError *error,
               gpointer userdata)
{
    char **name;
    
    if (error) {
        g_printerr ("GetConfigs error: %s", error->message);
        g_error_free (error);
        return;
    }
    g_print ("GetConfigs (template=TRUE):\n");

    for (name = config_names; name && *name; *name++){
        g_print ("\t%s\n", *name);
    }
    g_print ("\n");
}

static void
print_config_value (char *key, char *value, gpointer data)
{
    g_print ("\t\t%s = %s\n", key, value);
}
static void
print_config (char *key, GHashTable *sourceconfig, gpointer data)
{
    g_print ("\tsource = %s\n", key);
    g_hash_table_foreach (sourceconfig, (GHFunc)print_config_value, NULL);
}

static void
get_config_cb (SyncevoSession *session,
               SyncevoConfig *config,
               GError *error,
               gpointer userdata)
{
    if (error) {
        g_printerr ("GetConfig error: %s\n", error->message);
        g_error_free (error);
        return;
    }

    g_print ("Session configuration:\n");
    g_hash_table_foreach (config, (GHFunc)print_config, NULL);
}

static void
start_session_cb (SyncevoServer *server,
                  SyncevoSession *session,
                  GError *error,
                  gpointer userdata)
{

    if (error) {
        g_printerr ("StartSession error: %s\n", error->message);
        g_error_free (error);
        return;
    }

    g_print ("Testing Session...\n");

    syncevo_session_get_config (session,
                                FALSE,
                                (SyncevoSessionGetConfigCb)get_config_cb,
                                NULL);
}


static void
get_configs_cb(SyncevoServer *server,
               char **config_names,
               GError *error,
               char *service_name)
{
    char **name;

    if (error) {
        g_printerr ("GetConfigs error: %s\n", error->message);
        g_error_free (error);
        return;
    }
    g_print ("GetConfigs (template=FALSE):\n");

    for (name = config_names; name && *name; *name++){
        g_print ("\t%s\n", *name);
    }
    g_print ("\n");
}


int main (int argc, char *argv[])
{
    SyncevoServer *server;
    char *service = NULL;
    GMainLoop *loop;

    g_type_init();

    if (argc > 1) {
        service = argv[1];
    }

    g_print ("Testing Server...\n");

    server = syncevo_server_get_default ();

    syncevo_server_get_configs (server,
                                TRUE,
                                (SyncevoServerGetConfigsCb)get_template_configs_cb,
                                NULL);
    syncevo_server_get_configs (server,
                                FALSE,
                                (SyncevoServerGetConfigsCb)get_configs_cb,
                                NULL);

    if (service)
        syncevo_server_start_session (server,
                                      service,
                                      (SyncevoServerStartSessionCb)start_session_cb,
                                      NULL);


    loop = g_main_loop_new (NULL, TRUE);
    g_main_loop_run (loop);

    return 0;
}
