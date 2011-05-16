lib_LTLIBRARIES = libsyncevo-dbus.la

if COND_GUI
AM_CPPFLAGS = -I$(top_srcdir) $(SYNTHESIS_CFLAGS)

DISTCLEANFILES = syncevo-dbus.pc
EXTRA_DIST = syncevo-dbus.pc.in syncevo-marshal.list
pkgconfig_DATA = syncevo-dbus.pc
pkgconfigdir = $(libdir)/pkgconfig

noinst_PROGRAMS = test-syncevo-dbus

test_syncevo_dbus_SOURCES = test.c
test_syncevo_dbus_LDADD = $(DBUS_GLIB_LIBS) libsyncevo-dbus.la
test_syncevo_dbus_CFLAGS = $(DBUS_GLIB_CFLAGS)

nodist_libsyncevo_dbus_la_SOURCES = \
	syncevo-marshal.c

BUILT_SOURCES =  syncevo-marshal.c syncevo-marshal.h \
        syncevo-server-bindings.h syncevo-connection-bindings.h syncevo-session-bindings.h \
        syncevo-server-glue.h syncevo-connection-glue.h syncevo-session-glue.h \
        syncevo-server.xml syncevo-connection.xml syncevo-session.xml

CLEANFILES = $(BUILT_SOURCES) stamp-syncevo-dbus-glue.h

# D-Bus binding tool gets confused by doc comments, strip them first
%.xml: $(srcdir)/../interfaces/spec-strip-docs.xsl $(srcdir)/../interfaces/%-full.xml
	$(XSLT) -o $@ $+

%-bindings.h: stamp-%-bindings.h
	@true
stamp-%-bindings.h: %.xml
	$(DBUS_BINDING_TOOL) --mode=glib-client --prefix=syncevo $< > xgen-$(@F) \
	&& (cmp -s xgen-$(@F) $(@F:stamp-%=%) || cp xgen-$(@F) $(@F:stamp-%=%)) \
	&& rm -f xgen-$(@F) \
	&& echo timestamp > $(@F)

syncevo-marshal.h: $(srcdir)/syncevo-marshal.list $(GLIB_GENMARSHAL)
	$(GLIB_GENMARSHAL) $< --header --prefix=syncevo_marshal > $@
syncevo-marshal.c: $(srcdir)/syncevo-marshal.list syncevo-marshal.h $(GLIB_GENMARSHAL)
	echo "#include \"syncevo-marshal.h\"" > $@ \
	&& $(GLIB_GENMARSHAL) --prefix=syncevo_marshal $(srcdir)/syncevo-marshal.list --body >> $@

%-glue.h: stamp-%-glue.h
	@true
stamp-%-glue.h: %.xml
	$(DBUS_BINDING_TOOL) --prefix=syncevo --mode=glib-server $< > xgen-$(@F) \
	&& (cmp -s xgen-$(@F) $(@F:stamp-%=%) || cp xgen-$(@F) $(@F:stamp-%=%)) \
	&& rm -f xgen-$(@F) \
	&& echo timestamp > $(@F)

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
endif
