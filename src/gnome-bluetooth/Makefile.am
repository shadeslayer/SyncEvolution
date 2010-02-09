plugindir = $(GNOMEBLUETOOTH_DIR)/plugins/

INCLUDES =  $(GNOMEBLUETOOTH_CFLAGS) \
	-DLOCALEDIR=\"$(SYNCEVOLUTION_LOCALEDIR)\"	

plugin_LTLIBRARIES = libgbtsyncevolution.la
libgbtsyncevolution_la_SOURCES = syncevolution.c
libgbtsyncevolution_la_LDFLAGS = -module -avoid-version

