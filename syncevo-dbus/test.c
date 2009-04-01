/* test syncevo dbus */

#include <syncevo-dbus/syncevo-dbus.h>

enum TProgressEventEnum {
  /** some fatal aborting error */
  PEV_ERROR = 0,
  /** extra messages */
  PEV_MESSAGE = 1,
  /** extra error code */
  PEV_ERRCODE = 2,
  /** no extra message, just called to allow aborting */
  PEV_NOP = 3,
  /** called to signal main program, that caller would want to
      wait for extra1 milliseconds */
  PEV_WAIT = 4,
  /** called to allow debug interactions, extra1=code */
  PEV_DEBUG = 5,

  /* transport-related */
  
  PEV_SENDSTART = 6,
  PEV_SENDEND = 7,
  PEV_RECVSTART = 8,
  PEV_RECVEND = 9,
  /** expired */
  PEV_SSL_EXPIRED = 10,
  /** not completely trusted */
  PEV_SSL_NOTRUST = 11,
  /** sent periodically when waiting for network,
      allows application to check connection */
  PEV_CONNCHECK = 12,
  /** sent when client could initiate a explicit suspend */
  PEV_SUSPENDCHECK = 13,

  /* general */

  /** alert 100 received from remote, SessionKey's "displayalert" value contains message */
  PEV_DISPLAY100 = 14,

  /* session-related */
  
  PEV_SESSIONSTART = 15,
  /** session ended, probably with error in extra */
  PEV_SESSIONEND = 16,
  /* datastore-related */
  /** preparing (e.g. preflight in some clients), extra1=progress, extra2=total */
  PEV_PREPARING = 17,
  /** deleting (zapping datastore), extra1=progress, extra2=total */
  PEV_DELETING = 18,
  /** datastore alerted (extra1=0 for normal, 1 for slow, 2 for first time slow,
      extra2=1 for resumed session, extra3=syncmode: 0=twoway, 1=fromserver, 2=fromclient) */
  PEV_ALERTED = 19,
  /** sync started */
  PEV_SYNCSTART = 20,
  /** item received, extra1=current item count,
      extra2=number of expected changes (if >= 0) */
  PEV_ITEMRECEIVED = 21,
  /** item sent,     extra1=current item count,
      extra2=number of expected items to be sent (if >=0) */
  PEV_ITEMSENT = 22,
  /** item locally processed,               extra1=# added,
      extra2=# updated,
      extra3=# deleted */
  PEV_ITEMPROCESSED = 23,
  /** sync finished, probably with error in extra1 (0=ok),
      syncmode in extra2 (0=normal, 1=slow, 2=first time),
      extra3=1 for resumed session) */
  PEV_SYNCEND = 24,
  /** datastore statistics for local       (extra1=# added,
      extra2=# updated,
      extra3=# deleted) */
  PEV_DSSTATS_L = 25,
  /** datastore statistics for remote      (extra1=# added,
      extra2=# updated,
      extra3=# deleted) */
  PEV_DSSTATS_R = 26,
  /** datastore statistics for local/remote rejects (extra1=# locally rejected,
      extra2=# remotely rejected) */
  PEV_DSSTATS_E = 27,
  /** datastore statistics for server slowsync  (extra1=# slowsync matches) */
  PEV_DSSTATS_S = 28,
  /** datastore statistics for server conflicts (extra1=# server won,
      extra2=# client won,
      extra3=# duplicated) */
  PEV_DSSTATS_C = 29,
  /** datastore statistics for data   volume    (extra1=outgoing bytes,
      extra2=incoming bytes) */
  PEV_DSSTATS_D = 30,
  /** engine is in process of suspending */
  PEV_SUSPENDING = 31
};

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
             char *source,
             int type,
             int extra1, int extra2, int extra3,
             GMainLoop *loop)
{
    char *mode, *speed;
    int percent;

    switch(type) {
    case PEV_SESSIONSTART:
        g_debug ("  progress: %s: session start", server);
        break;
    case PEV_SESSIONEND:
        g_debug ("  progress: %s: session end", server);
        g_main_loop_quit(loop);
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
        if(extra1 == 0) 
            g_debug ("  source progress: %s/%s: sync finished", server, source);
        else
            g_debug ("  source progress: %s/%s: sync finished with error %d", server, source, extra1);
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
    GPtrArray *options;
    char **servers;
    char **ptr;
    char *server = NULL;

    g_type_init();

    if (argc > 1) {
        server = argv[1];
    }

    service = syncevo_service_get_default ();

    g_print ("Testing syncevo_service_get_servers() ");
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
    g_print ("Testing syncevo_service_get_config() with server %s\n", server);
    syncevo_service_get_server_config (service, server, &options, &error);
    if (error) {
        g_error ("  syncevo_service_get_server_config() failed with %s", error->message);
    }
    g_ptr_array_foreach (options, (GFunc)print_option, NULL);

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
