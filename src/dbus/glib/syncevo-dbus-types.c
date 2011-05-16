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

    *value = NULL;

    if (!source || strlen (source) == 0) {
        name = g_strdup ("");
    } else {
        name = g_strdup_printf ("source/%s", source);
    }

    source_config = (GHashTable*)g_hash_table_lookup (config, name);
    g_free (name);

    if (source_config) {
        return g_hash_table_lookup_extended (source_config, key,
                                             NULL, (gpointer*)value);
    }
    return FALSE;
}

gboolean
syncevo_config_set_value (SyncevoConfig *config,
                          const char *source,
                          const char *key,
                          const char *value)
{
    gboolean changed;
    char *name;
    char *old_value;
    GHashTable *source_config;

    g_return_val_if_fail (config, FALSE);
    g_return_val_if_fail (key, FALSE);

    if (!source || strlen (source) == 0) {
        name = g_strdup ("");
    } else {
        name = g_strdup_printf ("source/%s", source);
    }

    source_config = (GHashTable*)g_hash_table_lookup (config, name);
    if (!source_config) {
        source_config = g_hash_table_new (g_str_hash, g_str_equal);
        g_hash_table_insert (config, name, source_config);
    } else {
        g_free (name);
    }

    old_value = g_hash_table_lookup (source_config, key);
    if ((!old_value && !value) ||
        (old_value && value && strcmp (old_value, value) == 0)) {
        changed = FALSE;
    } else {
        changed = TRUE;
        g_hash_table_insert (source_config, g_strdup (key), g_strdup (value));
    }

    return changed;
}

void syncevo_config_foreach_source (SyncevoConfig *config,
                                    ConfigFunc func,
                                    gpointer userdata)
{
    GHashTableIter iter;
    char *key;
    GHashTable *value;

    g_hash_table_iter_init (&iter, config);
    while (g_hash_table_iter_next (&iter, (gpointer*)&key, (gpointer*)&value)) {

        if (key && g_str_has_prefix (key, "source/")) {
            char *name;

            name = key+7;
            func (name, value, userdata);
        }
    }
}


void
syncevo_config_free (SyncevoConfig *config)
{
    /* NOTE: Hashtables gcreated by dbus-glib should free their contents */
    if (config) {
        g_hash_table_destroy (config);
    }
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
    } else if (g_str_has_prefix (status_str, "done")) {
        status = SYNCEVO_STATUS_DONE;
    } else if (g_str_has_prefix (status_str, "running")) {
        status = SYNCEVO_STATUS_RUNNING;
    } else if (g_str_has_prefix (status_str, "aborting")) {
        status = SYNCEVO_STATUS_ABORTING;
    } else if (g_str_has_prefix (status_str, "suspending")) {
        status = SYNCEVO_STATUS_SUSPENDING;
    } else {
        status = SYNCEVO_STATUS_UNKNOWN;
    }

    if (status_str && strstr (status_str, ";waiting")) {
        status |= SYNCEVO_STATUS_WAITING;
    }

    return status;
}

SyncevoSyncMode
syncevo_sync_mode_from_string (const char *mode_str)
{
    if (!mode_str ||
        g_str_has_prefix (mode_str, "none") ||
        g_str_has_prefix (mode_str, "disabled")) {
        return SYNCEVO_SYNC_NONE;
    } else if (g_str_has_prefix (mode_str, "two-way")) {
        return SYNCEVO_SYNC_TWO_WAY;
    } else if (g_str_has_prefix (mode_str, "slow")) {
        return SYNCEVO_SYNC_SLOW;
    } else if (g_str_has_prefix (mode_str, "refresh-from-client")) {
        return SYNCEVO_SYNC_REFRESH_FROM_CLIENT;
    } else if (g_str_has_prefix (mode_str, "refresh-from-server")) {
        return SYNCEVO_SYNC_REFRESH_FROM_SERVER;
    } else if (g_str_has_prefix (mode_str, "one-way-from-client")) {
        return SYNCEVO_SYNC_ONE_WAY_FROM_CLIENT;
    } else if (g_str_has_prefix (mode_str, "one-way-from-server")) {
        return SYNCEVO_SYNC_ONE_WAY_FROM_SERVER;
    } else {
        return SYNCEVO_SYNC_UNKNOWN;
    }
}

static SyncevoSessionStatus
syncevo_source_status_from_string (const char *status_str)
{
    SyncevoSessionStatus status;

    if (!status_str) {
        status = SYNCEVO_STATUS_UNKNOWN;
    } else if (g_str_has_prefix (status_str, "idle")) {
        status = SYNCEVO_STATUS_IDLE;
    } else if (g_str_has_prefix (status_str, "running")) {
        status = SYNCEVO_STATUS_RUNNING;
    } else if (g_str_has_prefix (status_str, "done")) {
        status = SYNCEVO_STATUS_DONE;
    } else {
        status = SYNCEVO_STATUS_UNKNOWN;
    }

    /* check modifiers */
    if (status_str && strstr (status_str, ";waiting")) {
        status |= SYNCEVO_STATUS_WAITING;
    }

    return status;
}

void
syncevo_source_statuses_foreach (SyncevoSourceStatuses *source_statuses,
                                 SourceStatusFunc func,
                                 gpointer data)
{
    GHashTableIter iter;
    GValueArray *source_status;
    
    char *name;

    g_return_if_fail (source_statuses);

    g_hash_table_iter_init (&iter, source_statuses);
    while (g_hash_table_iter_next (&iter, (gpointer)&name, (gpointer)&source_status)) {
        const char *mode_str;
        const char *status_str;
        SyncevoSyncMode mode;
        SyncevoSessionStatus status;
        guint error_code;

        mode_str = g_value_get_string (g_value_array_get_nth (source_status, 0));
        mode = syncevo_sync_mode_from_string (mode_str);

        status_str = g_value_get_string (g_value_array_get_nth (source_status, 1));
        status = syncevo_source_status_from_string (status_str);
        error_code = g_value_get_uint (g_value_array_get_nth (source_status, 2));

        func (name, mode, status, error_code, data);
    }
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

void
syncevo_source_progresses_foreach (SyncevoSourceProgresses *source_progresses,
                                   SourceProgressFunc func,
                                   gpointer userdata)
{
    const char *phase_str, *name;
    GHashTableIter iter;
    GValueArray *progress_array;

    g_return_if_fail (source_progresses);

    g_hash_table_iter_init (&iter, source_progresses);
    while (g_hash_table_iter_next (&iter, (gpointer)&name, (gpointer)&progress_array)) {
        SyncevoSourcePhase phase;
        phase_str = g_value_get_string (g_value_array_get_nth (progress_array, 0));    

        if (!phase_str) {
            phase = SYNCEVO_PHASE_NONE;
        } else if (g_str_has_prefix (phase_str, "preparing")) {
            phase = SYNCEVO_PHASE_PREPARING;
        } else if (g_str_has_prefix (phase_str, "sending")) {
            phase = SYNCEVO_PHASE_SENDING;
        } else if (g_str_has_prefix (phase_str, "receiving")) {
            phase = SYNCEVO_PHASE_RECEIVING;
        } else {
            phase = SYNCEVO_PHASE_NONE;
        }

/*
        val = g_value_array_get_nth (progress_array, 1);
        progress->prepare_current = g_value_get_int (val);
        val = g_value_array_get_nth (progress_array, 2);
        progress->prepare_total = g_value_get_int (val);
        val = g_value_array_get_nth (progress_array, 3);
        progress->send_current = g_value_get_int (val);
        val = g_value_array_get_nth (progress_array, 4);
        progress->send_total = g_value_get_int (val);
        val = g_value_array_get_nth (progress_array, 5);
        progress->receive_current = g_value_get_int (val);
        val = g_value_array_get_nth (progress_array, 6);
        progress->receive_total = g_value_get_int (val);
*/

        func (name, phase, userdata);
    }
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

guint
syncevo_reports_get_length (SyncevoReports *reports)
{
    return reports->len;
}

void
syncevo_reports_free (SyncevoReports *reports)
{
    g_ptr_array_foreach (reports,
                         (GFunc)syncevo_report_free,
                         NULL);
    g_ptr_array_free (reports, TRUE);
}

const char*
syncevo_sessions_index (SyncevoSessions *sessions,
                        guint index)
{
    g_return_val_if_fail (sessions, NULL);

    if (index >= sessions->len) {
        return NULL;
    }
    return (const char*)g_ptr_array_index (sessions, index);
}

void
syncevo_sessions_free (SyncevoSessions *sessions)
{
    g_ptr_array_foreach (sessions,
                         (GFunc)g_free,
                         NULL);
    g_ptr_array_free (sessions, TRUE);
}

