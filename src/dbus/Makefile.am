lib_LTLIBRARIES = libsyncevo-dbus.la

SUBDIRS = interfaces

AM_CPPFLAGS = -I$(top_srcdir) -Iinterfaces $(SYNTHESIS_CFLAGS)

DISTCLEANFILES = syncevo-dbus.pc
EXTRA_DIST = syncevo-dbus.pc.in
pkgconfig_DATA = syncevo-dbus.pc
pkgconfigdir = $(libdir)/pkgconfig

noinst_PROGRAMS = test-syncevo-dbus

test_syncevo_dbus_SOURCES = test.c
test_syncevo_dbus_LDADD = $(DBUS_GLIB_LIBS) libsyncevo-dbus.la
test_syncevo_dbus_CFLAGS = $(DBUS_GLIB_CFLAGS)

nodist_libsyncevo_dbus_la_SOURCES = \
	interfaces/syncevo-marshal.c

libsyncevo_dbus_la_SOURCES = \
	$(syncevo_dbus_headers) \
	syncevo-dbus-types.c \
	syncevo-server.c \
	syncevo-session.c

libsyncevo_dbus_la_CFLAGS = \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	$(DBUS_GLIB_CFLAGS)

libsyncevo_dbus_la_LIBADD = \
	$(DBUS_GLIB_LIBS)

syncevo_dbus_headers = \
	syncevo-dbus-types.h \
	syncevo-server.h \
	syncevo-session.h

libsyncevo_dbus_includedir = $(includedir)/syncevo-dbus
libsyncevo_dbus_include_HEADERS =	\
	$(syncevo_dbus_headers)
