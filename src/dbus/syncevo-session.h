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

#ifndef __SYNCEVO_SESSION_H__
#define __SYNCEVO_SESSION_H__

#include <glib-object.h>
#include "syncevo-dbus-types.h"

G_BEGIN_DECLS 

enum SyncevoSessionError{
    SYNCEVO_SESSION_ERROR_NO_DBUS_OBJECT = 1,
};

#define SYNCEVO_SESSION_DBUS_SERVICE "org.syncevolution"
#define SYNCEVO_SESSION_DBUS_INTERFACE "org.syncevolution.Session"

#define SYNCEVO_TYPE_SESSION (syncevo_session_get_type ())
#define SYNCEVO_SESSION(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), SYNCEVO_TYPE_SESSION, SyncevoSession))
#define SYNCEVO_IS_SESSION(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), SYNCEVO_TYPE_SESSION))

typedef struct _SyncevoSession {
    GObject parent_object;
} SyncevoSession;

typedef struct _SyncevoSessionClass {
    GObjectClass parent_class;

    void (*status_changed) (SyncevoSession *session,
                            SyncevoSessionStatus status,
                            guint error_code,
                            SyncevoSourceStatuses *source_statuses);

    void (*progress_changed) (SyncevoSession *session,
                              int progress,
                              SyncevoSourceProgresses *source_progresses);

} SyncevoSessionClass;

GType syncevo_session_get_type (void);

typedef void (*SyncevoSessionGenericCb) (SyncevoSession *session,
                                         GError *error,
                                         gpointer userdata);

typedef void (*SyncevoSessionGetConfigNameCb) (SyncevoSession *session,
                                               char *name,
                                               GError *error,
                                               gpointer userdata);
void syncevo_session_get_config_name (SyncevoSession *session,
                                      SyncevoSessionGetConfigNameCb callback,
                                      gpointer userdata);

typedef void (*SyncevoSessionGetConfigCb) (SyncevoSession *session,
                                           SyncevoConfig *config,
                                           GError *error,
                                           gpointer userdata);
void syncevo_session_get_config (SyncevoSession *session,
                                 gboolean template,
                                 SyncevoSessionGetConfigCb callback,
                                 gpointer userdata);

void syncevo_session_set_config (SyncevoSession *session,
                                 gboolean update,
                                 gboolean temporary,
                                 SyncevoConfig *config,
                                 SyncevoSessionGenericCb callback,
                                 gpointer userdata);

typedef void (*SyncevoSessionGetReportsCb) (SyncevoSession *session,
                                            SyncevoReports *reports,
                                            GError *error,
                                            gpointer userdata);
void syncevo_session_get_reports (SyncevoSession *session,
                                  guint start,
                                  guint count,
                                  SyncevoSessionGetReportsCb callback,
                                  gpointer userdata);

void syncevo_session_sync (SyncevoSession *session,
                           SyncevoSyncMode mode,
                           SyncevoSourceModes *source_modes,
                           SyncevoSessionGenericCb callback,
                           gpointer userdata);

void syncevo_session_abort (SyncevoSession *session,
                            SyncevoSessionGenericCb callback,
                            gpointer userdata);

void syncevo_session_suspend (SyncevoSession *session,
                              SyncevoSessionGenericCb callback,
                              gpointer userdata);

typedef void (*SyncevoSessionGetStatusCb) (SyncevoSession *session,
                                           SyncevoSessionStatus status,
                                           guint error_code,
                                           SyncevoSourceStatuses *source_statuses,
                                           GError *error,
                                           gpointer userdata);
void syncevo_session_get_status (SyncevoSession *session,
                                 SyncevoSessionGetStatusCb callback,
                                 gpointer userdata);

typedef void (*SyncevoSessionGetProgressCb) (SyncevoSession *session,
                                             guint progress,
                                             SyncevoSourceProgresses *source_progresses,
                                             GError *error,
                                             gpointer userdata);
void syncevo_session_get_progress (SyncevoSession *session,
                                   SyncevoSessionGetProgressCb callback,
                                   gpointer userdata);

void syncevo_session_check_source (SyncevoSession *session,
                                   const char *source,
                                   SyncevoSessionGenericCb callback,
                                   gpointer userdata);

void syncevo_session_restore (SyncevoSession *session,
                              const char *backup_dir,
                              const gboolean before,
                              const char **sources,
                              SyncevoSessionGenericCb callback,
                              gpointer userdata);

const char *syncevo_session_get_path (SyncevoSession *session);

SyncevoSession *syncevo_session_new (const char *path);

G_END_DECLS

#endif
