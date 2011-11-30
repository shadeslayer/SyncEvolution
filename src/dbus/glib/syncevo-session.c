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

#include "syncevo-session.h"
#include "syncevo-marshal.h"
#include "syncevo-session-bindings.h"

typedef struct _SessionAsyncData {
    SyncevoSession *session;
    GCallback callback;
    gpointer userdata;
} SessionAsyncData;

enum {
	PROP_0,
	PROP_SESSION_PATH,
};

enum {
    STATUS_CHANGED,
    PROGRESS_CHANGED,
    LAST_SIGNAL
};
static guint32 signals[LAST_SIGNAL] = {0, };

typedef struct _SyncevoSessionPrivate {
    DBusGProxy *proxy;
    char *path;
} SyncevoSessionPrivate;

#define GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), SYNCEVO_TYPE_SESSION, SyncevoSessionPrivate))
G_DEFINE_TYPE (SyncevoSession, syncevo_session, G_TYPE_OBJECT);

static SessionAsyncData*
session_async_data_new (SyncevoSession *session,
                        GCallback callback,
                        gpointer userdata)
{
    SessionAsyncData *data;

    data = g_slice_new0 (SessionAsyncData);
    data->session = session;
    data->callback = G_CALLBACK (callback);
    data->userdata = userdata;

    return data;
}

static void
session_async_data_free (SessionAsyncData *data)
{
    g_slice_free (SessionAsyncData, data);
}

static void
status_changed_cb (DBusGProxy *proxy,
                   char *status,
                   guint error_code,
                   SyncevoSourceStatuses *source_statuses,
                   SyncevoSession *session)
{
    g_signal_emit (session, signals[STATUS_CHANGED], 0, 
                   syncevo_session_status_from_string (status),
                   error_code,
                   source_statuses);
}

static void
progress_changed_cb (DBusGProxy *proxy,
                     int progress,
                     SyncevoSourceProgresses *source_progresses,
                     SyncevoSession *session)
{
    g_signal_emit (session, signals[PROGRESS_CHANGED], 0, 
                   progress,
                   source_progresses);
}

static void
syncevo_session_get_property (GObject *object, guint property_id,
                              GValue *value, GParamSpec *pspec)
{
    SyncevoSession *session = SYNCEVO_SESSION (object);
    SyncevoSessionPrivate *priv;

    priv = GET_PRIVATE (session);

    switch (property_id) {
    case PROP_SESSION_PATH:
        g_value_set_string (value, priv->path);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void 
syncevo_session_set_path (SyncevoSession *session, const char *path)
{
    SyncevoSessionPrivate *priv;
    DBusGConnection *connection;
    GError *error;

    priv = GET_PRIVATE (session);
    error = NULL;

    priv->path = g_strdup (path);

    if (!priv->path) {
        return;
    }

    connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
    if (connection == NULL) {
        g_printerr ("Failed to open connection to bus: %s\n",
                    error->message);
        g_error_free (error);
        priv->proxy = NULL;
        return;
    }


    priv->proxy = dbus_g_proxy_new_for_name (connection,
                                             SYNCEVO_SESSION_DBUS_SERVICE,
                                             priv->path,
                                             SYNCEVO_SESSION_DBUS_INTERFACE);
    if (priv->proxy == NULL) {
        g_printerr ("dbus_g_proxy_new_for_name() failed for path '%s'", priv->path);
        return;
    }

    dbus_g_proxy_add_signal (priv->proxy, "StatusChanged",
                             G_TYPE_STRING, G_TYPE_UINT, SYNCEVO_TYPE_SOURCE_STATUSES, G_TYPE_INVALID);
    dbus_g_proxy_connect_signal (priv->proxy, "StatusChanged",
                                 G_CALLBACK (status_changed_cb), session, NULL);

    dbus_g_proxy_add_signal (priv->proxy, "ProgressChanged",
                             G_TYPE_INT, SYNCEVO_TYPE_SOURCE_PROGRESSES, G_TYPE_INVALID);
    dbus_g_proxy_connect_signal (priv->proxy, "ProgressChanged",
                                 G_CALLBACK (progress_changed_cb), session, NULL);

}

static void
syncevo_session_set_property (GObject *object, guint property_id,
                              const GValue *value, GParamSpec *pspec)
{
    SyncevoSession *session = SYNCEVO_SESSION (object);

    switch (property_id) {
    case PROP_SESSION_PATH:
        syncevo_session_set_path (session, g_value_get_string (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
dispose (GObject *object)
{
    SyncevoSessionPrivate *priv;

    priv = GET_PRIVATE (object);

    if (priv->proxy) {
        dbus_g_proxy_disconnect_signal (priv->proxy, "ProgressChanged",
                                        G_CALLBACK (progress_changed_cb),
                                        object);
        dbus_g_proxy_disconnect_signal (priv->proxy, "StatusChanged",
                                        G_CALLBACK (status_changed_cb),
                                        object);

        /* TODO: need to do this async... */
        org_syncevolution_Session_detach (priv->proxy, NULL);

        g_object_unref (priv->proxy);
        priv->proxy = NULL;
    }

    G_OBJECT_CLASS (syncevo_session_parent_class)->dispose (object);
}

static void
syncevo_session_class_init (SyncevoSessionClass *klass)
{
    GObjectClass *o_class = (GObjectClass *) klass;
    GParamSpec *pspec;

    o_class->dispose = dispose;
    o_class->set_property = syncevo_session_set_property;
    o_class->get_property = syncevo_session_get_property;

    g_type_class_add_private (klass, sizeof (SyncevoSessionPrivate));

    signals[STATUS_CHANGED] =
            g_signal_new ("status-changed",
                          G_TYPE_FROM_CLASS (klass),
                          G_SIGNAL_RUN_FIRST | G_SIGNAL_NO_RECURSE,
                          G_STRUCT_OFFSET (SyncevoSessionClass, status_changed),
                          NULL, NULL,
                          syncevo_marshal_VOID__UINT_UINT_BOXED,
                          G_TYPE_NONE, 
                          3, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_POINTER);
    signals[PROGRESS_CHANGED] =
            g_signal_new ("progress-changed",
                          G_TYPE_FROM_CLASS (klass),
                          G_SIGNAL_RUN_FIRST | G_SIGNAL_NO_RECURSE,
                          G_STRUCT_OFFSET (SyncevoSessionClass, progress_changed),
                          NULL, NULL,
                          syncevo_marshal_VOID__INT_BOXED,
                          G_TYPE_NONE, 
                          2, G_TYPE_INT, G_TYPE_POINTER);

    pspec = g_param_spec_string ("session-path",
                                 "Session path",
                                 "The D-Bus path this Syncevolution session uses",
                                 NULL,
                                 G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (o_class, PROP_SESSION_PATH, pspec);
}

static void
syncevo_session_init (SyncevoSession *session)
{
    /* ProgressChanged */
    dbus_g_object_register_marshaller (syncevo_marshal_VOID__INT_BOXED,
                                       G_TYPE_NONE,
                                       G_TYPE_INT,
                                       G_TYPE_BOXED,
                                       G_TYPE_INVALID);
    /* StatusChanged */
    dbus_g_object_register_marshaller (syncevo_marshal_VOID__UINT_UINT_BOXED,
                                       G_TYPE_NONE,
                                       G_TYPE_STRING,
                                       G_TYPE_UINT,
                                       G_TYPE_BOXED,
                                       G_TYPE_INVALID);
}



static void
generic_callback (DBusGProxy *proxy,
                  GError *error,
                  SessionAsyncData *data)
{
    if (data->callback) {
        (*(SyncevoSessionGenericCb)data->callback) (data->session,
                                                    error,
                                                    data->userdata);
    }
    session_async_data_free (data);
}

static gboolean
generic_error (SessionAsyncData *data)
{
    GError *error;

    error = g_error_new_literal (g_quark_from_static_string ("syncevo-session"),
                                 SYNCEVO_SESSION_ERROR_NO_DBUS_OBJECT, 
                                 "The D-Bus object does not exist");
    (*(SyncevoSessionGenericCb)data->callback) (data->session,
                                                error,
                                                data->userdata);
    session_async_data_free (data);

    return FALSE;
}

static void
get_config_name_callback (DBusGProxy *proxy,
                          char *name,
                          GError *error,
                          SessionAsyncData *data)
{
    if (data->callback) {
        (*(SyncevoSessionGetConfigNameCb)data->callback) (data->session,
                                                          name,
                                                          error,
                                                          data->userdata);
    }
    session_async_data_free (data);
}

void
syncevo_session_get_config_name (SyncevoSession *session,
                                 SyncevoSessionGetConfigNameCb callback,
                                 gpointer userdata)
{
    SessionAsyncData *data;
    SyncevoSessionPrivate *priv;

    priv = GET_PRIVATE (session);

    data = session_async_data_new (session, G_CALLBACK (callback), userdata);

    if (!priv->proxy) {
        if (callback) {
            g_idle_add ((GSourceFunc)generic_error, data);
        }
        return;
    }

    org_syncevolution_Session_get_config_name_async
            (priv->proxy,
             (org_syncevolution_Session_get_config_name_reply) get_config_name_callback,
             data);
}

static void
get_config_callback (DBusGProxy *proxy,
                     SyncevoConfig *configuration,
                     GError *error,
                     SessionAsyncData *data)
{
    if (data->callback) {
        (*(SyncevoSessionGetConfigCb)data->callback) (data->session,
                                                      configuration,
                                                      error,
                                                      data->userdata);
    }
    session_async_data_free (data);
}

static gboolean
get_config_error (SessionAsyncData *data)
{
    GError *error;

    error = g_error_new_literal (g_quark_from_static_string ("syncevo-session"),
                                 SYNCEVO_SESSION_ERROR_NO_DBUS_OBJECT, 
                                 "The D-Bus object does not exist");
    (*(SyncevoSessionGetConfigCb)data->callback) (data->session,
                                                  NULL,
                                                  error,
                                                  data->userdata);
    session_async_data_free (data);

    return FALSE;
}

void
syncevo_session_get_config (SyncevoSession *session,
                            gboolean template,
                            SyncevoSessionGetConfigCb callback,
                            gpointer userdata)
{
    SessionAsyncData *data;
    SyncevoSessionPrivate *priv;

    priv = GET_PRIVATE (session);

    data = session_async_data_new (session, G_CALLBACK (callback), userdata);

    if (!priv->proxy) {
        if (callback) {
            g_idle_add ((GSourceFunc)get_config_error, data);
        }
        return;
    }
    org_syncevolution_Session_get_config_async
            (priv->proxy,
             template,
             (org_syncevolution_Session_get_config_reply) get_config_callback,
             data);
}

void
syncevo_session_set_config (SyncevoSession *session,
                            gboolean update,
                            gboolean temporary,
                            SyncevoConfig *config,
                            SyncevoSessionGenericCb callback,
                            gpointer userdata)
{
    SessionAsyncData *data;
    SyncevoSessionPrivate *priv;

    priv = GET_PRIVATE (session);

    data = session_async_data_new (session, G_CALLBACK (callback), userdata);

    if (!priv->proxy) {
        if (callback) {
            g_idle_add ((GSourceFunc)generic_error, data);
        }
        return;
    }

    org_syncevolution_Session_set_config_async
            (priv->proxy,
             update,
             temporary,
             config,
             (org_syncevolution_Session_set_config_reply) generic_callback,
             data);
}

static void
get_reports_callback (DBusGProxy *proxy,
                      SyncevoReports *reports,
                      GError *error,
                      SessionAsyncData *data)
{
    if (data->callback) {
        (*(SyncevoSessionGetReportsCb)data->callback) (data->session,
                                                       reports,
                                                       error,
                                                       data->userdata);
    }
    session_async_data_free (data);
}

static gboolean
get_reports_error (SessionAsyncData *data)
{
    GError *error;

    error = g_error_new_literal (g_quark_from_static_string ("syncevo-session"),
                                 SYNCEVO_SESSION_ERROR_NO_DBUS_OBJECT, 
                                 "The D-Bus object does not exist");
    (*(SyncevoSessionGetReportsCb)data->callback) (data->session,
                                                   NULL,
                                                   error,
                                                   data->userdata);
    session_async_data_free (data);

    return FALSE;
}

void
syncevo_session_get_reports (SyncevoSession *session,
                             guint start,
                             guint count,
                             SyncevoSessionGetReportsCb callback,
                             gpointer userdata)
{
    SessionAsyncData *data;
    SyncevoSessionPrivate *priv;

    priv = GET_PRIVATE (session);

    data = session_async_data_new (session, G_CALLBACK (callback), userdata);

    if (!priv->proxy) {
        if (callback) {
            g_idle_add ((GSourceFunc)get_reports_error, data);
        }
        return;
    }

    org_syncevolution_Session_get_reports_async
            (priv->proxy,
             start,
             count,
             (org_syncevolution_Session_get_reports_reply)get_reports_callback,
             data);
}

void
syncevo_session_sync (SyncevoSession *session,
                      SyncevoSyncMode mode,
                      SyncevoSourceModes *source_modes,
                      SyncevoSessionGenericCb callback,
                      gpointer userdata)
{
    SessionAsyncData *data;
    SyncevoSessionPrivate *priv;

    priv = GET_PRIVATE (session);

    data = session_async_data_new (session, G_CALLBACK (callback), userdata);

    if (!priv->proxy) {
        if (callback) {
            g_idle_add ((GSourceFunc)generic_error, data);
        }
        return;
    }

    org_syncevolution_Session_sync_async
            (priv->proxy,
             syncevo_sync_mode_to_string (mode),
             source_modes,
             (org_syncevolution_Session_sync_reply) generic_callback,
             data);
}

void
syncevo_session_abort (SyncevoSession *session,
                       SyncevoSessionGenericCb callback,
                       gpointer userdata)
{
    SessionAsyncData *data;
    SyncevoSessionPrivate *priv;

    priv = GET_PRIVATE (session);

    data = session_async_data_new (session, G_CALLBACK (callback), userdata);

    if (!priv->proxy) {
        if (callback) {
            g_idle_add ((GSourceFunc)generic_error, data);
        }
        return;
    }

    org_syncevolution_Session_abort_async
            (priv->proxy,
             (org_syncevolution_Session_abort_reply) generic_callback,
             data);
}

void
syncevo_session_suspend (SyncevoSession *session,
                         SyncevoSessionGenericCb callback,
                         gpointer userdata)
{
    SessionAsyncData *data;
    SyncevoSessionPrivate *priv;

    priv = GET_PRIVATE (session);

    data = session_async_data_new (session, G_CALLBACK (callback), userdata);

    if (!priv->proxy) {
        if (callback) {
            g_idle_add ((GSourceFunc)generic_error, data);
        }
        return;
    }

    org_syncevolution_Session_suspend_async
            (priv->proxy,
             (org_syncevolution_Session_suspend_reply) generic_callback,
             data);
}

static void
get_status_callback (DBusGProxy *proxy,
                     char *status,
                     guint error_code,
                     SyncevoSourceStatuses *sources,
                     GError *error,
                     SessionAsyncData *data)
{
    if (data->callback) {
        (*(SyncevoSessionGetStatusCb)data->callback) (data->session,
                                                      syncevo_session_status_from_string (status),
                                                      error_code,
                                                      sources,
                                                      error,
                                                      data->userdata);
    }
    session_async_data_free (data);
}

static gboolean
get_status_error (SessionAsyncData *data)
{
    GError *error;

    error = g_error_new_literal (g_quark_from_static_string ("syncevo-session"),
                                 SYNCEVO_SESSION_ERROR_NO_DBUS_OBJECT, 
                                 "The D-Bus object does not exist");
    (*(SyncevoSessionGetStatusCb)data->callback) (data->session,
                                                  SYNCEVO_STATUS_UNKNOWN,
                                                  0,
                                                  NULL,
                                                  error,
                                                  data->userdata);
    session_async_data_free (data);

    return FALSE;
}

void
syncevo_session_get_status (SyncevoSession *session,
                            SyncevoSessionGetStatusCb callback,
                            gpointer userdata)
{
    SessionAsyncData *data;
    SyncevoSessionPrivate *priv;

    priv = GET_PRIVATE (session);

    data = session_async_data_new (session, G_CALLBACK (callback), userdata);

    if (!priv->proxy) {
        if (callback) {
            g_idle_add ((GSourceFunc)get_status_error, data);
        }
        return;
    }

    org_syncevolution_Session_get_status_async
            (priv->proxy,
             (org_syncevolution_Session_get_status_reply) get_status_callback,
             data);
}
static void
get_progress_callback (DBusGProxy *proxy,
                       int progress,
                       SyncevoSourceProgresses *source_progresses,
                       GError *error,
                       SessionAsyncData *data)
{
    if (data->callback) {
        (*(SyncevoSessionGetProgressCb)data->callback) (data->session,
                                                        progress,
                                                        source_progresses,
                                                        error,
                                                        data->userdata);
    }
    session_async_data_free (data);
}

static gboolean
get_progress_error (SessionAsyncData *data)
{
    GError *error;

    error = g_error_new_literal (g_quark_from_static_string ("syncevo-session"),
                                 SYNCEVO_SESSION_ERROR_NO_DBUS_OBJECT, 
                                 "The D-Bus object does not exist");
    (*(SyncevoSessionGetProgressCb)data->callback) (data->session,
                                                    -1,
                                                    NULL,
                                                    error,
                                                    data->userdata);
    session_async_data_free (data);

    return FALSE;
}

void
syncevo_session_get_progress (SyncevoSession *session,
                              SyncevoSessionGetProgressCb callback,
                              gpointer userdata)
{
    SessionAsyncData *data;
    SyncevoSessionPrivate *priv;

    priv = GET_PRIVATE (session);

    data = session_async_data_new (session, G_CALLBACK (callback), userdata);

    if (!priv->proxy) {
        if (callback) {
            g_idle_add ((GSourceFunc)get_progress_error, data);
        }
        return;
    }

    org_syncevolution_Session_get_progress_async
            (priv->proxy,
             (org_syncevolution_Session_get_progress_reply) get_progress_callback,
             data);
}

void
syncevo_session_check_source (SyncevoSession *session,
                              const char *source,
                              SyncevoSessionGenericCb callback,
                              gpointer userdata)
{
    SessionAsyncData *data;
    SyncevoSessionPrivate *priv;

    priv = GET_PRIVATE (session);

    data = session_async_data_new (session, G_CALLBACK (callback), userdata);

    if (!priv->proxy) {
        if (callback) {
            g_idle_add ((GSourceFunc)generic_error, data);
        }
        return;
    }

    org_syncevolution_Session_check_source_async
            (priv->proxy,
             source,
             (org_syncevolution_Session_check_source_reply) generic_callback,
             data);
}

void
syncevo_session_restore (SyncevoSession *session,
                         const char *backup_dir,
                         const gboolean before,
                         const char **sources,
                         SyncevoSessionGenericCb callback,
                         gpointer userdata)
{
    SessionAsyncData *data;
    SyncevoSessionPrivate *priv;

    priv = GET_PRIVATE (session);

    data = session_async_data_new (session, G_CALLBACK (callback), userdata);

    if (!priv->proxy) {
        if (callback) {
            g_idle_add ((GSourceFunc)generic_error, data);
        }
        return;
    }

    org_syncevolution_Session_restore_async
            (priv->proxy,
             backup_dir,
             before,
             sources,
             (org_syncevolution_Session_check_source_reply) generic_callback,
             data);
}

const char*
syncevo_session_get_path (SyncevoSession *session)
{
    SyncevoSessionPrivate *priv;

    priv = GET_PRIVATE (session);
    return priv->path;
}

SyncevoSession*
syncevo_session_new (const char *path)
{
    return g_object_new (SYNCEVO_TYPE_SESSION,
                         "session-path", path,
                         NULL);
}
