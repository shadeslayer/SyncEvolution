plugindir = $(GNOMEBLUETOOTH_DIR)/plugins/

INCLUDES =  $(GNOMEBLUETOOTH_CFLAGS) \
	-DLOCALEDIR=\"$(SYNCEVOLUTION_LOCALEDIR)\"	

if ENABLE_GNOME_BLUETOOTH_PANEL
plugin_LTLIBRARIES = libgbtsyncevolution.la
libgbtsyncevolution_la_SOURCES = syncevolution.c
libgbtsyncevolution_la_LDFLAGS = -module -avoid-version
endif
