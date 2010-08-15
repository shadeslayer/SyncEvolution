# include_HEADERS = gdbus.h

if ENABLE_MODULES
pkglib_LTLIBRARIES = libgdbus.la
else
noinst_LTLIBRARIES = libgdbus.la
endif

libgdbus_la_SOURCES = debug.h debug.c mainloop.c object.c watch.c \
    gdbus.h gdbus-cxx-bridge.h gdbus-cxx.h
libgdbus_la_LDFLAGS = -version-info 0:0:0 -export-symbols-regex b_dbus_.*

libgdbus_la_LIBADD = @GLIB_LIBS@ @DBUS_LIBS@

AM_CFLAGS = @GLIB_CFLAGS@ @DBUS_CFLAGS@

MAINTAINERCLEANFILES = Makefile.in

# include_HEADERS = gdbus-cxx-bridge.h gdbus-cxx.h
noinst_PROGRAMS = example
example_SOURCES = test/example.cpp
example_CXXFLAGS = @GLIB_CFLAGS@ @DBUS_CFLAGS@
example_LDADD = libgdbus.la @GLIB_LIBS@ @DBUS_LIBS@
