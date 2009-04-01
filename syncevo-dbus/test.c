/* test syncevo dbus */

#include <syncevo-dbus/syncevo-dbus.h>

static void
print_option (SyncevoOption *option, gpointer userdata)
{
	const char *ns, *key, *value;

	syncevo_option_get (option, &ns, &key, &value);
	g_debug ("  Got option [%s] %s = %s", ns, key, value);
}

static void
progress_cb (SyncevoService *service,
             char *server,
             int type,
             int extra1, int extra2, int extra3)
{
    g_debug ("  progress...");
}

static void
source_progress_cb (SyncevoService *service,
                    char *server,
                    char *source,
                    int type,
                    int extra1, int extra2, int extra3)
{
    g_debug ("  source progress...");
}

int main (int argc, char *argv[])
{
    SyncevoService *service;
    GMainLoop *loop;
    GPtrArray *sources;
    GError *error = NULL;
    GPtrArray *options;
    char **servers;
    char **ptr;
    char *server = NULL;

    g_type_init();

    if (argc > 1) {
        server = argv[1];
    }

    service = syncevo_service_get_default ();

    g_debug ("Testing syncevo_service_get_servers() ");
    syncevo_service_get_servers (service, &servers, &error);
    if (error) {
        g_error ("  syncevo_service_get_servers() failed with %s", error->message);
    }
    
    for (ptr = servers; *ptr; ptr++) {
        g_debug ("  Got server '%s'", *ptr);
    }

    if (!server) {
        g_print ("No server given, stopping here\n");
        return 0;
    }
    
    options = g_ptr_array_new();
    g_debug ("Testing syncevo_service_get_config() with server %s", server);
    syncevo_service_get_server_config (service, server, &options, &error);
    if (error) {
        g_error ("  syncevo_service_get_server_config() failed with %s", error->message);
    }
    g_ptr_array_foreach (options, (GFunc)print_option, NULL);

    loop = g_main_loop_new (NULL, TRUE);
    g_signal_connect (service, "progress", (GCallback)progress_cb, NULL);
    g_signal_connect (service, "source-progress", (GCallback)source_progress_cb, NULL);
    
    g_debug ("Testing syncevo_service_start_sync() with server %s", server);
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
