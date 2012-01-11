EXTRA_DIST = configure-sub.in

if ENABLE_QT_DBUS

include $(top_srcdir)/m4-repo/autotroll.mk

libsyncevolution_qt_dbus_includedir = $(includedir)/syncevolution-qt-dbus

lib_LTLIBRARIES = libsyncevolution-qt-dbus.la

libsyncevolution_qt_dbus_headers = dbustypes.h

nodist_libsyncevolution_qt_dbus_include_HEADERS = \
  $(libsyncevolution_qt_dbus_headers) \
  $(built_headers) \
  $(NULL)

dist_libsyncevolution_qt_dbus_la_SOURCES = \
        $(libsyncevolution_qt_dbus_headers) \
	dbustypes.cpp

nodist_libsyncevolution_qt_dbus_la_SOURCES = \
	$(BUILT_SOURCES)

AM_CPPFLAGS = $(SYNCEVOLUTION_CFLAGS) -I$(top_srcdir)/test $(BACKEND_CPPFLAGS)

built_headers = \
  syncevo-server-full.h \
  syncevo-session-full.h \
  syncevo-connection-full.h \
  $(NULL)

BUILT_SOURCES = \
  syncevo-server-full.cpp \
  syncevo-session-full.cpp \
  syncevo-connection-full.cpp \
  syncevo-server-full.moc.cpp \
  syncevo-session-full.moc.cpp \
  syncevo-connection-full.moc.cpp \
  $(built_headers) \
  $(NULL)

MOSTLYCLEANFILES = $(lib_LTLIBRARIES)

MAINTAINERCLEANFILES = Makefile.in

CLEANFILES = $(BUILT_SOURCES) \
	stamp-server \
	stamp-session \
	stamp-connection

DISTCLEANFILES += syncevolution-qt-dbus.pc
EXTRA_DIST += syncevolution-qt-dbus.pc.in
pkgconfig_DATA = syncevolution-qt-dbus.pc
pkgconfigdir = $(libdir)/pkgconfig

libsyncevolution_qt_dbus_la_LIBADD = $(QT_DBUS_LIBS) $(QT_LIBS)
libsyncevolution_qt_dbus_la_CPPFLAGS = $(AM_CPPFLAGS) $(QT_CPPFLAGS)
# Allow Qt to set some compile flags, but not the ones normally set via configure.
# In particular -W is not compatible with the SyncEvolution header files (we have
# unused parameters in some inline functions).
libsyncevolution_qt_dbus_la_CXXFLAGS = $(SYNCEVOLUTION_CXXFLAGS) $(filter-out -O2 -g -W -Wall, $(QT_CXXFLAGS))

syncevo-server-full.cpp syncevo-server-full.h: stamp-server
syncevo-session-full.cpp syncevo-session-full.h: stamp-session
syncevo-connection-full.cpp syncevo-connection-full.h: stamp-connection

# work around #ifndef SYNCEVO-SERVER-FULL_H_1305547804 bug
stamp-%: $(srcdir)/../interfaces/syncevo-%-full.xml
	qdbusxml2cpp -p syncevo-$*-full -i dbustypes.h $<
	perl -pi -e 's/SYNCEVO-(\w*)-FULL_H/SYNCEVO_$$1_FULL_H/' syncevo-$*-full.*
	touch $@

endif # ENABLE_QT_DBUS
