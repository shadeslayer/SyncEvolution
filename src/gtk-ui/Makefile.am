applicationsdir = $(datadir)/applications
applications_in_files = sync.desktop.in
applications_DATA = $(applications_in_files:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@

gladedir = $(datadir)/syncevolution/
glade_DATA = \
	ui.xml

themercdir = $(datadir)/syncevolution/
themerc_DATA = \
	close.png close_hover.png settings.png settings_hover.png \
	sync-generic.png \
	sync-ui.rc

desktopdir = $(datadir)/applications
desktop_DATA = \
	sync.desktop

EXTRA_DIST = \
	ui.glade \
	$(themerc_DATA) \
	$(desktop_DATA) \
	$(noinst_DATA)

ui.xml: ui.glade
	$(GTK_BUILDER_CONV) $< $(@F)

bin_PROGRAMS = \
	sync-ui


sync_ui_SOURCES = \
	main.c sync-ui.c sync-ui.h sync-ui-config.c sync-ui-config.h \
	mux-frame.c mux-frame.h mux-window.c mux-window.h mux-icon-button.c mux-icon-button.h \
	sync-ui-marshal.c sync-ui-marshal.h
sync_ui_LDADD = $(GUI_LIBS) $(DBUS_GLIB_LIBS) -L$(top_builddir)/src/dbus -lsyncevo-dbus
sync_ui_CFLAGS = $(GUI_CFLAGS) $(DBUS_GLIB_CFLAGS) \
                 -DGLADEDIR=\""$(gladedir)"\" \
                 -DTHEMEDIR=\""$(themercdir)"\" \
                 -DSYNCEVOLUTION_LOCALEDIR=\"${SYNCEVOLUTION_LOCALEDIR}\"
sync_ui_CPPFLAGS = -I$(top_builddir) -I$(top_srcdir) -I$(top_builddir)/src/dbus -I$(top_srcdir)/src/dbus $(SYNTHESIS_CFLAGS) 

noinst_DATA = sync-ui-marshal.list
BUILT_SOURCES = \
	syncevo-bindings.h sync-ui-marshal.c sync-ui-marshal.h
CLEANFILES = $(BUILT_SOURCES) 

%-bindings.h: stamp-%-bindings.h
	@true
stamp-%-bindings.h: $(top_builddir)/src/dbus/interfaces/%.xml
	$(DBUS_BINDING_TOOL) --mode=glib-client --prefix=syncevo $< > xgen-$(@F) \
	&& (cmp -s xgen-$(@F) $(@F:stamp-%=%) || cp xgen-$(@F) $(@F:stamp-%=%)) \
	&& rm -f xgen-$(@F) \
	&& echo timestamp > $(@F)

sync-ui-marshal.h: $(srcdir)/sync-ui-marshal.list $(GLIB_GENMARSHAL)
	$(GLIB_GENMARSHAL) $< --header --prefix=sync_ui_marshal > $@
sync-ui-marshal.c: $(srcdir)/sync-ui-marshal.list sync-ui-marshal.h $(GLIB_GENMARSHAL)
	echo "#include \"sync-ui-marshal.h\"" > $@ \
	&& $(GLIB_GENMARSHAL) --prefix=sync_ui_marshal $(srcdir)/sync-ui-marshal.list --body >> $@
