if COND_DBUS

noinst_LTLIBRARIES = libsyncevodbusserver.la

server_cpp_files = 				\
	../../NotificationBackendNoop.cpp 	\
	../../NotificationBackendLibnotify.cpp 	\
	../../NotificationBackendMLite.cpp 	\
	../../NotificationManagerFactory.cpp 	\
	auto-sync-manager.cpp			\
	bluez-manager.cpp			\
	client.cpp				\
	connection.cpp				\
	connman-client.cpp			\
	dbus-sync.cpp				\
	dbus-transport-agent.cpp		\
	dbus-user-interface.cpp			\
	exceptions.cpp				\
	info-req.cpp				\
	network-manager-client.cpp		\
	presence-status.cpp			\
	progress-data.cpp			\
	read-operations.cpp			\
	server.cpp				\
	session.cpp

server_h_files = \
	$(server_cpp_files:%.cpp=%.h)		\
	../../NotificationBackendBase.h 	\
	../../NotificationManager.h 		\
	../../NotificationManagerBase.h 	\
	auto-term.h				\
	cmdline-wrapper.h			\
	resource.h				\
	restart.h				\
	session-listener.h			\
	source-progress.h			\
	source-status.h				\
	timeout.h				\
	timer.h

libsyncevodbusserver_la_SOURCES = 		\
	$(server_h_files)			\
	$(server_cpp_files)			\
	main.cpp

libsyncevodbusserver_la_LIBADD = $(KEYRING_LIBS) $(LIBNOTIFY_LIBS) $(MLITE_LIBS) $(KDE_KWALLET_LIBS) $(DBUS_LIBS) $(LIBSOUP_LIBS)
libsyncevodbusserver_la_CPPFLAGS = -DHAVE_CONFIG_H  -DSYNCEVOLUTION_LOCALEDIR=\"${SYNCEVOLUTION_LOCALEDIR}\" -I$(top_srcdir)/src -I$(top_srcdir)/test -I$(top_srcdir) $(BACKEND_CPPFLAGS)
libsyncevodbusserver_la_CXXFLAGS = $(SYNCEVOLUTION_CXXFLAGS) $(CORE_CXXFLAGS) $(SYNTHESIS_CFLAGS) $(GLIB_CFLAGS) $(DBUS_CFLAGS) $(LIBSOUP_CFLAGS) $(KEYRING_CFLAGS) $(LIBNOTIFY_CFLAGS) $(MLITE_CFLAGS) $(KDE_KWALLET_CFLAGS)

# Deal with .service, .desktop and startup script files.
CLEANFILES =	org.syncevolution.service \
		$(SYNCEVO_DBUS_SERVER_DESKTOP) \
		$(SYNCEVO_DBUS_SERVER_SCRIPT)

SYNCEVO_DBUS_SERVER_SCRIPT = syncevo-dbus-server-startup.sh
SYNCEVO_DBUS_SERVER_SCRIPT_IN = $(SYNCEVO_DBUS_SERVER_SCRIPT).in

autostartdir = $(sysconfdir)/xdg/autostart
SYNCEVO_DBUS_SERVER_DESKTOP = syncevo-dbus-server.desktop
SYNCEVO_DBUS_SERVER_DESKTOP_IN = $(SYNCEVO_DBUS_SERVER_DESKTOP).in

libexec_SCRIPTS = $(SYNCEVO_DBUS_SERVER_SCRIPT)
$(libexec_SCRIPTS): $(SYNCEVO_DBUS_SERVER_SCRIPT_IN) Makefile
	@sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@
autostart_DATA = $(SYNCEVO_DBUS_SERVER_DESKTOP)
$(autostart_DATA): $(SYNCEVO_DBUS_SERVER_DESKTOP_IN) Makefile
	@sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

service_in_files = org.syncevolution.service.in
servicedir = $(DBUS_SERVICES_DIR)
service_DATA = $(service_in_files:.service.in=.service)
$(service_DATA): $(service_in_files) Makefile
	@sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

EXTRA_DIST =	$(service_in_files) \
		$(SYNCEVO_DBUS_SERVER_SCRIPT_IN) \
		$(SYNCEVO_DBUS_SERVER_DESKTOP_IN)

endif