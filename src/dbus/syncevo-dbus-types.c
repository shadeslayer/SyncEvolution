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

#include <string.h>
#include <glib.h>
#include <dbus/dbus-glib.h>
#include "syncevo-dbus-types.h"


gboolean
syncevo_config_get_value (SyncevoConfig *config,
                          const char *source,
                          const char *key,
                          char **value)
{
    char *name;
    GHashTable *source_config;

    g_return_val_if_fail (config, FALSE);
    g_return_val_if_fail (value, FALSE);

    if (!source || strlen (source) == 0) {
        name = g_strdup ("");
    } else {
        name = g_strdup_printf ("sources/%s", source);
    }

    source_config = (GHashTable*)g_hash_table_lookup (config, name);
    g_free (name);

    if (source_config) {
        *value = (char*)g_hash_table_lookup (source_config, key);
    }
    
    return (source_config != NULL);
}

static void
free_source_config_item (char *key,
                         char *value)
{
    g_free (key);
    g_free (value);
}

static void
free_source_configs_item (char *source,
                          GHashTable *source_config)
{
    g_free (source);
    g_hash_table_foreach (source_config, 
                          (GHFunc)free_source_config_item,
                          NULL);
    g_hash_table_destroy (source_config);
}


void
syncevo_config_free (SyncevoConfig *config)
{
    g_hash_table_foreach (config,
                          (GHFunc)free_source_configs_item,
                          NULL);
    g_hash_table_destroy (config);
}

const char*
syncevo_sync_mode_to_string (SyncevoSyncMode mode)
{
    const char *mode_str;

    switch (mode) {
    case SYNCEVO_SYNC_NONE:
        mode_str = "none";
        break;
    case SYNCEVO_SYNC_TWO_WAY:
        mode_str = "two-way";
        break;
    case SYNCEVO_SYNC_SLOW:
        mode_str = "slow";
        break;
    case SYNCEVO_SYNC_REFRESH_FROM_CLIENT:
        mode_str = "refresh-from-client";
        break;
    case SYNCEVO_SYNC_REFRESH_FROM_SERVER:
        mode_str = "refresh-from-server";
        break;
    case SYNCEVO_SYNC_ONE_WAY_FROM_CLIENT:
        mode_str = "one-way-from-client";
        break;
    case SYNCEVO_SYNC_ONE_WAY_FROM_SERVER:
        mode_str = "one-way-from-server";
        break;
    case SYNCEVO_SYNC_DEFAULT:
        mode_str = "";
        break;
    default:
        mode_str = "";
        break;
    }

    return mode_str;
}

SyncevoSourceModes*
syncevo_source_modes_new ()
{
    return g_hash_table_new_full (g_str_hash, g_str_equal,
                                  NULL, NULL);
}

void
syncevo_source_modes_add (SyncevoSourceModes *source_modes,
                          char *source,
                          SyncevoSyncMode mode)
{
    const char *mode_str;

    g_return_if_fail (source_modes);
    g_return_if_fail (source);

    mode_str = syncevo_sync_mode_to_string (mode);

    g_hash_table_insert (source_modes, source, (char*)mode_str);
}

void
syncevo_source_modes_free (SyncevoSourceModes *source_modes)
{
    /* no need to free keys/values */
    g_hash_table_destroy (source_modes);
}

SyncevoSessionStatus
syncevo_session_status_from_string (const char *status_str)
{
    SyncevoSessionStatus status;

    if (!status_str) {
        status = SYNCEVO_STATUS_UNKNOWN;
    } else if (g_str_has_prefix (status_str, "queueing")) {
        status = SYNCEVO_STATUS_QUEUEING;
    } else if (g_str_has_prefix (status_str, "idle")) {
        status = SYNCEVO_STATUS_IDLE;
    } else if (g_str_has_prefix (status_str, "running")) {
        status = SYNCEVO_STATUS_RUNNING;
    } else if (g_str_has_prefix (status_str, "aborting")) {
        status = SYNCEVO_STATUS_ABORTING;
    } else if (g_str_has_prefix (status_str, "suspending")) {
        status = SYNCEVO_STATUS_SUSPENDING;
    } else {
        status = SYNCEVO_STATUS_UNKNOWN;
    }

    return status;
}

gboolean
syncevo_source_statuses_get (SyncevoSourceStatuses *source_statuses,
                             char *source,
                             SyncevoSyncMode *mode,
                             SyncevoSourceStatus *status,
                             guint *error_code)
{
    GValueArray *source_status;

    g_return_val_if_fail (source_statuses, FALSE);
    g_return_val_if_fail (source, FALSE);

    source_status = g_hash_table_lookup (source_statuses, source);
    if (!source_status) {
        return FALSE;
    }

    if (mode) {
        const char *mode_str;

        mode_str = g_value_get_string (g_value_array_get_nth (source_status, 0));
        if (!mode_str) {
            *mode = SYNCEVO_SYNC_UNKNOWN;
        } else if (g_str_has_prefix (mode_str, "none")) {
            *mode = SYNCEVO_SYNC_NONE;
        } else if (g_str_has_prefix (mode_str, "two-way")) {
            *mode = SYNCEVO_SYNC_TWO_WAY;
        } else if (g_str_has_prefix (mode_str, "slow")) {
            *mode = SYNCEVO_SYNC_SLOW;
        } else if (g_str_has_prefix (mode_str, "refresh-from-client")) {
            *mode = SYNCEVO_SYNC_REFRESH_FROM_CLIENT;
        } else if (g_str_has_prefix (mode_str, "refresh-from-server")) {
            *mode = SYNCEVO_SYNC_REFRESH_FROM_SERVER;
        } else if (g_str_has_prefix (mode_str, "one-way-from-client")) {
            *mode = SYNCEVO_SYNC_ONE_WAY_FROM_CLIENT;
        } else if (g_str_has_prefix (mode_str, "one-way-from-server")) {
            *mode = SYNCEVO_SYNC_ONE_WAY_FROM_SERVER;
        } else {
            *mode = SYNCEVO_SYNC_UNKNOWN;
        }
    }
    if (status) {
        const char *status_str;
        status_str = g_value_get_string (g_value_array_get_nth (source_status, 1));
        *status = syncevo_session_status_from_string (status_str);
    }
    if (error_code) {
        *error_code = g_value_get_uint (g_value_array_get_nth (source_status, 2));
    }

    return TRUE;
}

static void
free_source_status_item (char *source,
                         GValueArray *status_array)
{
    g_free (source);
    g_boxed_free (SYNCEVO_TYPE_SOURCE_STATUS, status_array);
}

void
syncevo_source_statuses_free (SyncevoSourceStatuses *source_statuses)
{
    g_hash_table_foreach (source_statuses, 
                          (GHFunc)free_source_status_item,
                          NULL);
    g_hash_table_destroy (source_statuses);
}

gboolean
syncevo_source_progresses_get (SyncevoSourceProgresses *source_progresses,
                               char *source,
                               SyncevoSourceProgress *source_progress)
{
    const char *phase_str;
    GValueArray *progress_array;
    GValue *val;

    g_return_val_if_fail (source_progresses, FALSE);
    g_return_val_if_fail (source, FALSE);

    progress_array = g_hash_table_lookup (source_progresses, source);
    if (!progress_array) {
        return FALSE;
    }

    if (!source_progress) {
        return TRUE;
    }


    phase_str = g_value_get_string (g_value_array_get_nth (progress_array, 0));
    if (!phase_str) {
        source_progress->phase = SYNCEVO_PHASE_NONE;
    } else if (g_str_has_prefix (phase_str, "preparing")) {
        source_progress->phase = SYNCEVO_PHASE_PREPARING;
    } else if (g_str_has_prefix (phase_str, "sending")) {
        source_progress->phase = SYNCEVO_PHASE_SENDING;
    } else if (g_str_has_prefix (phase_str, "receiving")) {
        source_progress->phase = SYNCEVO_PHASE_RECEIVING;
    } else {
        source_progress->phase = SYNCEVO_PHASE_NONE;
    }

    val = g_value_array_get_nth (progress_array, 1);
    source_progress->prepare_current = g_value_get_int (val);
    val = g_value_array_get_nth (progress_array, 2);
    source_progress->prepare_total = g_value_get_int (val);
    val = g_value_array_get_nth (progress_array, 3);
    source_progress->send_current = g_value_get_int (val);
    val = g_value_array_get_nth (progress_array, 4);
    source_progress->send_total = g_value_get_int (val);
    val = g_value_array_get_nth (progress_array, 5);
    source_progress->receive_current = g_value_get_int (val);
    val = g_value_array_get_nth (progress_array, 6);
    source_progress->receive_total = g_value_get_int (val);

    return TRUE;
}

static void
free_source_progress_item (char *source,
                           GValueArray *progress_array)
{
    g_free (source);
    g_boxed_free (SYNCEVO_TYPE_SOURCE_PROGRESS, progress_array);
}

void
syncevo_source_progresses_free (SyncevoSourceProgresses *source_progresses)
{
    g_hash_table_foreach (source_progresses, 
                          (GHFunc)free_source_progress_item,
                          NULL);
    g_hash_table_destroy (source_progresses);
}

static void
free_report_item (char *key, char *value)
{
    g_free (key);
    g_free (value);
}

static void
syncevo_report_free (GHashTable *report)
{
    g_hash_table_foreach (report, 
                          (GHFunc)free_report_item,
                          NULL);
    g_hash_table_destroy (report);
}

GHashTable*
syncevo_reports_index (SyncevoReports *reports,
                       guint index)
{
    g_return_val_if_fail (reports, NULL);

    return (GHashTable*)g_ptr_array_index (reports, index);
}

void
syncevo_reports_free (SyncevoReports *reports)
{
    g_ptr_array_foreach (reports,
                         (GFunc)syncevo_report_free,
                         NULL);
    g_ptr_array_free (reports, TRUE);
}

char*
syncevo_sessions_index (SyncevoSessions *sessions,
                        guint index)
{
    g_return_val_if_fail (sessions, NULL);

    return (char*)g_ptr_array_index (sessions, index);
}

void
syncevo_sessions_free (SyncevoSessions *sessions)
{
    g_ptr_array_foreach (sessions,
                         (GFunc)g_free,
                         NULL);
    g_ptr_array_free (sessions, TRUE);
}

