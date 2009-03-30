#ifndef __SYNCEVO_DBUS_SERVER_H
#define __SYNCEVO_DBUS_SERVER_H

#include <glib-object.h>
#include <dbus/dbus-glib.h>

#define SYNCEVO_TYPE_DBUS_SERVER (syncevo_dbus_server_get_type ())
#define SYNCEVO_DBUS_SERVER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), SYNCEVO_TYPE_DBUS_SERVER, SyncevoDBusServerClass))

typedef struct _SyncevoDBusServer {
	GObject parent_object;
} SyncevoDBusServer;

typedef struct _SyncevoDBusServerClass {
	GObjectClass parent_class;
	DBusGConnection *connection;
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


#endif // __SYNCEVO_DBUS_SERVER_H
