noinst_LTLIBRARIES = libsyncevodbusserver.la

libsyncevodbusserver_la_SOURCES = \
	../NotificationBackendBase.h \
	../NotificationBackendNoop.h \
	../NotificationBackendNoop.cpp \
	../NotificationBackendLibnotify.h \
	../NotificationBackendLibnotify.cpp \
	../NotificationBackendMLite.h \
	../NotificationBackendMLite.cpp \
	../NotificationManager.h \
	../NotificationManagerBase.h \
	../NotificationManagerFactory.h \
	../NotificationManagerFactory.cpp \
	auto-sync-manager.h	\
	auto-sync-manager.cpp	\
	auto-term.h		\
	bluez-manager.h		\
	bluez-manager.cpp	\
	client.h		\
	client.cpp		\
	common.h		\
	connman-client.h	\
	connman-client.cpp	\
	dbus-user-interface.h	\
	dbus-user-interface.cpp	\
	info-req.h		\
	info-req.cpp		\
	main.cpp		\
	network-manager-client.h	\
	network-manager-client.cpp	\
	presence-status.h	\
	presence-status.cpp	\
	read-operations.h	\
	read-operations.cpp	\
	restart.h		\
	session-listener.h	\
	source-progress.h	\
	source-status.h		\
	syncevo-dbus-server.h	\
	syncevo-dbus-server.cpp \
	syncevo-exceptions.h	\
	syncevo-exceptions.cpp	\
	timeout.h		\
	timer.h

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
