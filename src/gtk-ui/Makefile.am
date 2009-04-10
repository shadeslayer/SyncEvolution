
gladedir = $(datadir)/syncevolution/
glade_DATA = \
	ui.xml

themercdir = $(datadir)/syncevolution/
themerc_DATA = \
	sync-ui.rc


EXTRA_DIST = \
	$(glade_DATA)
	$(glade_DATA)

ui.xml: ui.glade
	$(GTK_BUILDER_CONV) $< $(@F)

bin_PROGRAMS = \
	sync-ui

sync_ui_SOURCES = sync-ui.c sync-ui-config.c sync-ui-config.h mux-bin.c mux-bin.h
sync_ui_LDADD = $(GUI_LIBS) $(DBUS_GLIB_LIBS) -L$(top_builddir)/syncevo-dbus -lsyncevo-dbus
sync_ui_CFLAGS = $(GUI_CFLAGS) $(DBUS_GLIB_CFLAGS) -DGLADEDIR=\""$(gladedir)"\" -DTHEMEDIR=\""$(themercdir)"\" 

BUILT_SOURCES = \
	syncevo-bindings.h

%-bindings.h: stamp-%-bindings.h
	@true
stamp-%-bindings.h: $(top_srcdir)/interfaces/%.xml
	$(DBUS_BINDING_TOOL) --mode=glib-client --prefix=syncevo $< > xgen-$(@F) \
	&& (cmp -s xgen-$(@F) $(@F:stamp-%=%) || cp xgen-$(@F) $(@F:stamp-%=%)) \
	&& rm -f xgen-$(@F) \
	&& echo timestamp > $(@F)

