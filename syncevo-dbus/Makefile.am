lib_LTLIBRARIES = libsyncevo-dbus.la

noinst_PROGRAMS = \
	test-syncevo-dbus

test_syncevo_dbus_SOURCES = test.c
test_syncevo_dbus_LDADD = $(DBUS_GLIB_LIBS) libsyncevo-dbus.la
test_syncevo_dbus_CFLAGS = $(DBUS_GLIB_CFLAGS)

BUILT_SOURCES = \
	$(nodist_libsyncevo_dbus_la_SOURCES)

nodist_libsyncevo_dbus_la_SOURCES = \
	syncevo-marshal.c \
	syncevo-marshal.h \
	syncevo-bindings.h

libsyncevo_dbus_la_SOURCES = \
	$(syncevo_dbus_headers) \
	syncevo-dbus-types.c \
	syncevo-dbus.c

libsyncevo_dbus_la_CFLAGS = \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	$(DBUS_GLIB_CFLAGS)

libsyncevo_dbus_la_LIBADD = \
	$(DBUS_GLIB_LIBS)

syncevo_dbus_headers = \
	syncevo-dbus-types.h \
	syncevo-dbus.h

libsyncevo_dbus_includedir = $(includedir)/syncevo-dbus
libsyncevo_dbus_include_HEADERS =	\
	$(syncevo_dbus_headers)

CLEANFILES = $(BUILT_SOURCES)

%-bindings.h: stamp-%-bindings.h
	@true
stamp-%-bindings.h: ../interfaces/%.xml
	$(DBUS_BINDING_TOOL) --mode=glib-client --prefix=syncevo $< > xgen-$(@F) \
	&& (cmp -s xgen-$(@F) $(@F:stamp-%=%) || cp xgen-$(@F) $(@F:stamp-%=%)) \
	&& rm -f xgen-$(@F) \
	&& echo timestamp > $(@F)

syncevo-marshal.h: ../interfaces/syncevo-marshal.list $(GLIB_GENMARSHAL)
	$(GLIB_GENMARSHAL) $< --header --prefix=syncevo_marshal > $@
syncevo-marshal.c: ../interfaces/syncevo-marshal.list syncevo-marshal.h $(GLIB_GENMARSHAL)
	echo "#include \"syncevo-marshal.h\"" > $@ \
	&& $(GLIB_GENMARSHAL) --prefix=syncevo_marshal $(srcdir)/../interfaces/syncevo-marshal.list --body >> $@
