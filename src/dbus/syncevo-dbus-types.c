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
	g_return_if_fail (source);

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
	g_return_if_fail (option);

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
	g_return_if_fail (server);

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

SyncevoReport* 
syncevo_report_new (char *source)
{
	GValue val = {0, };

	g_value_init (&val, SYNCEVO_REPORT_TYPE);
	g_value_take_boxed (&val, dbus_g_type_specialized_construct (SYNCEVO_REPORT_TYPE));
	dbus_g_type_struct_set (&val,
	                        0, source,
	                        G_MAXUINT);

	return (SyncevoReport*) g_value_get_boxed (&val);
}

static void
insert_int (SyncevoReport *report, int index, int value)
{
	GValue val = {0};

	g_value_init (&val, G_TYPE_INT);
	g_value_set_int (&val, value);
	g_value_array_insert (report, index, &val);
}

void
syncevo_report_set_io (SyncevoReport *report, 
                       int sent_bytes, int received_bytes)
{
	g_return_if_fail (report);

	insert_int (report, 1, sent_bytes);
	insert_int (report, 2, received_bytes);
}


void 
syncevo_report_set_local (SyncevoReport *report, 
                          int adds, int updates, int removes, int rejects)
{
	g_return_if_fail (report);

	insert_int (report, 3, adds);
	insert_int (report, 4, updates);
	insert_int (report, 5, removes);
	insert_int (report, 6, rejects);
}

void
syncevo_report_set_remote (SyncevoReport *report, 
                           int adds, int updates, int removes, int rejects)
{
	g_return_if_fail (report);

	insert_int (report, 7, adds);
	insert_int (report, 8, updates);
	insert_int (report, 9, removes);
	insert_int (report, 10, rejects);
}

void
syncevo_report_set_conflicts (SyncevoReport *report, 
                              int local_won, int remote_won, int duplicated)
{
	g_return_if_fail (report);

	insert_int (report, 11, local_won);
	insert_int (report, 12, remote_won);
	insert_int (report, 13, duplicated);
}

const char*
syncevo_report_get_name (SyncevoReport *report)
{
	g_return_val_if_fail (report, NULL);

	return g_value_get_string (g_value_array_get_nth (report, 0));

}

void
syncevo_report_get_io (SyncevoReport *report, 
                       int *bytes_sent, int *bytes_received)
{
	g_return_if_fail (report);

	if (bytes_sent) {
		*bytes_sent = g_value_get_int (g_value_array_get_nth (report, 1));
	}
	if (bytes_received) {
		*bytes_received = g_value_get_int (g_value_array_get_nth (report, 2));
	}
}

void
syncevo_report_get_local (SyncevoReport *report, 
                          int *adds, int *updates, int *removes, int *rejects)
{
	g_return_if_fail (report);

	if (adds) {
		*adds = g_value_get_int (g_value_array_get_nth (report, 3));
	}
	if (updates) {
		*updates = g_value_get_int (g_value_array_get_nth (report, 4));
	}
	if (removes) {
		*removes = g_value_get_int (g_value_array_get_nth (report, 5));
	}
	if (rejects) {
		*rejects = g_value_get_int (g_value_array_get_nth (report, 6));
	}
}

void
syncevo_report_get_remote (SyncevoReport *report, 
                           int *adds, int *updates, int *removes, int *rejects)
{
	g_return_if_fail (report);

	if (adds) {
		*adds = g_value_get_int (g_value_array_get_nth (report, 7));
	}
	if (updates) {
		*updates = g_value_get_int (g_value_array_get_nth (report, 8));
	}
	if (removes) {
		*removes = g_value_get_int (g_value_array_get_nth (report, 9));
	}
	if (rejects) {
		*rejects = g_value_get_int (g_value_array_get_nth (report, 10));
	}
}

void
syncevo_report_get_conflicts (SyncevoReport *report, 
                              int *local_won, int *remote_won, int *duplicated)
{
	g_return_if_fail (report);

	if (local_won) {
		*local_won = g_value_get_int (g_value_array_get_nth (report, 11));
	}
	if (remote_won) {
		*remote_won = g_value_get_int (g_value_array_get_nth (report, 12));
	}
	if (duplicated) {
		*duplicated = g_value_get_int (g_value_array_get_nth (report, 13));
	}
}

void
syncevo_report_free (SyncevoReport *report)
{
	if (report) {
		g_boxed_free (SYNCEVO_REPORT_TYPE, report);
	}
}

SyncevoReportArray* syncevo_report_array_new (int end_time, GPtrArray *reports)
{
	GValue val = {0, };

	g_value_init (&val, SYNCEVO_REPORT_ARRAY_TYPE);
	g_value_take_boxed (&val, dbus_g_type_specialized_construct (SYNCEVO_REPORT_ARRAY_TYPE));
	dbus_g_type_struct_set (&val,
	                        0, end_time,
	                        1, reports,
	                        G_MAXUINT);
	return (SyncevoReportArray*) g_value_get_boxed (&val);
}

void syncevo_report_array_get (SyncevoReportArray *array, int *end_time, GPtrArray **reports)
{
	g_return_if_fail (array);

	if (end_time) {
		*end_time = g_value_get_int (g_value_array_get_nth (array, 0));
	}
	if (reports) {
		*reports = g_value_get_boxed (g_value_array_get_nth (array, 1));
	}
}

void
syncevo_report_array_free (SyncevoReportArray *array)
{
	if (array) {
		g_boxed_free (SYNCEVO_REPORT_ARRAY_TYPE, array);
	}
}

