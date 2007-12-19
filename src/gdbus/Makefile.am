
include_HEADERS = gdbus.h

lib_LTLIBRARIES = libgdbus.la

libgdbus_la_SOURCES = debug.h debug.c mainloop.c
libgdbus_la_LDFLAGS = -version-info 0:0:0 -export-symbols-regex g_dbus_.*

libgdbus_la_LIBADD = @GLIB_LIBS@ @DBUS_LIBS@

AM_CFLAGS = @GLIB_CFLAGS@ @DBUS_CFLAGS@

MAINTAINERCLEANFILES = Makefile.in
