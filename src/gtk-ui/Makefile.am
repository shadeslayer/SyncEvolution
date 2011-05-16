applicationsdir = $(datadir)/applications
applications_in_files = sync.desktop.in sync-gtk.desktop.in
applications_generated = $(applications_in_files:.desktop.in=.desktop)
applications_DATA = $(GUI_DESKTOP_FILES)
@INTLTOOL_DESKTOP_RULE@

# When installing both the plain GTK and the Moblin-themed version,
# the Moblin version uses the normal "Sync - Up to date" name/comment
# and the GTK version uses "Sync (GTK)" as name with the same
# comment. This is a somewhat arbitrary choice, with the rationale
# being that a Moblin user is less likely to care about the
# distinction while a GTK user might understand what "(GTK)" means.
sync-moblin.desktop: sync.desktop
	cp $< $@

gladedir = $(datadir)/syncevolution/
glade_DATA = \
	ui.xml

icondir = $(datadir)/icons/hicolor/48x48/apps
dist_icon_DATA = sync.png

themercdir = $(datadir)/syncevolution/
themerc_DATA = \
	sync-generic.png \
	sync-spinner.gif \
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
	mux-frame.c mux-frame.h \
	sync-config-widget.c sync-config-widget.h \
	gtkinfobar.c gtkinfobar.h

sync_ui_LDADD = $(GUI_LIBS) $(DBUS_GLIB_LIBS) $(top_builddir)/src/dbus/glib/libsyncevo-dbus.la
sync_ui_CFLAGS = $(GUI_CFLAGS) \
	$(DBUS_GLIB_CFLAGS) \
	-DGLADEDIR=\""$(gladedir)"\" \
	-DTHEMEDIR=\""$(themercdir)"\" \
	-DLIBEXECDIR=\"@libexecdir@\" \
	-DSYNCEVOLUTION_LOCALEDIR=\"${SYNCEVOLUTION_LOCALEDIR}\"
sync_ui_CPPFLAGS = -I$(top_builddir) -I$(top_srcdir) -I$(top_builddir)/src/dbus/glib -I$(top_srcdir)/src/dbus/glib $(SYNTHESIS_CFLAGS) 

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

CLEANFILES = $(BUILT_SOURCES) \
	ui.xml \
	sync-moblin.desktop \
        $(applications_generated)
