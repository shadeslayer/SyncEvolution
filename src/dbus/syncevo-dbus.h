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

#ifndef __SYNCEVO_SERVICE_H__
#define __SYNCEVO_SERVICE_H__

#include <glib-object.h>
#include "syncevo-dbus-types.h"

G_BEGIN_DECLS 

enum SyncevoServiceError{
	SYNCEVO_SERVICE_ERROR_COULD_NOT_START = 1,
};

#define SYNCEVO_SERVICE_DBUS_SERVICE "org.Moblin.SyncEvolution"
#define SYNCEVO_SERVICE_DBUS_PATH "/org/Moblin/SyncEvolution"
#define SYNCEVO_SERVICE_DBUS_INTERFACE "org.Moblin.SyncEvolution"

#define SYNCEVO_TYPE_SERVICE (syncevo_service_get_type ())
#define SYNCEVO_SERVICE(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), SYNCEVO_TYPE_SERVICE, SyncevoService))
#define SYNCEVO_IS_SERVICE(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), SYNCEVO_TYPE_SERVICE))


typedef struct _SyncevoService {
	GObject parent_object;
} SyncevoService;

typedef struct _SyncevoServiceClass {
	GObjectClass parent_class;

	void (*progress) (SyncevoService *service,
	                  char *server,
	                  char *source,
	                  int type,
	                  int extra1, int extra2, int extra3);
	void (*server_message) (SyncevoService *service,
	                        char *server,
	                        char *message);
	void (*server_shutdown) (SyncevoService *service);
} SyncevoServiceClass;

GType syncevo_service_get_type (void);

SyncevoService *syncevo_service_get_default ();

gboolean syncevo_service_start_sync (SyncevoService *service,
                                     char *server,
                                     GPtrArray *sources,
                                     GError **error);
gboolean syncevo_service_abort_sync (SyncevoService *service,
                                     char *server,
                                     GError **error);
typedef void (*SyncevoAbortSyncCb) (SyncevoService *service,
                                     GError *error,
                                     gpointer userdata);
void  syncevo_service_abort_sync_async (SyncevoService *service,
                                        char *server,
                                        SyncevoAbortSyncCb callback,
                                        gpointer userdata);

gboolean syncevo_service_get_servers (SyncevoService *service,
                                      GPtrArray **servers,
                                      GError **error);
typedef void (*SyncevoGetServersCb) (SyncevoService *service,
                                     GPtrArray *servers,
                                     GError *error,
                                     gpointer userdata);
void syncevo_service_get_servers_async (SyncevoService *service,
                                        SyncevoGetServersCb callback,
                                        gpointer userdata);

gboolean syncevo_service_get_templates (SyncevoService *service,
                                        GPtrArray **templates,
                                        GError **error);
typedef void (*SyncevoGetTemplatesCb) (SyncevoService *service,
                                       GPtrArray *templates,
                                       GError *error,
                                       gpointer userdata);
void syncevo_service_get_templates_async (SyncevoService *service,
                                          SyncevoGetTemplatesCb callback,
                                          gpointer userdata);

gboolean syncevo_service_get_template_config (SyncevoService *service,
                                              char *template,
                                              GPtrArray **options,
                                              GError **error);
typedef void (*SyncevoGetTemplateConfigCb) (SyncevoService *service,
                                            GPtrArray *options,
                                            GError *error,
                                            gpointer userdata);
void syncevo_service_get_template_config_async (SyncevoService *service,
                                                char *template,
                                                SyncevoGetTemplateConfigCb callback,
                                                gpointer userdata);

gboolean syncevo_service_get_server_config (SyncevoService *service,
                                            char *server,
                                            GPtrArray **options,
                                            GError **error);
typedef void (*SyncevoGetServerConfigCb) (SyncevoService *service,
                                          GPtrArray *options,
                                          GError *error,
                                          gpointer userdata);
void syncevo_service_get_server_config_async (SyncevoService *service,
                                              char *server,
                                              SyncevoGetServerConfigCb callback,
                                              gpointer userdata);

gboolean syncevo_service_set_server_config (SyncevoService *service,
                                            char *server,
                                            GPtrArray *options,
                                            GError **error);
typedef void (*SyncevoSetServerConfigCb) (SyncevoService *service,
                                          GError *error,
                                          gpointer userdata);
void syncevo_service_set_server_config_async (SyncevoService *service,
                                              char *server,
                                              GPtrArray *options,
                                              SyncevoSetServerConfigCb callback,
                                              gpointer userdata);

gboolean syncevo_service_remove_server_config (SyncevoService *service,
                                               char *server,
                                               GError **error);
typedef void (*SyncevoRemoveServerConfigCb) (SyncevoService *service,
                                             GError *error,
                                             gpointer userdata);
void syncevo_service_remove_server_config_async (SyncevoService *service,
                                                 char *server,
                                                 SyncevoRemoveServerConfigCb callback,
                                                 gpointer userdata);


gboolean syncevo_service_get_sync_reports (SyncevoService *service,
                                           char *server,
                                           int count,
                                           GPtrArray **reports,
                                           GError **error);
typedef void (*SyncevoGetSyncReportsCb) (SyncevoService *service,
                                         GPtrArray *reports,
                                         GError *error,
                                         gpointer userdata);
void syncevo_service_get_sync_reports_async (SyncevoService *service,
                                             char *server,
                                             int count,
                                             SyncevoGetSyncReportsCb callback,
                                             gpointer userdata);


G_END_DECLS

#endif
