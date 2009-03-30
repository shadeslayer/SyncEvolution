#ifndef __SYNCEVO_SERVICE_H__
#define __SYNCEVO_SERVICE_H__

#include <glib-object.h>
#include "syncevo-dbus-types.h"

G_BEGIN_DECLS 

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
	                  int type,
	                  int extra1, int extra2, int extra3);
	void (*source_progress) (SyncevoService *service,
	                         char *server,
	                         char *source,
	                         int type,
	                         int extra1, int extra2, int extra3);
	void (*server_message) (SyncevoService *service,
	                        char *server,
	                        char *message);
	void (*need_password) (SyncevoService *service,
	                       char *server);
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
gboolean syncevo_service_set_password (SyncevoService *service,
                                       char *server,
                                       char *password,
                                       GError **error);


gboolean syncevo_service_get_servers (SyncevoService *service,
                                      char ***servers,
                                      GError **error);
typedef void (*SyncevoGetServersCb) (SyncevoService *service,
                                     char **servers,
                                     GError *error,
                                     gpointer userdata);
void syncevo_service_get_servers_async (SyncevoService *service,
                                        SyncevoGetServersCb callback,
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

G_END_DECLS

#endif
