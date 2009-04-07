#ifndef __SYNCEVO_DBUS_TYPES_H__
#define __SYNCEVO_DBUS_TYPES_H__

#include <glib.h>
#include <dbus/dbus-glib.h>

enum SyncevoDBusError{
	SYNCEVO_DBUS_ERROR_GENERIC_ERROR = 1,
	SYNCEVO_DBUS_ERROR_NO_SUCH_SERVER = 2,
	SYNCEVO_DBUS_ERROR_MISSING_ARGS = 3,
};

#define SYNCEVO_SOURCE_TYPE (dbus_g_type_get_struct ("GValueArray", G_TYPE_STRING, G_TYPE_INT, G_TYPE_INVALID))
typedef GValueArray SyncevoSource;

#define SYNCEVO_OPTION_TYPE (dbus_g_type_get_struct ("GValueArray", G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID))
typedef GValueArray SyncevoOption;

#define SYNCEVO_SERVER_TYPE (dbus_g_type_get_struct ("GValueArray", G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID))
typedef GValueArray SyncevoServer;

SyncevoOption* syncevo_option_new (char *ns, char *key, char *value);
void syncevo_option_get (SyncevoOption *option, const char **ns, const char **key, const char **value);
void syncevo_option_free (SyncevoOption *option);

SyncevoSource* syncevo_source_new (char *name, int mode);
void syncevo_source_get (SyncevoSource *source, const char **name, int *mode);
void syncevo_source_free (SyncevoSource *source);

SyncevoServer* syncevo_server_new (char *name, char *url, char *icon);
void syncevo_server_get (SyncevoServer *server, const char **name, const char **url, const char **icon);
void syncevo_server_free (SyncevoServer *server);

#endif
