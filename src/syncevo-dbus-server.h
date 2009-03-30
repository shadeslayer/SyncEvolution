#ifndef __SYNCEVO_DBUS_SERVER_H
#define __SYNCEVO_DBUS_SERVER_H

#include <glib-object.h>
#include <dbus/dbus-glib.h>

typedef struct _SyncevoDBusServer SyncevoDBusServer;
#include "DBusSyncClient.h"

#define SYNCEVO_TYPE_DBUS_SERVER (syncevo_dbus_server_get_type ())
#define SYNCEVO_DBUS_SERVER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), SYNCEVO_TYPE_DBUS_SERVER, SyncevoDBusServerClass))

typedef struct _SyncevoDBusServer {
	GObject parent_object;

	DBusSyncClient *client;
	char *server;
	GPtrArray *sources;
	
	gboolean aborted;
} SyncevoDBusServer;

typedef struct _SyncevoDBusServerClass {
	GObjectClass parent_class;
	DBusGConnection *connection;
	
	/* DBus signals*/
	void (*progress) (SyncevoDBusServer *service,
	                  char *server,
	                  int type,
	                  int extra1, int extra2, int extra3);
	void (*source_progress) (SyncevoDBusServer *service,
	                         char *server,
	                         char *source,
	                         int type,
	                         int extra1, int extra2, int extra3);
	void (*server_message) (SyncevoDBusServer *service,
	                        char *server,
	                        char *message);
	void (*need_password) (SyncevoDBusServer *service,
	                       char *server);
} SyncevoDBusServerClass;

GType syncevo_dbus_server_get_type (void);

void
emit_progress (int type,
               int extra1,
               int extra2,
               int extra3, 
               gpointer data);

void
emit_source_progress (const char *source,
                      int type,
                      int extra1,
                      int extra2,
                      int extra3, 
                      gpointer data);

void
emit_server_message (const char *message, 
                     gpointer data);

char* 
need_password (const char *message, 
               gpointer data);

#endif // __SYNCEVO_DBUS_SERVER_H
