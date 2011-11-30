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

#include <glib-object.h>
#include <string.h>

#include "syncevo-server.h"
#include "syncevo-marshal.h"
#include "syncevo-server-bindings.h"

typedef struct _ServerAsyncData {
    SyncevoServer *server;
    GCallback callback;
    gpointer userdata;
} ServerAsyncData;

enum {
    SESSION_CHANGED,
    PRESENCE_CHANGED,
    INFO_REQUEST,
    TEMPLATES_CHANGED,
    SHUTDOWN,
    LAST_SIGNAL
};
static guint32 signals[LAST_SIGNAL] = {0, };

typedef struct _SyncevoServerPrivate {
    DBusGProxy *proxy;
} SyncevoServerPrivate;

#define GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), SYNCEVO_TYPE_SERVER, SyncevoServerPrivate))
G_DEFINE_TYPE (SyncevoServer, syncevo_server, G_TYPE_OBJECT);

static ServerAsyncData*
server_async_data_new (SyncevoServer *server,
                        GCallback callback,
                        gpointer userdata)
{
    ServerAsyncData *data;

    data = g_slice_new0 (ServerAsyncData);
    data->server = server;
    data->callback = G_CALLBACK (callback);
    data->userdata = userdata;

    return data;
}

static void
server_async_data_free (ServerAsyncData *data)
{
    g_slice_free (ServerAsyncData, data);
}

static void
generic_callback (DBusGProxy *proxy,
                  GError *error,
                  ServerAsyncData *data)
{
    if (data->callback) {
        (*(SyncevoServerGenericCb)data->callback) (data->server,
                                                   error,
                                                   data->userdata);
    }
    server_async_data_free (data);
}

static gboolean
generic_error (ServerAsyncData *data)
{
    GError *error;

    error = g_error_new_literal (SYNCEVO_SERVER_ERROR_QUARK,
                                 SYNCEVO_SERVER_ERROR_NO_DBUS_OBJECT, 
                                 "The D-Bus object does not exist");
    (*(SyncevoServerGenericCb)data->callback) (data->server,
                                               error,
                                               data->userdata);
    server_async_data_free (data);

    return FALSE;
}

static void
templates_changed_cb (DBusGProxy *proxy,
                      SyncevoServer *server)
{
    g_signal_emit (server, signals[TEMPLATES_CHANGED], 0);
}

static void
session_changed_cb (DBusGProxy *proxy,
                    char *session_path,
                    gboolean started,
                    SyncevoServer *server)
{
    g_signal_emit (server, signals[SESSION_CHANGED], 0, 
                   session_path, started);
}

static void
presence_cb (DBusGProxy *proxy,
             char *configuration,
             char *status,
             char *transport,
             SyncevoServer *server)
{
    g_signal_emit (server, signals[PRESENCE_CHANGED], 0, 
                   configuration, status, transport);
}

static void
info_request_cb (DBusGProxy *proxy,
                 char *id,
                 char *session_path,
                 char *state,
                 char *handler_path,
                 char *type,
                 GHashTable *parameters,
                 SyncevoServer *server)
{
    g_signal_emit (server, signals[INFO_REQUEST], 0, 
                   id, session_path, state, handler_path, type, parameters);
}

static void
proxy_destroy_cb (DBusGProxy *proxy, 
                  SyncevoServer *server)
{
    SyncevoServerPrivate *priv;
    priv = GET_PRIVATE (server);

    if (priv->proxy) {
        g_object_unref (priv->proxy);
    }
    priv->proxy = NULL;

    g_signal_emit (server, signals[SHUTDOWN], 0);
}

#if 0
static void
detach_cb (DBusGProxy *proxy,
           GError *error,
           gpointer userdata)
{
    if (error) {
        g_warning ("Server.Detach failed: %s", error->message);
        g_error_free (error);
    }
}
#endif

static void
dispose (GObject *object)
{
    SyncevoServerPrivate *priv;

    priv = GET_PRIVATE (object);

    if (priv->proxy) {
        dbus_g_proxy_disconnect_signal (priv->proxy, "SessionChanged",
                                        G_CALLBACK (session_changed_cb),
                                        object);
        dbus_g_proxy_disconnect_signal (priv->proxy, "Presence",
                                        G_CALLBACK (presence_cb),
                                        object);
        dbus_g_proxy_disconnect_signal (priv->proxy, "InfoRequest",
                                        G_CALLBACK (info_request_cb),
                                        object);
        dbus_g_proxy_disconnect_signal (priv->proxy, "TemplatesChanged",
                                        G_CALLBACK (templates_changed_cb),
                                        object);
        dbus_g_proxy_disconnect_signal (priv->proxy, "destroy",
                                        G_CALLBACK (proxy_destroy_cb),
                                        object);
        org_syncevolution_Server_detach (priv->proxy, NULL);

        g_object_unref (priv->proxy);
        priv->proxy = NULL;
    }

    G_OBJECT_CLASS (syncevo_server_parent_class)->dispose (object);
}

static void
attach_cb (DBusGProxy *proxy,
           GError *error,
           gpointer userdata)
{
    if (error) {
        g_warning ("Server.Attach failed: %s", error->message);
        g_error_free (error);
    }
}

static gboolean
syncevo_server_get_new_proxy (SyncevoServer *server)
{
    DBusGConnection *connection;
    GError *error;
    guint32 result;
    SyncevoServerPrivate *priv;
    DBusGProxy *proxy;

    priv = GET_PRIVATE (server);
    error = NULL;

    connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
    if (connection == NULL) {
        g_printerr ("Failed to open connection to bus: %s\n",
                    error->message);
        g_error_free (error);
        priv->proxy = NULL;
        return FALSE;
    }

    /* we want to use dbus_g_proxy_new_for_name_owner() for the destroy signal
     * so need to start the service by hand by using DBUS proxy: */
    proxy = dbus_g_proxy_new_for_name (connection,
                                       DBUS_SERVICE_DBUS,
                                       DBUS_PATH_DBUS,
                                       DBUS_INTERFACE_DBUS);
    if (!dbus_g_proxy_call (proxy, "StartServiceByName", NULL,
                            G_TYPE_STRING, DBUS_SERVICE_SYNCEVO_SERVER,
                            G_TYPE_UINT, 0,
                            G_TYPE_INVALID,
                            G_TYPE_UINT, &result,
                            G_TYPE_INVALID)) {
        g_warning ("StartServiceByName call failed");
    }
    g_object_unref (proxy);

    /* the real proxy */
    priv->proxy = dbus_g_proxy_new_for_name_owner (connection,
                                             DBUS_SERVICE_SYNCEVO_SERVER,
                                             DBUS_PATH_SYNCEVO_SERVER,
                                             DBUS_INTERFACE_SYNCEVO_SERVER,
                                             &error);
    if (priv->proxy == NULL) {
        g_printerr ("dbus_g_proxy_new_for_name_owner() failed: %s\n", error->message);
        g_error_free (error);
        return FALSE;
    }

    dbus_g_proxy_add_signal (priv->proxy, "SessionChanged",
                             DBUS_TYPE_G_OBJECT_PATH, G_TYPE_BOOLEAN, G_TYPE_INVALID);
    dbus_g_proxy_connect_signal (priv->proxy, "SessionChanged",
                                 G_CALLBACK (session_changed_cb), server, NULL);
    dbus_g_proxy_add_signal (priv->proxy, "Presence",
                             G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
    dbus_g_proxy_connect_signal (priv->proxy, "Presence",
                                 G_CALLBACK (presence_cb), server, NULL);
    dbus_g_proxy_add_signal (priv->proxy, "InfoRequest",
                             G_TYPE_STRING,
                             DBUS_TYPE_G_OBJECT_PATH,
                             G_TYPE_STRING,
                             G_TYPE_STRING,
                             G_TYPE_STRING,
                             SYNCEVO_TYPE_STRING_STRING_HASHTABLE,
                             G_TYPE_INVALID);
    dbus_g_proxy_connect_signal (priv->proxy, "InfoRequest",
                                 G_CALLBACK (info_request_cb), server, NULL);
    dbus_g_proxy_add_signal (priv->proxy, "TemplatesChanged", G_TYPE_INVALID);
    dbus_g_proxy_connect_signal (priv->proxy, "TemplatesChanged",
                                 G_CALLBACK (templates_changed_cb), server, NULL);
    g_signal_connect (priv->proxy, "destroy",
                      G_CALLBACK (proxy_destroy_cb), server);

    org_syncevolution_Server_attach_async (priv->proxy,
                                           (org_syncevolution_Server_attach_reply)attach_cb,
                                           NULL);

    return TRUE;
}


static void
syncevo_server_init (SyncevoServer *server)
{
    /* SessionChanged */
    dbus_g_object_register_marshaller (syncevo_marshal_VOID__STRING_BOOLEAN,
                                       G_TYPE_NONE,
                                       DBUS_TYPE_G_OBJECT_PATH,
                                       G_TYPE_BOOLEAN,
                                       G_TYPE_INVALID);
    /* Presence */
    dbus_g_object_register_marshaller (syncevo_marshal_VOID__STRING_STRING_STRING,
                                       G_TYPE_NONE,
                                       G_TYPE_STRING,
                                       G_TYPE_STRING,
                                       G_TYPE_STRING,
                                       G_TYPE_INVALID);
    /* InfoRequest */
    dbus_g_object_register_marshaller (syncevo_marshal_VOID__STRING_STRING_STRING_STRING_STRING_BOXED,
                                       G_TYPE_NONE,
                                       G_TYPE_STRING,
                                       DBUS_TYPE_G_OBJECT_PATH,
                                       G_TYPE_STRING,
                                       G_TYPE_STRING,
                                       G_TYPE_STRING,
                                       G_TYPE_BOXED,
                                       G_TYPE_INVALID);

    /* TemplatesChanged */
    dbus_g_object_register_marshaller (g_cclosure_marshal_VOID__VOID,
                                       G_TYPE_NONE,
                                       G_TYPE_INVALID);

    syncevo_server_get_new_proxy (server);
}

static void
syncevo_server_class_init (SyncevoServerClass *klass)
{
    GObjectClass *o_class = (GObjectClass *) klass;

    o_class->dispose = dispose;

    g_type_class_add_private (klass, sizeof (SyncevoServerPrivate));

    signals[SESSION_CHANGED] =
            g_signal_new ("session-changed",
                          G_TYPE_FROM_CLASS (klass),
                          G_SIGNAL_RUN_FIRST | G_SIGNAL_NO_RECURSE,
                          G_STRUCT_OFFSET (SyncevoServerClass, session_changed),
                          NULL, NULL,
                          syncevo_marshal_VOID__STRING_BOOLEAN,
                          G_TYPE_NONE,
                          2, G_TYPE_STRING, G_TYPE_BOOLEAN);
    signals[PRESENCE_CHANGED] =
            g_signal_new ("presence-changed",
                          G_TYPE_FROM_CLASS (klass),
                          G_SIGNAL_RUN_FIRST | G_SIGNAL_NO_RECURSE,
                          G_STRUCT_OFFSET (SyncevoServerClass, presence_changed),
                          NULL, NULL,
                          syncevo_marshal_VOID__STRING_STRING_STRING,
                          G_TYPE_NONE,
                          3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    signals[INFO_REQUEST] =
            g_signal_new ("info-request",
                          G_TYPE_FROM_CLASS (klass),
                          G_SIGNAL_RUN_FIRST | G_SIGNAL_NO_RECURSE,
                          G_STRUCT_OFFSET (SyncevoServerClass, info_request),
                          NULL, NULL,
                          syncevo_marshal_VOID__STRING_STRING_STRING_STRING_STRING_BOXED,
                          G_TYPE_NONE,
                          6, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_POINTER);
    signals[TEMPLATES_CHANGED] =
            g_signal_new ("templates-changed",
                          G_TYPE_FROM_CLASS (klass),
                          G_SIGNAL_RUN_FIRST | G_SIGNAL_NO_RECURSE,
                          G_STRUCT_OFFSET (SyncevoServerClass, templates_changed),
                          NULL, NULL,
                          g_cclosure_marshal_VOID__VOID,
                          G_TYPE_NONE,
                          0);
    signals[SHUTDOWN] =
            g_signal_new ("shutdown",
                          G_TYPE_FROM_CLASS (klass),
                          G_SIGNAL_RUN_FIRST | G_SIGNAL_NO_RECURSE,
                          G_STRUCT_OFFSET (SyncevoServerClass, shutdown),
                          NULL, NULL,
                          g_cclosure_marshal_VOID__VOID,
                          G_TYPE_NONE,
                          0);
}

SyncevoServer *
syncevo_server_get_default ()
{
    static SyncevoServer *server = NULL;

    if (server == NULL) {
        server = g_object_new (SYNCEVO_TYPE_SERVER, NULL);
        g_object_add_weak_pointer (G_OBJECT (server),
                                   (gpointer) &server);
        return server;
    }

    return g_object_ref (server);
}


static void
get_configs_callback (DBusGProxy *proxy, 
                      char **config_names,
                      GError *error,
                      ServerAsyncData *data)
{
    if (data->callback) {
        (*(SyncevoServerGetConfigsCb)data->callback) (data->server,
                                                      config_names,
                                                      error,
                                                      data->userdata);
    }
    server_async_data_free (data);
}

static gboolean
get_configs_error (ServerAsyncData *data)
{
    GError *error;

    error = g_error_new_literal (SYNCEVO_SERVER_ERROR_QUARK,
                                 SYNCEVO_SERVER_ERROR_NO_DBUS_OBJECT, 
                                 "Could not start service");
    (*(SyncevoServerGetConfigsCb)data->callback) (data->server,
                                                  NULL,
                                                  error,
                                                  data->userdata);
    server_async_data_free (data);

    return FALSE;
}

void
syncevo_server_get_configs (SyncevoServer *syncevo,
                            gboolean template,
                            SyncevoServerGetConfigsCb callback,
                            gpointer userdata)
{
    ServerAsyncData *data;
    SyncevoServerPrivate *priv;

    priv = GET_PRIVATE (syncevo);

    data = server_async_data_new (syncevo, G_CALLBACK (callback), userdata);

    if (!priv->proxy && !syncevo_server_get_new_proxy (syncevo)) {
        if (callback) {
            g_idle_add ((GSourceFunc)get_configs_error, data);
        }
        return;
    }

    org_syncevolution_Server_get_configs_async
            (priv->proxy,
             template,
             (org_syncevolution_Server_get_configs_reply) get_configs_callback,
             data);
}

static void
get_config_callback (DBusGProxy *proxy,
                     SyncevoConfig *configuration,
                     GError *error,
                     ServerAsyncData *data)
{
    if (data->callback) {
        (*(SyncevoServerGetConfigCb)data->callback) (data->server,
                                                     configuration,
                                                     error,
                                                     data->userdata);
    }
    server_async_data_free (data);
}

static gboolean
get_config_error (ServerAsyncData *data)
{
    GError *error;

    error = g_error_new_literal (SYNCEVO_SERVER_ERROR_QUARK,
                                 SYNCEVO_SERVER_ERROR_NO_DBUS_OBJECT, 
                                 "Could not start service");
    (*(SyncevoServerGetConfigCb)data->callback) (data->server,
                                                 NULL,
                                                 error,
                                                 data->userdata);
    server_async_data_free (data);

    return FALSE;
}

void
syncevo_server_get_config (SyncevoServer *syncevo,
                           const char *config_name, 
                           gboolean template,
                           SyncevoServerGetConfigCb callback,
                           gpointer userdata)
{
    ServerAsyncData *data;
    SyncevoServerPrivate *priv;

    priv = GET_PRIVATE (syncevo);

    data = server_async_data_new (syncevo, G_CALLBACK (callback), userdata);

    if (!priv->proxy && !syncevo_server_get_new_proxy (syncevo)) {
        if (callback) {
            g_idle_add ((GSourceFunc)get_config_error, data);
        }
        return;
    }

    org_syncevolution_Server_get_config_async 
            (priv->proxy,
             config_name,
             template,
             (org_syncevolution_Server_get_config_reply) get_config_callback,
             data);
}

static void
get_reports_callback (DBusGProxy *proxy,
                      SyncevoReports *reports,
                      GError *error,
                      ServerAsyncData *data)
{
    if (data->callback) {
        (*(SyncevoServerGetReportsCb)data->callback) (data->server,
                                                      reports,
                                                      error,
                                                      data->userdata);
    }
    server_async_data_free (data);
}

static gboolean
get_reports_error (ServerAsyncData *data)
{
    GError *error;

    error = g_error_new_literal (SYNCEVO_SERVER_ERROR_QUARK,
                                 SYNCEVO_SERVER_ERROR_NO_DBUS_OBJECT, 
                                 "Could not start service");
    (*(SyncevoServerGetReportsCb)data->callback) (data->server,
                                                  NULL,
                                                  error,
                                                  data->userdata);
    server_async_data_free (data);

    return FALSE;    
}

void
syncevo_server_get_reports (SyncevoServer *syncevo,
                            const char *config_name,
                            guint start,
                            guint count,
                            SyncevoServerGetReportsCb callback,
                            gpointer userdata)
{
    ServerAsyncData *data;
    SyncevoServerPrivate *priv;

    priv = GET_PRIVATE (syncevo);

    data = server_async_data_new (syncevo, G_CALLBACK (callback), userdata);

    if (!priv->proxy && !syncevo_server_get_new_proxy (syncevo)) {
        if (callback) {
            g_idle_add ((GSourceFunc)get_reports_error, data);
        }
        return;
    }

    org_syncevolution_Server_get_reports_async
            (priv->proxy,
             config_name,
             start,
             count,
             (org_syncevolution_Server_get_reports_reply) get_reports_callback,
             data);
}

static void
start_session_callback (SyncevoServer *syncevo,
                        char *session_path,
                        GError *error,
                        ServerAsyncData *data)
{
    if (data->callback) {
        (*(SyncevoServerStartSessionCb)data->callback) (data->server,
                                                        session_path,
                                                        error,
                                                        data->userdata);
    }
    server_async_data_free (data);
}

static gboolean
start_session_error (ServerAsyncData *data)
{
    GError *error;

    error = g_error_new_literal (SYNCEVO_SERVER_ERROR_QUARK,
                                 SYNCEVO_SERVER_ERROR_NO_DBUS_OBJECT, 
                                 "Could not start service");
    (*(SyncevoServerStartSessionCb)data->callback) (data->server,
                                                    NULL,
                                                    error,
                                                    data->userdata);
    server_async_data_free (data);

    return FALSE;    
}

void
syncevo_server_start_session (SyncevoServer *syncevo,
                              const char *config_name, 
                              SyncevoServerStartSessionCb callback,
                              gpointer userdata)
{
    ServerAsyncData *data;
    SyncevoServerPrivate *priv;

    priv = GET_PRIVATE (syncevo);

    data = server_async_data_new (syncevo, G_CALLBACK (callback), userdata);

    if (!priv->proxy && !syncevo_server_get_new_proxy (syncevo)) {
        if (callback) {
            g_idle_add ((GSourceFunc)start_session_error, data);
        }
        return;
    }

    org_syncevolution_Server_start_session_async
            (priv->proxy,
             config_name,
             (org_syncevolution_Server_start_session_reply) start_session_callback,
             data);
}

void
syncevo_server_start_no_sync_session (SyncevoServer *syncevo,
                                      const char *config_name,
                                      SyncevoServerStartSessionCb callback,
                                      gpointer userdata)
{
    ServerAsyncData *data;
    SyncevoServerPrivate *priv;
    const char *flags[2] = {"no-sync", NULL};

    priv = GET_PRIVATE (syncevo);

    data = server_async_data_new (syncevo, G_CALLBACK (callback), userdata);

    if (!priv->proxy && !syncevo_server_get_new_proxy (syncevo)) {
        if (callback) {
            g_idle_add ((GSourceFunc)start_session_error, data);
        }
        return;
    }

    org_syncevolution_Server_start_session_with_flags_async
            (priv->proxy,
             config_name,
             flags,
             (org_syncevolution_Server_start_session_reply) start_session_callback,
             data);
}

static void
get_sessions_callback (SyncevoServer *syncevo,
                       SyncevoSessions *sessions,
                       GError *error,
                       ServerAsyncData *data)
{
    if (data->callback) {
        (*(SyncevoServerGetSessionsCb)data->callback) (data->server,
                                                       sessions,
                                                       error,
                                                       data->userdata);
    }
    server_async_data_free (data);
}

static gboolean
get_sessions_error (ServerAsyncData *data)
{
    GError *error;

    error = g_error_new_literal (SYNCEVO_SERVER_ERROR_QUARK,
                                 SYNCEVO_SERVER_ERROR_NO_DBUS_OBJECT, 
                                 "Could not start service");
    (*(SyncevoServerGetSessionsCb)data->callback) (data->server,
                                                   NULL,
                                                   error,
                                                   data->userdata);
    server_async_data_free (data);

    return FALSE;    
}

void
syncevo_server_get_sessions (SyncevoServer *syncevo,
                             SyncevoServerGetSessionsCb callback,
                             gpointer userdata)
{
    ServerAsyncData *data;
    SyncevoServerPrivate *priv;

    priv = GET_PRIVATE (syncevo);

    data = server_async_data_new (syncevo, G_CALLBACK (callback), userdata);

    if (!priv->proxy && !syncevo_server_get_new_proxy (syncevo)) {
        if (callback) {
            g_idle_add ((GSourceFunc)get_sessions_error, data);
        }
        return;
    }

    org_syncevolution_Server_get_sessions_async
            (priv->proxy,
             (org_syncevolution_Server_get_reports_reply) get_sessions_callback,
             data);
}


static void
check_presence_callback (SyncevoServer *syncevo,
                         char *status,
                         char **transports,
                         GError *error,
                         ServerAsyncData *data)
{
    if (data->callback) {
        (*(SyncevoServerGetPresenceCb)data->callback) (data->server,
                                                       status,
                                                       transports,
                                                       error,
                                                       data->userdata);
    }
    server_async_data_free (data);
}

static gboolean
check_presence_error (ServerAsyncData *data)
{
    GError *error;

    error = g_error_new_literal (SYNCEVO_SERVER_ERROR_QUARK,
                                 SYNCEVO_SERVER_ERROR_NO_DBUS_OBJECT, 
                                 "Could not start service");
    (*(SyncevoServerGetPresenceCb)data->callback) (data->server,
                                                   NULL,
                                                   NULL,
                                                   error,
                                                   data->userdata);
    server_async_data_free (data);

    return FALSE;
}


void
syncevo_server_get_presence (SyncevoServer *syncevo,
                             const char *config_name,
                             SyncevoServerGetPresenceCb callback,
                             gpointer userdata)
{
    ServerAsyncData *data;
    SyncevoServerPrivate *priv;

    priv = GET_PRIVATE (syncevo);

    data = server_async_data_new (syncevo, G_CALLBACK (callback), userdata);

    if (!priv->proxy && !syncevo_server_get_new_proxy (syncevo)) {
        if (callback) {
            g_idle_add ((GSourceFunc)check_presence_error, data);
        }
        return;
    }

    org_syncevolution_Server_check_presence_async
            (priv->proxy,
             config_name,
             (org_syncevolution_Server_check_presence_reply) check_presence_callback,
             data);
}

void syncevo_server_check_source (SyncevoServer *server,
                                  const char *config,
                                  const char *source,
                                  SyncevoServerGenericCb callback,
                                  gpointer userdata)
{
    ServerAsyncData *data;
    SyncevoServerPrivate *priv;

    priv = GET_PRIVATE (server);

    data = server_async_data_new (server, G_CALLBACK (callback), userdata);

    if (!priv->proxy) {
        if (callback) {
            g_idle_add ((GSourceFunc)generic_error, data);
        }
        return;
    }

    org_syncevolution_Server_check_source_async 
            (priv->proxy,
             config,
             source,
             (org_syncevolution_Server_check_source_reply) generic_callback,
             data);
}

void
syncevo_server_info_response (SyncevoServer *server,
                              const char *id,
                              const char *state,
                              GHashTable *response,
                              SyncevoServerGenericCb callback,
                              gpointer userdata)
{
    ServerAsyncData *data;
    SyncevoServerPrivate *priv;

    priv = GET_PRIVATE (server);

    data = server_async_data_new (server, G_CALLBACK (callback), userdata);

    if (!priv->proxy) {
        if (callback) {
            g_idle_add ((GSourceFunc)generic_error, data);
        }
        return;
    }

    org_syncevolution_Server_info_response_async
            (priv->proxy,
             id,
             state,
             response,
             (org_syncevolution_Server_info_response_reply) generic_callback,
             data);
}
