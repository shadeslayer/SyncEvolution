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

/* test syncevo dbus */

#include "syncevo-dbus.h"

#include <synthesis/syerror.h>
#include <synthesis/engine_defs.h>

static void
print_option (SyncevoOption *option, gpointer userdata)
{
	const char *ns, *key, *value;

	syncevo_option_get (option, &ns, &key, &value);
	g_debug ("  Got option [%s] %s = %s", ns, key, value);
}

static void
print_server (SyncevoServer *temp, gpointer userdata)
{
	const char *name, *url, *icon;
	gboolean ready;

	syncevo_server_get (temp, &name, &url, &icon, &ready);
	g_debug ("  Got server %s (%s, %s, %sconsumer ready)",
	         name, url, icon,
	         ready ? "" : "non-");
}

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

int main (int argc, char *argv[])
{
    SyncevoService *service;
    GMainLoop *loop;
    GPtrArray *sources;
    GError *error = NULL;
    GPtrArray *array;
    char *server = NULL;

    g_type_init();

    if (argc > 1) {
        server = argv[1];
    }

    service = syncevo_service_get_default ();

    array = g_ptr_array_new();
    g_print ("Testing syncevo_service_get_servers()\n");
    syncevo_service_get_servers (service, &array, &error);
    if (error) {
        g_error ("  syncevo_service_get_servers() failed with %s", error->message);
    }
    g_ptr_array_foreach (array, (GFunc)print_server, NULL);

    array = g_ptr_array_new();
    g_print ("Testing syncevo_service_get_templates()\n");
    syncevo_service_get_templates (service, &array, &error);
    if (error) {
        g_error ("  syncevo_service_get_templates() failed with %s", error->message);
    }
    g_ptr_array_foreach (array, (GFunc)print_server, NULL);
    

    if (!server) {
        g_print ("No server given, stopping here\n");
        return 0;
    }


    array = g_ptr_array_new();
    g_print ("Testing syncevo_service_get_config() with server %s\n", server);
    syncevo_service_get_server_config (service, server, &array, &error);
    if (error) {
        g_error ("  syncevo_service_get_server_config() failed with %s", error->message);
    }
    g_ptr_array_foreach (array, (GFunc)print_option, NULL);

    loop = g_main_loop_new (NULL, TRUE);
    g_signal_connect (service, "progress", (GCallback)progress_cb, loop);
    
    g_print ("Testing syncevo_service_start_sync() with server %s\n", server);
    sources = g_ptr_array_new (); /*empty*/
    syncevo_service_start_sync (service, 
                                server,
                                sources,
                                &error);
    if (error) {
        g_error ("  syncevo_service_start_sync() failed with %s", error->message);
    }

    g_main_loop_run (loop);

    return 0;
}
