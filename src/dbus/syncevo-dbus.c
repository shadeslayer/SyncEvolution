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

#include "syncevo-dbus.h"
#include "syncevo-marshal.h"
#include "syncevo-bindings.h"

typedef struct _SyncevoAsyncData {
	SyncevoService *service;
	GCallback callback;
	gpointer userdata;
} SyncevoAsyncData;


enum {
	PROGRESS,
	SERVER_MESSAGE,
	SERVER_SHUTDOWN,
	LAST_SIGNAL
};

typedef struct _SyncevoServicePrivate {
	DBusGProxy *proxy;
} SyncevoServicePrivate;

#define GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), SYNCEVO_TYPE_SERVICE, SyncevoServicePrivate))

static void progress_cb (DBusGProxy *proxy,
                         char *server,
                         char *source,
                         int type,
                         int extra1, int extra2, int extra3,
                         SyncevoService *service);
static void server_message_cb (DBusGProxy *proxy,
                               char *server,
                               char *message,
                               SyncevoService *service);
static void proxy_destroyed (DBusGProxy *proxy,
                             SyncevoService *service);

G_DEFINE_TYPE (SyncevoService, syncevo_service, G_TYPE_OBJECT);

static guint32 signals[LAST_SIGNAL] = {0, };

static void
finalize (GObject *object)
{
	SyncevoServicePrivate *priv;

	priv = GET_PRIVATE (object);

	G_OBJECT_CLASS (syncevo_service_parent_class)->finalize (object);
}

static void
dispose (GObject *object)
{
	SyncevoServicePrivate *priv;

	priv = GET_PRIVATE (object);

	if (priv->proxy) {
		dbus_g_proxy_disconnect_signal (priv->proxy, "Progress",
						G_CALLBACK (progress_cb),
						object);
		dbus_g_proxy_disconnect_signal (priv->proxy, "ServerMessage",
						G_CALLBACK (server_message_cb),
						object);

		g_object_unref (priv->proxy);
		priv->proxy = NULL;
	}

	G_OBJECT_CLASS (syncevo_service_parent_class)->dispose (object);
}

static void progress_cb (DBusGProxy *proxy,
                         char *server,
                         char *source,
                         int type,
                         int extra1, int extra2, int extra3,
                         SyncevoService *service)
{
	g_signal_emit (service, signals[PROGRESS], 0, 
	               server, source, type, extra1, extra2, extra3);
}

static void server_message_cb (DBusGProxy *proxy,
                               char *server,
                               char *message,
                               SyncevoService *service)
{
	g_signal_emit (service, signals[SERVER_MESSAGE], 0, 
	               server, message);
}

static gboolean
syncevo_service_get_new_proxy (SyncevoService *service)
{
	DBusGConnection *connection;
	GError *error;
	guint32 result;
	SyncevoServicePrivate *priv;
	DBusGProxy *proxy;

	priv = GET_PRIVATE (service);
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
	                        G_TYPE_STRING, SYNCEVO_SERVICE_DBUS_SERVICE,
	                        G_TYPE_UINT, 0,
	                        G_TYPE_INVALID,
	                        G_TYPE_UINT, &result,
	                        G_TYPE_INVALID)) {
		g_warning ("StartServiceByName call failed");
	}
	g_object_unref (proxy);

	/* the real proxy */
	proxy = dbus_g_proxy_new_for_name_owner (connection,
	                                         SYNCEVO_SERVICE_DBUS_SERVICE,
	                                         SYNCEVO_SERVICE_DBUS_PATH,
	                                         SYNCEVO_SERVICE_DBUS_INTERFACE,
	                                         &error);
	if (proxy == NULL) {
		g_printerr ("dbus_g_proxy_new_for_name_owner() failed");
		priv->proxy = NULL;
		return FALSE;
	}

	dbus_g_proxy_add_signal (proxy, "Progress",
	                         G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "Progress",
	                             G_CALLBACK (progress_cb), service, NULL);
	dbus_g_proxy_add_signal (proxy, "ServerMessage",
	                         G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "ServerMessage",
	                             G_CALLBACK (server_message_cb), service, NULL);

	g_signal_connect (proxy, "destroy",
	                  G_CALLBACK (proxy_destroyed), service);

	priv->proxy = proxy;
	return TRUE;
}

static void
proxy_destroyed (DBusGProxy *proxy,
                 SyncevoService *service)
{
	SyncevoServicePrivate *priv;
	priv = GET_PRIVATE (service);

	if (priv->proxy) {
		g_object_unref (priv->proxy);
	}
	priv->proxy = NULL;

	g_signal_emit (service, signals[SERVER_SHUTDOWN], 0);
}

static GObject *
constructor (GType                  type,
             guint                  n_construct_properties,
             GObjectConstructParam *construct_properties)
{
	SyncevoService *service;
	SyncevoServicePrivate *priv;

	service = SYNCEVO_SERVICE (G_OBJECT_CLASS (syncevo_service_parent_class)->constructor
			(type, n_construct_properties, construct_properties));
	priv = GET_PRIVATE (service);

	dbus_g_object_register_marshaller (syncevo_marshal_VOID__STRING_STRING_INT_INT_INT_INT,
					   G_TYPE_NONE,
					   G_TYPE_STRING,
					   G_TYPE_STRING,
					   G_TYPE_INT,
					   G_TYPE_INT,
					   G_TYPE_INT,
					   G_TYPE_INT,
					   G_TYPE_INVALID);
	dbus_g_object_register_marshaller (syncevo_marshal_VOID__STRING_STRING,
					   G_TYPE_NONE,
					   G_TYPE_STRING,
					   G_TYPE_STRING,
					   G_TYPE_INVALID);

	syncevo_service_get_new_proxy (service);

	return G_OBJECT (service);
}

static void
syncevo_service_class_init (SyncevoServiceClass *klass)
{
	GObjectClass *o_class = (GObjectClass *) klass;

	o_class->finalize = finalize;
	o_class->dispose = dispose;
	o_class->constructor = constructor;

	g_type_class_add_private (klass, sizeof (SyncevoServicePrivate));

	signals[PROGRESS] = g_signal_new ("progress",
	                                  G_TYPE_FROM_CLASS (klass),
	                                  G_SIGNAL_RUN_FIRST | G_SIGNAL_NO_RECURSE,
	                                  G_STRUCT_OFFSET (SyncevoServiceClass, progress),
	                                  NULL, NULL,
	                                  syncevo_marshal_VOID__STRING_STRING_INT_INT_INT_INT,
	                                  G_TYPE_NONE, 
	                                  6, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT);
	signals[SERVER_MESSAGE] = g_signal_new ("server-message",
	                                  G_TYPE_FROM_CLASS (klass),
	                                  G_SIGNAL_RUN_FIRST | G_SIGNAL_NO_RECURSE,
	                                  G_STRUCT_OFFSET (SyncevoServiceClass, server_message),
	                                  NULL, NULL,
	                                  syncevo_marshal_VOID__STRING_STRING,
	                                  G_TYPE_NONE, 
	                                  2, G_TYPE_STRING, G_TYPE_STRING);
	signals[SERVER_SHUTDOWN] = g_signal_new ("server-shutdown",
	                                  G_TYPE_FROM_CLASS (klass),
	                                  G_SIGNAL_RUN_FIRST | G_SIGNAL_NO_RECURSE,
	                                  G_STRUCT_OFFSET (SyncevoServiceClass, server_shutdown),
	                                  NULL, NULL,
	                                  g_cclosure_marshal_VOID__VOID,
	                                  G_TYPE_NONE, 
	                                  0);
}

static void
syncevo_service_init (SyncevoService *service)
{
}


SyncevoService *
syncevo_service_get_default ()
{
	static SyncevoService *default_service = NULL;

	if (default_service == NULL) {
		default_service = g_object_new (SYNCEVO_TYPE_SERVICE, NULL);
		g_object_add_weak_pointer (G_OBJECT (default_service),
					   (gpointer) &default_service);
		return default_service;
	}

	return g_object_ref (default_service);
}



gboolean syncevo_service_start_sync (SyncevoService *service,
                                     char *server,
                                     GPtrArray *sources,
                                     GError **error)
{
	SyncevoServicePrivate *priv;

	priv = GET_PRIVATE (service);

	if (!priv->proxy && !syncevo_service_get_new_proxy (service)) {
		if (error) {
			*error = g_error_new_literal (g_quark_from_static_string ("syncevo-service"),
			                              SYNCEVO_SERVICE_ERROR_COULD_NOT_START, 
			                              "Could not start service");
		}
		return FALSE;
	}

	return org_Moblin_SyncEvolution_start_sync (priv->proxy, 
	                                            server, 
	                                            sources,
	                                            error);
}

gboolean syncevo_service_abort_sync (SyncevoService *service,
                                     char *server,
                                     GError **error)
{
	SyncevoServicePrivate *priv;

	priv = GET_PRIVATE (service);

	if (!priv->proxy && !syncevo_service_get_new_proxy (service)) {
		if (error) {
			*error = g_error_new_literal (g_quark_from_static_string ("syncevo-service"),
			                              SYNCEVO_SERVICE_ERROR_COULD_NOT_START, 
			                              "Could not start service");
		}
		return FALSE;
	}

	return org_Moblin_SyncEvolution_abort_sync (priv->proxy, 
	                                            server, 
	                                            error);
}

static void
abort_sync_async_callback (DBusGProxy *proxy, 
                           GError *error,
                           SyncevoAsyncData *data)
{
	(*(SyncevoAbortSyncCb)data->callback) (data->service,
	                                       error,
	                                       data->userdata);
	g_slice_free (SyncevoAsyncData, data);
}

static gboolean
abort_sync_async_error (SyncevoAsyncData *data)
{
	GError *error;

	error = g_error_new_literal (g_quark_from_static_string ("syncevo-service"),
								 SYNCEVO_SERVICE_ERROR_COULD_NOT_START, 
								 "Could not start service");
	(*(SyncevoAbortSyncCb)data->callback) (data->service,
	                                       error,
	                                       data->userdata);
	g_slice_free (SyncevoAsyncData, data);

	return FALSE;
}

void 
syncevo_service_abort_sync_async (SyncevoService *service,
                                  char *server,
                                  SyncevoAbortSyncCb callback,
                                  gpointer userdata)
{
	SyncevoAsyncData *data;
	SyncevoServicePrivate *priv;

	priv = GET_PRIVATE (service);

	data = g_slice_new0 (SyncevoAsyncData);
	data->service = service;
	data->callback = G_CALLBACK (callback);
	data->userdata = userdata;

	if (!priv->proxy && !syncevo_service_get_new_proxy (service)) {
		g_idle_add ((GSourceFunc)abort_sync_async_error, data);
		return;
	}

	org_Moblin_SyncEvolution_abort_sync_async 
			(priv->proxy,
			 server,
			 (org_Moblin_SyncEvolution_abort_sync_reply) abort_sync_async_callback,
			 data);
}


gboolean syncevo_service_get_servers (SyncevoService *service,
                                      GPtrArray **servers,
                                      GError **error)
{
	SyncevoServicePrivate *priv;

	priv = GET_PRIVATE (service);

	if (!priv->proxy && !syncevo_service_get_new_proxy (service)) {
		if (error) {
			*error = g_error_new_literal (g_quark_from_static_string ("syncevo-service"),
			                              SYNCEVO_SERVICE_ERROR_COULD_NOT_START, 
			                              "Could not start service");
		}
		return FALSE;
	}

	return org_Moblin_SyncEvolution_get_servers (priv->proxy, 
	                                             servers, 
	                                             error);
}

static void
get_servers_async_callback (DBusGProxy *proxy, 
                            GPtrArray *servers,
                            GError *error,
                            SyncevoAsyncData *data)
{
	(*(SyncevoGetServersCb)data->callback) (data->service,
	                                        servers,
	                                        error,
	                                        data->userdata);
	g_slice_free (SyncevoAsyncData, data);
}

static gboolean
get_servers_async_error (SyncevoAsyncData *data)
{
	GError *error;

	error = g_error_new_literal (g_quark_from_static_string ("syncevo-service"),
								 SYNCEVO_SERVICE_ERROR_COULD_NOT_START, 
								 "Could not start service");
	(*(SyncevoGetServersCb)data->callback) (data->service,
	                                        NULL,
	                                        error,
	                                        data->userdata);
	g_slice_free (SyncevoAsyncData, data);

	return FALSE;
}

void 
syncevo_service_get_servers_async (SyncevoService *service,
                                   SyncevoGetServersCb callback,
                                   gpointer userdata)
{
	SyncevoAsyncData *data;
	SyncevoServicePrivate *priv;

	priv = GET_PRIVATE (service);

	data = g_slice_new0 (SyncevoAsyncData);
	data->service = service;
	data->callback = G_CALLBACK (callback);
	data->userdata = userdata;

	if (!priv->proxy && !syncevo_service_get_new_proxy (service)) {
		g_idle_add ((GSourceFunc)get_servers_async_error, data);
		return;
	}

	org_Moblin_SyncEvolution_get_servers_async 
			(priv->proxy,
			 (org_Moblin_SyncEvolution_get_servers_reply) get_servers_async_callback,
			 data);
}

gboolean syncevo_service_get_templates (SyncevoService *service,
                                        GPtrArray **templates,
                                        GError **error)
{
	SyncevoServicePrivate *priv;

	priv = GET_PRIVATE (service);

	if (!priv->proxy && !syncevo_service_get_new_proxy (service)) {
		if (error) {
			*error = g_error_new_literal (g_quark_from_static_string ("syncevo-service"),
			                              SYNCEVO_SERVICE_ERROR_COULD_NOT_START, 
			                              "Could not start service");
		}
		return FALSE;
	}

	return org_Moblin_SyncEvolution_get_templates (priv->proxy, 
	                                               templates, 
	                                               error);
}

static void
get_templates_async_callback (DBusGProxy *proxy, 
                              GPtrArray *templates,
                              GError *error,
                              SyncevoAsyncData *data)
{
	(*(SyncevoGetTemplatesCb)data->callback) (data->service,
	                                          templates,
	                                          error,
	                                          data->userdata);
	g_slice_free (SyncevoAsyncData, data);
}

static gboolean
get_templates_async_error (SyncevoAsyncData *data)
{
	GError *error;

	error = g_error_new_literal (g_quark_from_static_string ("syncevo-service"),
								 SYNCEVO_SERVICE_ERROR_COULD_NOT_START, 
								 "Could not start service");
	(*(SyncevoGetTemplatesCb)data->callback) (data->service,
	                                          NULL,
	                                          error,
	                                          data->userdata);
	g_slice_free (SyncevoAsyncData, data);

	return FALSE;
}

void syncevo_service_get_templates_async (SyncevoService *service,
                                          SyncevoGetTemplatesCb callback,
                                          gpointer userdata)
{
	SyncevoAsyncData *data;
	SyncevoServicePrivate *priv;

	priv = GET_PRIVATE (service);

	data = g_slice_new0 (SyncevoAsyncData);
	data->service = service;
	data->callback = G_CALLBACK (callback);
	data->userdata = userdata;

	if (!priv->proxy && !syncevo_service_get_new_proxy (service)) {
		g_idle_add ((GSourceFunc)get_templates_async_error, data);
		return;
	}

	org_Moblin_SyncEvolution_get_templates_async 
			(priv->proxy,
			 (org_Moblin_SyncEvolution_get_templates_reply) get_templates_async_callback,
			 data);
}

gboolean syncevo_service_get_template_config (SyncevoService *service,
                                              char *template,
                                              GPtrArray **options,
                                              GError **error)
{
	SyncevoServicePrivate *priv;

	priv = GET_PRIVATE (service);

	if (!priv->proxy && !syncevo_service_get_new_proxy (service)) {
		if (error) {
			*error = g_error_new_literal (g_quark_from_static_string ("syncevo-service"),
			                              SYNCEVO_SERVICE_ERROR_COULD_NOT_START, 
			                              "Could not start service");
		}
		return FALSE;
	}

	return org_Moblin_SyncEvolution_get_template_config (priv->proxy, 
	                                                     template,
	                                                     options, 
	                                                     error);
}

static void
get_template_config_async_callback (DBusGProxy *proxy, 
                                    GPtrArray *options,
                                    GError *error,
                                    SyncevoAsyncData *data)
{
	(*(SyncevoGetTemplateConfigCb)data->callback) (data->service,
	                                               options,
	                                               error,
	                                               data->userdata);
	g_slice_free (SyncevoAsyncData, data);
}

static gboolean
get_template_config_async_error (SyncevoAsyncData *data)
{
	GError *error;

	error = g_error_new_literal (g_quark_from_static_string ("syncevo-service"),
								 SYNCEVO_SERVICE_ERROR_COULD_NOT_START, 
								 "Could not start service");
	(*(SyncevoGetTemplateConfigCb)data->callback) (data->service,
	                                               NULL,
	                                               error,
	                                               data->userdata);
	g_slice_free (SyncevoAsyncData, data);

	return FALSE;
}

void 
syncevo_service_get_template_config_async (SyncevoService *service,
                                           char *template,
                                           SyncevoGetServerConfigCb callback,
                                           gpointer userdata)
{
	SyncevoAsyncData *data;
	SyncevoServicePrivate *priv;

	priv = GET_PRIVATE (service);

	data = g_slice_new0 (SyncevoAsyncData);
	data->service = service;
	data->callback = G_CALLBACK (callback);
	data->userdata = userdata;

	if (!priv->proxy && !syncevo_service_get_new_proxy (service)) {
		g_idle_add ((GSourceFunc)get_template_config_async_error, data);
		return;
	}

	org_Moblin_SyncEvolution_get_template_config_async 
			(priv->proxy,
			 template,
			 (org_Moblin_SyncEvolution_get_server_config_reply) get_template_config_async_callback,
			 data);
}

gboolean syncevo_service_get_server_config (SyncevoService *service,
                                            char *server,
                                            GPtrArray **options,
                                            GError **error)
{
	SyncevoServicePrivate *priv;

	priv = GET_PRIVATE (service);

	if (!priv->proxy && !syncevo_service_get_new_proxy (service)) {
		if (error) {
			*error = g_error_new_literal (g_quark_from_static_string ("syncevo-service"),
			                              SYNCEVO_SERVICE_ERROR_COULD_NOT_START, 
			                              "Could not start service");
		}
		return FALSE;
	}

	return org_Moblin_SyncEvolution_get_server_config (priv->proxy, 
	                                                   server,
	                                                   options, 
	                                                   error);
}

static void
get_server_config_async_callback (DBusGProxy *proxy, 
                                  GPtrArray *options,
                                  GError *error,
                                  SyncevoAsyncData *data)
{
	(*(SyncevoGetServerConfigCb)data->callback) (data->service,
	                                             options,
	                                             error,
	                                             data->userdata);
	g_slice_free (SyncevoAsyncData, data);
}

static gboolean
get_server_config_async_error (SyncevoAsyncData *data)
{
	GError *error;

	error = g_error_new_literal (g_quark_from_static_string ("syncevo-service"),
								 SYNCEVO_SERVICE_ERROR_COULD_NOT_START, 
								 "Could not start service");
	(*(SyncevoGetServerConfigCb)data->callback) (data->service,
	                                             NULL,
	                                             error,
	                                             data->userdata);

	return FALSE;
}

void 
syncevo_service_get_server_config_async (SyncevoService *service,
                                         char *server,
                                         SyncevoGetServerConfigCb callback,
                                         gpointer userdata)
{
	SyncevoAsyncData *data;
	SyncevoServicePrivate *priv;

	priv = GET_PRIVATE (service);

	data = g_slice_new0 (SyncevoAsyncData);
	data->service = service;
	data->callback = G_CALLBACK (callback);
	data->userdata = userdata;

	if (!priv->proxy && !syncevo_service_get_new_proxy (service)) {
		g_idle_add ((GSourceFunc)get_server_config_async_error, data);
		return;
	}

	org_Moblin_SyncEvolution_get_server_config_async 
			(priv->proxy,
			 server,
			 (org_Moblin_SyncEvolution_get_server_config_reply) get_server_config_async_callback,
			 data);
}


gboolean syncevo_service_set_server_config (SyncevoService *service,
                                            char *server,
                                            GPtrArray *options,
                                            GError **error)
{
	SyncevoServicePrivate *priv;

	priv = GET_PRIVATE (service);

	if (!priv->proxy && !syncevo_service_get_new_proxy (service)) {
		if (error) {
			*error = g_error_new_literal (g_quark_from_static_string ("syncevo-service"),
			                              SYNCEVO_SERVICE_ERROR_COULD_NOT_START, 
			                              "Could not start service");
		}
		return FALSE;
	}

	return org_Moblin_SyncEvolution_set_server_config (priv->proxy, 
	                                                   server,
	                                                   options, 
	                                                   error);
}

static void
set_server_config_async_callback (DBusGProxy *proxy, 
                                  GError *error,
                                  SyncevoAsyncData *data)
{
	(*(SyncevoSetServerConfigCb)data->callback) (data->service,
	                                             error,
	                                             data->userdata);
	g_slice_free (SyncevoAsyncData, data);
}

static gboolean
set_server_config_async_error (SyncevoAsyncData *data)
{
	GError *error;

	error = g_error_new_literal (g_quark_from_static_string ("syncevo-service"),
								 SYNCEVO_SERVICE_ERROR_COULD_NOT_START, 
								 "Could not start service");
	(*(SyncevoSetServerConfigCb)data->callback) (data->service,
	                                             error,
	                                             data->userdata);
	g_slice_free (SyncevoAsyncData, data);

	return FALSE;
}

void 
syncevo_service_set_server_config_async (SyncevoService *service,
                                         char *server,
                                         GPtrArray *options,
                                         SyncevoSetServerConfigCb callback,
                                         gpointer userdata)
{
	SyncevoAsyncData *data;
	SyncevoServicePrivate *priv;

	priv = GET_PRIVATE (service);

	data = g_slice_new0 (SyncevoAsyncData);
	data->service = service;
	data->callback = G_CALLBACK (callback);
	data->userdata = userdata;

	if (!priv->proxy && !syncevo_service_get_new_proxy (service)) {
		g_idle_add ((GSourceFunc)set_server_config_async_error, data);
		return;
	}

	org_Moblin_SyncEvolution_set_server_config_async
			(priv->proxy,
			 server,
			 options,
			 (org_Moblin_SyncEvolution_set_server_config_reply) set_server_config_async_callback,
			 data);
}

gboolean 
syncevo_service_remove_server_config (SyncevoService *service,
                                      char *server,
                                      GError **error)
{
	SyncevoServicePrivate *priv;

	priv = GET_PRIVATE (service);

	if (!priv->proxy && !syncevo_service_get_new_proxy (service)) {
		if (error) {
			*error = g_error_new_literal (g_quark_from_static_string ("syncevo-service"),
			                              SYNCEVO_SERVICE_ERROR_COULD_NOT_START, 
			                              "Could not start service");
		}
		return FALSE;
	}

	return org_Moblin_SyncEvolution_remove_server_config (priv->proxy, 
	                                                      server,
	                                                      error);
}

static void
remove_server_config_async_callback (DBusGProxy *proxy, 
                                     GError *error,
                                     SyncevoAsyncData *data)
{
	(*(SyncevoRemoveServerConfigCb)data->callback) (data->service,
	                                             error,
	                                             data->userdata);
	g_slice_free (SyncevoAsyncData, data);
}

static gboolean
remove_server_config_async_error (SyncevoAsyncData *data)
{
	GError *error;

	error = g_error_new_literal (g_quark_from_static_string ("syncevo-service"),
								 SYNCEVO_SERVICE_ERROR_COULD_NOT_START, 
								 "Could not start service");
	(*(SyncevoRemoveServerConfigCb)data->callback) (data->service,
	                                                error,
	                                                data->userdata);
	g_slice_free (SyncevoAsyncData, data);

	return FALSE;
}

void 
syncevo_service_remove_server_config_async (SyncevoService *service,
                                            char *server,
                                            SyncevoRemoveServerConfigCb callback,
                                            gpointer userdata)
{
	SyncevoAsyncData *data;
	SyncevoServicePrivate *priv;

	priv = GET_PRIVATE (service);

	data = g_slice_new0 (SyncevoAsyncData);
	data->service = service;
	data->callback = G_CALLBACK (callback);
	data->userdata = userdata;

	if (!priv->proxy && !syncevo_service_get_new_proxy (service)) {
		g_idle_add ((GSourceFunc)remove_server_config_async_error, data);
		return;
	}

	org_Moblin_SyncEvolution_remove_server_config_async 
			(priv->proxy,
			 server,
			 (org_Moblin_SyncEvolution_remove_server_config_reply) remove_server_config_async_callback,
			 data);
}

gboolean 
syncevo_service_get_sync_reports (SyncevoService *service,
                                  char *server,
                                  int count,
                                  GPtrArray **reports,
                                  GError **error)
{
	SyncevoServicePrivate *priv;

	priv = GET_PRIVATE (service);

	if (!priv->proxy && !syncevo_service_get_new_proxy (service)) {
		if (error) {
			*error = g_error_new_literal (g_quark_from_static_string ("syncevo-service"),
			                              SYNCEVO_SERVICE_ERROR_COULD_NOT_START, 
			                              "Could not start service");
		}
		return FALSE;
	}

	return org_Moblin_SyncEvolution_get_sync_reports ( 
			priv->proxy,
			server,
			count,
			reports,
			error);
}

static void
get_sync_reports_async_callback (DBusGProxy *proxy, 
                                 GPtrArray *reports,
                                 GError *error,
                                 SyncevoAsyncData *data)
{
	(*(SyncevoGetSyncReportsCb)data->callback) (data->service,
	                                            reports,
	                                            error,
	                                            data->userdata);
	g_slice_free (SyncevoAsyncData, data);
}

static gboolean
get_sync_reports_async_error (SyncevoAsyncData *data)
{
	GError *error;

	error = g_error_new_literal (g_quark_from_static_string ("syncevo-service"),
								 SYNCEVO_SERVICE_ERROR_COULD_NOT_START, 
								 "Could not start service");
	(*(SyncevoGetSyncReportsCb)data->callback) (data->service,
	                                            NULL,
	                                            error,
	                                            data->userdata);
	g_slice_free (SyncevoAsyncData, data);

	return FALSE;
}

void 
syncevo_service_get_sync_reports_async (SyncevoService *service,
                                       char *server,
                                       int count,
                                       SyncevoGetSyncReportsCb callback,
                                       gpointer userdata)
{
	SyncevoAsyncData *data;
	SyncevoServicePrivate *priv;

	priv = GET_PRIVATE (service);

	data = g_slice_new0 (SyncevoAsyncData);
	data->service = service;
	data->callback = G_CALLBACK (callback);
	data->userdata = userdata;

	if (!priv->proxy && !syncevo_service_get_new_proxy (service)) {
		g_idle_add ((GSourceFunc)get_sync_reports_async_error, data);
		return;
	}

	org_Moblin_SyncEvolution_get_sync_reports_async 
			(priv->proxy,
			 server,
			 count,
			 (org_Moblin_SyncEvolution_get_sync_reports_reply) get_sync_reports_async_callback,
			 data);
}
