#include "syncevo-dbus-types.h"

SyncevoSource*
syncevo_source_new (char *name, int mode)
{
	GValue val = {0, };

	g_value_init (&val, SYNCEVO_SOURCE_TYPE);
	g_value_take_boxed (&val, dbus_g_type_specialized_construct (SYNCEVO_SOURCE_TYPE));
	dbus_g_type_struct_set (&val, 0, name, 1, mode, G_MAXUINT);

	return (SyncevoSource*) g_value_get_boxed (&val);
}

void
syncevo_source_get (SyncevoSource *source, const char **name, int *mode)
{
	g_assert (source);

	if (name) {
		*name = g_value_get_string (g_value_array_get_nth (source, 0));
	}
	if (mode) {
		*mode = g_value_get_int (g_value_array_get_nth (source, 1));
	}
}

void
syncevo_source_free (SyncevoSource *source)
{
	if (source) {
		g_boxed_free (SYNCEVO_SOURCE_TYPE, source);
	}
}

SyncevoOption*
syncevo_option_new (char *ns, char *key, char *value)
{
	GValue val = {0, };

	g_value_init (&val, SYNCEVO_OPTION_TYPE);
	g_value_take_boxed (&val, dbus_g_type_specialized_construct (SYNCEVO_OPTION_TYPE));
	dbus_g_type_struct_set (&val, 0, ns, 1, key, 2, value, G_MAXUINT);

	return (SyncevoOption*) g_value_get_boxed (&val);
}

void
syncevo_option_get (SyncevoOption *option, const char **ns, const char **key, const char **value)
{
	g_assert (option);

	if (ns) {
		*ns = g_value_get_string (g_value_array_get_nth (option, 0));
	}
	if (key) {
		*key = g_value_get_string (g_value_array_get_nth (option, 1));
	}
	if (value) {
		*value = g_value_get_string (g_value_array_get_nth (option, 2));
	}
}

void
syncevo_option_free (SyncevoOption *option)
{
	if (option) {
		g_boxed_free (SYNCEVO_OPTION_TYPE, option);
	}
}

SyncevoServer* syncevo_server_new (char *name, char *url, char *icon)
{
	GValue val = {0, };

	g_value_init (&val, SYNCEVO_SERVER_TYPE);
	g_value_take_boxed (&val, dbus_g_type_specialized_construct (SYNCEVO_SERVER_TYPE));
	dbus_g_type_struct_set (&val, 0, name, 1, url, 2, icon, G_MAXUINT);

	return (SyncevoServer*) g_value_get_boxed (&val);
}

void syncevo_server_get (SyncevoServer *server, const char **name, const char **url, const char **icon)
{
	g_assert (server);

	if (name) {
		*name = g_value_get_string (g_value_array_get_nth (server, 0));
	}
	if (url) {
		*url = g_value_get_string (g_value_array_get_nth (server, 1));
	}
	if (icon) {
		*icon = g_value_get_string (g_value_array_get_nth (server, 2));
	}
}

void syncevo_server_free (SyncevoServer *server)
{
	if (server) {
		g_boxed_free (SYNCEVO_SERVER_TYPE, server);
	}
}
