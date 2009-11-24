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

#ifndef __SYNCEVO_TYPES_H__
#define __SYNCEVO_TYPES_H__

#include <glib.h>

#define SYNCEVO_DBUS_ERROR_EXCEPTION "org.syncevolution.Exception"
#define SYNCEVO_DBUS_ERROR_NO_SUCH_CONFIG "org.syncevolution.NoSuchConfig"
#define SYNCEVO_DBUS_ERROR_NO_SUCH_SOURCE "org.syncevolution.NoSuchsource"
#define SYNCEVO_DBUS_ERROR_INVALID_CALL "org.syncevolution.InvalidCall"
#define SYNCEVO_DBUS_ERROR_SOURCE_UNUSABLE "org.syncevolution.SourceUnusable"

typedef enum {
  SYNCEVO_SYNC_UNKNOWN, /* Cannot be used in Sync */
  SYNCEVO_SYNC_DEFAULT, /* cannot be received in GetStatus*/
  SYNCEVO_SYNC_NONE,
  SYNCEVO_SYNC_TWO_WAY,
  SYNCEVO_SYNC_SLOW,
  SYNCEVO_SYNC_REFRESH_FROM_CLIENT,
  SYNCEVO_SYNC_REFRESH_FROM_SERVER,
  SYNCEVO_SYNC_ONE_WAY_FROM_CLIENT,
  SYNCEVO_SYNC_ONE_WAY_FROM_SERVER,
} SyncevoSyncMode;

typedef enum {
  SYNCEVO_STATUS_UNKNOWN,
  SYNCEVO_STATUS_QUEUEING,
  SYNCEVO_STATUS_IDLE,
  SYNCEVO_STATUS_RUNNING,
  SYNCEVO_STATUS_ABORTING,
  SYNCEVO_STATUS_SUSPENDING,
  SYNCEVO_STATUS_DONE,
} SyncevoSessionStatus;

typedef enum {
  SYNCEVO_SOURCE_IDLE,
  SYNCEVO_SOURCE_RUNNING,
  SYNCEVO_SOURCE_RUNNING_WAITING,
  SYNCEVO_SOURCE_RUNNING_PROCESSING,
  SYNCEVO_SOURCE_DONE,
} SyncevoSourceStatus;

typedef enum {
  SYNCEVO_PHASE_NONE,
  SYNCEVO_PHASE_PREPARING,
  SYNCEVO_PHASE_SENDING,
  SYNCEVO_PHASE_RECEIVING,
} SyncevoSourcePhase;

typedef struct {
  char *name;
  SyncevoSourcePhase phase;
  int prepare_current;
  int prepare_total;
  int send_current;
  int send_total;
  int receive_current;
  int receive_total;
} SyncevoSourceProgress;  

#define SYNCEVO_TYPE_SOURCE_STATUS (dbus_g_type_get_struct ("GValueArray", G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID))
#define SYNCEVO_TYPE_SOURCE_STATUSES (dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, SYNCEVO_TYPE_SOURCE_STATUS))
#define SYNCEVO_TYPE_SOURCE_PROGRESS (dbus_g_type_get_struct ("GValueArray", G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INVALID))
#define SYNCEVO_TYPE_SOURCE_PROGRESSES (dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, SYNCEVO_TYPE_SOURCE_PROGRESS))

typedef GHashTable SyncevoConfig;
typedef GHashTable SyncevoSourceModes;
typedef GHashTable SyncevoSourceStatuses;
typedef GHashTable SyncevoSourceProgresses;
typedef GPtrArray SyncevoReports;
typedef GPtrArray SyncevoSessions;


gboolean syncevo_config_get_value (SyncevoConfig *config,
                                   const char *source,
                                   const char *key,
                                   char **value);
gboolean syncevo_config_set_value (SyncevoConfig *config,
                                   const char *source,
                                   const char *key,
                                   const char *value);

typedef void (*ConfigFunc) (char *name,
                            GHashTable *source_configuration,
                            gpointer user_data);

void syncevo_config_foreach_source (SyncevoConfig *config,
                                    ConfigFunc func,
                                    gpointer userdata);
void syncevo_config_free (SyncevoConfig *config);

const char* syncevo_sync_mode_to_string (SyncevoSyncMode mode);

SyncevoSourceModes* syncevo_source_modes_new ();
void syncevo_source_modes_add (SyncevoSourceModes *source_modes,
                               char *source,
                               SyncevoSyncMode mode);
void syncevo_source_modes_free (SyncevoSourceModes *source_modes);

SyncevoSessionStatus syncevo_session_status_from_string (const char *status_str);


typedef void (*SourceStatusFunc) (char *name,
                                  SyncevoSyncMode mode,
                                  SyncevoSourceStatus status,
                                  guint error_code,
                                  gpointer user_data);
void
syncevo_source_statuses_foreach (SyncevoSourceStatuses *source_statuses,
                                 SourceStatusFunc func,
                                 gpointer data);

void syncevo_source_statuses_free (SyncevoSourceStatuses *source_statuses);


SyncevoSourceProgress* syncevo_source_progresses_get_current (SyncevoSourceProgresses *source_progresses);

void syncevo_source_progresses_free (SyncevoSourceProgresses *source_progresses);

void syncevo_source_progress_free (SyncevoSourceProgress *progress);


GHashTable* syncevo_reports_index (SyncevoReports *reports,
                                   guint index);
guint syncevo_reports_get_length (SyncevoReports *reports);

void syncevo_reports_free (SyncevoReports *reports);


const char* syncevo_sessions_index (SyncevoSessions *sessions,
                                    guint index);
void syncevo_sessions_free (SyncevoSessions *sessions);

#endif
