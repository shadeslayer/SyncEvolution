applicationsdir = $(datadir)/applications
applications_in_files = sync.desktop.in
applications_generated = $(applications_in_files:.desktop.in=.desktop)
applications_DATA = $(GUI_DESKTOP_FILES)
@INTLTOOL_DESKTOP_RULE@

# would be nicer to have different descriptions and names...
sync-gtk.desktop sync-moblin.desktop: sync-%.desktop: sync.desktop
	sed -e 's/Exec=sync-ui/Exec=sync-ui-$*/' $< >$@

gladedir = $(datadir)/syncevolution/
glade_DATA = \
	ui.xml

themercdir = $(datadir)/syncevolution/
themerc_DATA = \
	close.png close_hover.png settings.png settings_hover.png \
	sync-generic.png \
	sync-ui.rc

desktopdir = $(datadir)/applications

EXTRA_DIST = \
	ui.glade \
	$(themerc_DATA) \
	$(applications_in_files) \
	$(noinst_DATA)

ui.xml: ui.glade
	$(GTK_BUILDER_CONV) $< $(@F)

# sync-ui: default GUI, could be plain GTK or Moblin UX
# sync-ui-gtk: GTK GUI
# sync-ui-moblin: Moblin UX
#
# The later two are built when --enable-gui=all was used.
EXTRA_PROGRAMS = \
	sync-ui \
	sync-ui-gtk \
	sync-ui-moblin
bin_PROGRAMS = $(GUI_PROGRAMS)

sync_ui_SOURCES = \
	main.c sync-ui.c sync-ui.h sync-ui-config.c sync-ui-config.h \
	mux-frame.c mux-frame.h mux-window.c mux-window.h mux-icon-button.c mux-icon-button.h
nodist_sync_ui_SOURCES = sync-ui-marshal.c sync-ui-marshal.h
sync_ui_LDADD = $(GUI_LIBS) $(DBUS_GLIB_LIBS) $(top_builddir)/src/dbus/libsyncevo-dbus.la
sync_ui_CFLAGS = $(GUI_CFLAGS) \
	$(DBUS_GLIB_CFLAGS) \
	-DGLADEDIR=\""$(gladedir)"\" \
	-DTHEMEDIR=\""$(themercdir)"\" \
	-DLIBEXECDIR=\"@libexecdir@\" \
	-DSYNCEVOLUTION_LOCALEDIR=\"${SYNCEVOLUTION_LOCALEDIR}\"
sync_ui_CPPFLAGS = -I$(top_builddir) -I$(top_srcdir) -I$(top_builddir)/src/dbus -I$(top_srcdir)/src/dbus $(SYNTHESIS_CFLAGS) 

sync_ui_gtk_SOURCES = $(sync_ui_SOURCES)
nodist_sync_ui_gtk_SOURCES = $(nodist_sync_ui_SOURCES)
sync_ui_gtk_LDADD = $(sync_ui_LDADD)
sync_ui_gtk_CFLAGS = $(sync_ui_CFLAGS)
sync_ui_gtk_CPPFLAGS = $(sync_ui_CPPFLAGS)

sync_ui_moblin_SOURCES = $(sync_ui_SOURCES)
nodist_sync_ui_moblin_SOURCES = $(nodist_sync_ui_SOURCES)
sync_ui_moblin_LDADD = $(sync_ui_LDADD)
sync_ui_moblin_CFLAGS = $(sync_ui_CFLAGS)
sync_ui_moblin_CPPFLAGS = $(sync_ui_CPPFLAGS) -DUSE_MOBLIN_UX

noinst_DATA = sync-ui-marshal.list
if COND_GUI
BUILT_SOURCES = \
	syncevo-bindings.h sync-ui-marshal.c sync-ui-marshal.h
endif
CLEANFILES = $(BUILT_SOURCES) \
	ui.xml \
        $(applications_generated)

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
