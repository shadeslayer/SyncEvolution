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

#ifndef __SYNCEVO_SERVER_H__
#define __SYNCEVO_SERVER_H__

#include <glib-object.h>
#include "syncevo-dbus-types.h"
#include "syncevo-session.h"

G_BEGIN_DECLS 

enum SyncevoServerError{
    SYNCEVO_SERVER_ERROR_NO_DBUS_OBJECT = 1,
};

#define SYNCEVO_SERVER_ERROR_QUARK g_quark_from_static_string ("syncevo-server")

#define DBUS_SERVICE_SYNCEVO_SERVER "org.syncevolution"
#define DBUS_PATH_SYNCEVO_SERVER "/org/syncevolution/Server"
#define DBUS_INTERFACE_SYNCEVO_SERVER "org.syncevolution.Server"

#define SYNCEVO_TYPE_SERVER (syncevo_server_get_type ())
#define SYNCEVO_SERVER(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), SYNCEVO_TYPE_SERVER, SyncevoServer))
#define SYNCEVO_IS_SERVER(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), SYNCEVO_TYPE_SERVER))


typedef struct _SyncevoServer {
    GObject parent_object;
} SyncevoServer;

typedef struct _SyncevoServerClass {
    GObjectClass parent_class;

    void (*session_changed) (SyncevoServer *syncevo,
                             char *session_path,
                             gboolean started);

    void (*presence_changed) (SyncevoServer *syncevo,
                              char *configuration,
                              char *status,
                              char *transport);

    void (*info_request) (SyncevoServer *syncevo,
                          char *id,
                          char *session_path,
                          char *state,
                          char *handler_path,
                          char *type,
                          GHashTable *parameters);

    void (*templates_changed) (SyncevoServer *syncevo);

    void (*shutdown) (SyncevoServer *syncevo);
} SyncevoServerClass;

GType syncevo_server_get_type (void);

SyncevoServer *syncevo_server_get_default ();

typedef void (*SyncevoServerGenericCb) (SyncevoServer *server,
                                        GError *error,
                                        gpointer userdata);

typedef void (*SyncevoServerGetConfigsCb) (SyncevoServer *syncevo,
                                           char **config_names,
                                           GError *error,
                                           gpointer userdata);
void syncevo_server_get_configs (SyncevoServer *syncevo,
                                 gboolean template,
                                 SyncevoServerGetConfigsCb callback,
                                 gpointer userdata);

typedef void (*SyncevoServerGetConfigCb) (SyncevoServer *syncevo,
                                          SyncevoConfig *config,
                                          GError *error,
                                          gpointer userdata);
void syncevo_server_get_config (SyncevoServer *syncevo,
                                const char *config_name, 
                                gboolean template,
                                SyncevoServerGetConfigCb callback,
                                gpointer userdata);

typedef void (*SyncevoServerGetReportsCb) (SyncevoServer *syncevo,
                                           SyncevoReports *reports,
                                           GError *error,
                                           gpointer userdata);
void syncevo_server_get_reports (SyncevoServer *syncevo,
                                 const char *config_name,
                                 guint start,
                                 guint count,
                                 SyncevoServerGetReportsCb callback,
                                 gpointer userdata);

typedef void (*SyncevoServerStartSessionCb) (SyncevoServer *syncevo,
                                             char *session_path,
                                             GError *error,
                                             gpointer userdata);
void syncevo_server_start_session (SyncevoServer *syncevo,
                                   const char *config_name, 
                                   SyncevoServerStartSessionCb callback,
                                   gpointer userdata);
void syncevo_server_start_no_sync_session (SyncevoServer *syncevo,
                                           const char *config_name, 
                                           SyncevoServerStartSessionCb callback,
                                           gpointer userdata);

typedef void (*SyncevoServerGetSessionsCb) (SyncevoServer *syncevo,
                                            SyncevoSessions *sessions,
                                            GError *error,
                                            gpointer userdata);
void syncevo_server_get_sessions (SyncevoServer *syncevo,
                                  SyncevoServerGetSessionsCb callback,
                                  gpointer userdata);

typedef void (*SyncevoServerGetPresenceCb) (SyncevoServer *syncevo,
                                            char *status,
                                            char **transports,
                                            GError *error,
                                            gpointer userdata);
void syncevo_server_get_presence (SyncevoServer *syncevo,
                                  const char *config_name,
                                  SyncevoServerGetPresenceCb callback,
                                  gpointer userdata);

void syncevo_server_check_source (SyncevoServer *server,
                                  const char *config,
                                  const char *source,
                                  SyncevoServerGenericCb callback,
                                  gpointer userdata);

void syncevo_server_info_response (SyncevoServer *server,
                                   const char *id,
                                   const char *state,
                                   GHashTable *response,
                                   SyncevoServerGenericCb callback,
                                   gpointer userdata);

G_END_DECLS

#endif
