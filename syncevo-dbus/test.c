/* test program for the C wrapper for the SyncEvolution DBus interface */

#include "syncevo-dbus.h"


void
progress_cb (SyncevoService *service, char *server, int type, int extra1, int extra2, int extra3)
{
	g_print ("Got signal 'progress': %s: %d %d %d %d\n",
	         server, type, extra1, extra2, extra3);
}

int main ()
{
	SyncevoService *service;
	SyncevoSource *source;
	GPtrArray *sources;
	GMainLoop *loop;
	GError *error = NULL;
	char* source_name = "calendar"; 
	int source_mode = 1;


	g_type_init ();
	
	service = syncevo_service_get_default();

	/*for some reason the dbus type system fails unless a connection has been created... 
	  workaround: create SyncEvoService first */
	source = syncevo_source_new (source_name, source_mode);
	sources = g_ptr_array_new ();
	g_ptr_array_add (sources, source);

	g_signal_connect (service, "progress", 
	                  G_CALLBACK (progress_cb), NULL);
	if (!syncevo_service_start_sync (service, "scheduleworld", sources, &error)) {
		g_printerr ("StartSync failed :%s\n", error->message);
		g_error_free (error);
	} else {
		g_print ("StartSync ok, waiting for signals\n");
		loop = g_main_loop_new (NULL, FALSE);
		g_main_loop_run (loop);
		g_main_loop_unref (loop);
	}

	g_ptr_array_free (sources, FALSE);
	syncevo_source_free (source);
	g_object_unref (service);
}
