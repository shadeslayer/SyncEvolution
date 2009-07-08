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

#ifndef __SYNCEVO_DBUS_TYPES_H__
#define __SYNCEVO_DBUS_TYPES_H__

#include <glib.h>
#include <dbus/dbus-glib.h>

#define SYNCEVO_DBUS_ERROR_GENERIC_ERROR "org.Moblin.SyncEvolution.GenericError"
#define SYNCEVO_DBUS_ERROR_NO_SUCH_SERVER "org.Moblin.SyncEvolution.NoSuchServer"
#define SYNCEVO_DBUS_ERROR_MISSING_ARGS "org.Moblin.SyncEvolution.MissingArgs"
#define SYNCEVO_DBUS_ERROR_INVALID_CALL "org.Moblin.SyncEvolution.InvalidCall"

#define SYNCEVO_SOURCE_TYPE (dbus_g_type_get_struct ("GValueArray", G_TYPE_STRING, G_TYPE_INT, G_TYPE_INVALID))
typedef GValueArray SyncevoSource;

#define SYNCEVO_OPTION_TYPE (dbus_g_type_get_struct ("GValueArray", G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID))
typedef GValueArray SyncevoOption;

#define SYNCEVO_SERVER_TYPE (dbus_g_type_get_struct ("GValueArray", G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_INVALID))
typedef GValueArray SyncevoServer;

#define SYNCEVO_REPORT_TYPE (dbus_g_type_get_struct ("GValueArray", G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INVALID))
typedef GValueArray SyncevoReport;

#define SYNCEVO_REPORT_ARRAY_TYPE (dbus_g_type_get_struct ("GValueArray", G_TYPE_INT, dbus_g_type_get_collection ("GPtrArray", SYNCEVO_REPORT_TYPE)))
typedef GValueArray SyncevoReportArray;

SyncevoOption* syncevo_option_new (char *ns, char *key, char *value);
void syncevo_option_get (SyncevoOption *option, const char **ns, const char **key, const char **value);
void syncevo_option_free (SyncevoOption *option);

SyncevoSource* syncevo_source_new (char *name, int mode);
void syncevo_source_get (SyncevoSource *source, const char **name, int *mode);
void syncevo_source_free (SyncevoSource *source);

SyncevoServer* syncevo_server_new (char *name, char *url, char *icon, gboolean consumer_ready);
void syncevo_server_get (SyncevoServer *server, const char **name, const char **url, const char **icon, gboolean *consumer_ready);
void syncevo_server_free (SyncevoServer *server);


SyncevoReport* syncevo_report_new (char *source);

void syncevo_report_set_io (SyncevoReport *report, 
                            int sent_bytes, int received_bytes);
void syncevo_report_set_local (SyncevoReport *report, 
                               int adds, int updates, int removes, int rejects);
void syncevo_report_set_remote (SyncevoReport *report, 
                                int adds, int updates, int removes, int rejects);
void syncevo_report_set_conflicts (SyncevoReport *report, 
                                   int local_won, int remote_won, int duplicated);

const char* syncevo_report_get_name (SyncevoReport *report);
void syncevo_report_get_io (SyncevoReport *report,
                            int *bytes_sent, int *bytes_received);
void syncevo_report_get_local (SyncevoReport *report, 
                               int *adds, int *updates, int *removes, int *rejects);
void syncevo_report_get_remote (SyncevoReport *report, 
                               int *adds, int *updates, int *removes, int *rejects);
void syncevo_report_get_conflicts (SyncevoReport *report, 
                                   int *local_won, int *remote_won, int *duplicated);

void syncevo_report_free (SyncevoReport *report);


SyncevoReportArray* syncevo_report_array_new (int end_time, GPtrArray *reports);
void syncevo_report_array_get (SyncevoReportArray *array, int *end_time, GPtrArray **reports);
void syncevo_report_array_free (SyncevoReportArray *array);


#endif
