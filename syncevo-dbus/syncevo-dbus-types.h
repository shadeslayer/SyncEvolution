#ifndef __SYNCEVO_DBUS_TYPES_H__
#define __SYNCEVO_DBUS_TYPES_H__

#include <glib.h>
#include <dbus/dbus-glib.h>

#define SYNCEVO_SOURCE_TYPE (dbus_g_type_get_struct ("GValueArray", G_TYPE_STRING, G_TYPE_INT, G_TYPE_INVALID))
typedef GValueArray SyncevoSource;

#define SYNCEVO_OPTION_TYPE (dbus_g_type_get_struct ("GValueArray", G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID))
typedef GValueArray SyncevoOption;

#define SYNCEVO_TEMPLATE_TYPE (dbus_g_type_get_struct ("GValueArray", G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID))
typedef GValueArray SyncevoTemplate;

SyncevoOption* syncevo_option_new (char *ns, char *key, char *value);
void syncevo_option_get (SyncevoOption *option, const char **ns, const char **key, const char **value);
void syncevo_option_free (SyncevoOption *option);

SyncevoSource* syncevo_source_new (char *name, int mode);
void syncevo_source_get (SyncevoSource *source, const char **name, int *mode);
void syncevo_source_free (SyncevoSource *source);

SyncevoTemplate* syncevo_template_new (char *name, char *note);
void syncevo_template_get (SyncevoTemplate *template, const char **name, const char **note);
void syncevo_template_free (SyncevoTemplate *template);

#endif
