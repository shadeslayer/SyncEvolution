AM_CPPFLAGS = @BACKEND_CPPFLAGS@ @GLIB_CFLAGS@ -I$(top_srcdir)/test -I$(top_srcdir)/src -DSYNCEVO_BACKEND=\"$(backendsearchdir)\"

# applies to sources in SyncEvolution repository, but not
# the Funambol C++ client library
SYNCEVOLUTION_CXXFLAGS = @SYNCEVOLUTION_CXXFLAGS@

SYNCEVOLUTION_LDADD = @SYNCEVOLUTION_LDADD@
MAINTAINERCLEANFILES = Makefile.in

if ENABLE_UNIT_TESTS
SYNCEVOLUTION_CXXFLAGS += $(CPPUNIT_CXXFLAGS)
SYNCEVOLUTION_LDADD += $(CPPUNIT_LDFLAGS)
endif

lib_LTLIBRARIES = libsyncevolution.la

SYNCEVOLUTION_SOURCES  = \
	ConfigTree.h \
	ConfigNode.h \
	HashConfigNode.h \
	VolatileConfigNode.h \
	VolatileConfigTree.h \
        SmartPtr.h \
	eds_abi_wrapper.h \
	eds_abi_wrapper.cpp \
	\
	SyncML.h \
	SyncML.cpp \
	\
	SynthesisEngine.h \
	SynthesisEngine.cpp \
	\
	Logging.h \
	Logging.cpp \
	LogStdout.h \
	LogStdout.cpp \
	LogRedirect.h \
	LogRedirect.cpp \
	\
	TransportAgent.h \
	TransportAgent.cpp \
	CurlTransportAgent.h \
	CurlTransportAgent.cpp \
	\
	SoupTransportAgent.h \
	SoupTransportAgent.cpp \
	\
	util.cpp \
	util.h \
        \
	Cmdline.cpp \
        Cmdline.h \
	\
        SyncSource.h \
        SyncSource.cpp \
	\
	SynthesisDBPlugin.cpp \
	\
	SyncEvolutionXML.c \
        SyncContext.h \
	SyncContext.cpp \
	\
	SyncConfig.h \
	SyncConfig.cpp \
	\
	DevNullConfigNode.h \
	MultiplexConfigNode.h \
	MultiplexConfigNode.cpp \
	\
	FilterConfigNode.h \
	FilterConfigNode.cpp \
	\
	SafeConfigNode.h \
	SafeConfigNode.cpp \
	\
	PrefixConfigNode.h \
	PrefixConfigNode.cpp \
	\
	FileConfigNode.h \
	FileConfigNode.cpp \
	\
	FileConfigTree.h \
	FileConfigTree.cpp \
	\
	TrackingSyncSource.h \
	TrackingSyncSource.cpp

libsyncevolution_includedir= $(includedir)/syncevo
libsyncevolution_include_HEADERS = \
	declarations.h \
	TrackingSyncSource.h  \
								  FilterConfigNode.h \
								  PrefixConfigNode.h \
								  SafeConfigNode.h \
								  SyncConfig.h \
								  SyncSource.h \
								  util.h \
								  SyncContext.h \
								  SynthesisEngine.h \
								  Logging.h \
								  SyncML.h \
								  eds_abi_wrapper.h \
								  SmartPtr.h \
								  ConfigNode.h 
if ENABLE_OBEX 
SYNCEVOLUTION_SOURCES += ObexTransportAgent.h ObexTransportAgent.cpp
SYNCEVOLUTION_LDADD += $(LIBOPENOBEX_LIBS)
SYNCEVOLUTION_CXXFLAGS += $(LIBOPENOBEX_CFLAGS)
if ENABLE_BLUETOOTH
SYNCEVOLUTION_LDADD += $(BLUEZ_LIBS)
SYNCEVOLUTION_CXXFLAGS += $(BLUEZ_CFLAGS)
endif
endif

pkgconfigdir=$(libdir)/pkgconfig
pkgconfig_DATA=syncevolution.pc
DISTCLEANFILES =syncevolution.pc
EXTRA_DIST =syncevolution.pc.in

libsyncevolution_la_SOURCES = $(SYNCEVOLUTION_SOURCES)
libsyncevolution_la_LIBADD = @EPACKAGE_LIBS@ @GLIB_LIBS@ $(SYNTHESIS_LIBS) $(TRANSPORT_LIBS) @LIBS@ $(SYNCEVOLUTION_LDADD)
libsyncevolution_la_CXXFLAGS = $(TRANSPORT_CFLAGS) $(SYNCEVOLUTION_CXXFLAGS) $(SYNTHESIS_CFLAGS)
libsyncevolution_la_CPPFLAGS = $(AM_CPPFLAGS) -DTEMPLATE_DIR=\""$(datadir)/syncevolution/templates"\" -DLIBDIR=\""$(libdir)"\"
libsyncevolution_la_DEPENDENCIES = $(SYNTHESIS_DEP)

if ENABLE_MODULES
libsyncevolution_la_LDFLAGS =
else
libsyncevolution_la_LDFLAGS = -static
endif

SyncEvolutionXML.c: $(srcdir)/../syncclient_sample_config.xml
	echo "const char *SyncEvolutionXML =" > $@
	sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/\(.*\)/"\1\\n"/' $< >>$@
	echo ";" >>$@

# include boost in distribution
#dist-hook:
#	cp -r $(srcdir)/boost $(distdir)
#	find $(distdir) -name .libs -o -name "*~" -o -name ".*" -o -name "*.o" -o -name "*.lo" -o -name CVS -o -name autom4te.cache | xargs rm -rf

# make sure that the installed development files are usable
installcheck-local: $(srcdir)/installcheck-local.sh
	env PKG_CONFIG_PATH=$(DESTDIR)/$(pkgconfigdir):$$PKG_CONFIG_PATH $< "$(DESTDIR)/$(libsyncevolution_includedir)" "$(DESTDIR)/$(includedir)" "$(DESTDIR)/$(libdir)"
EXTRA_DIST += installcheck-local.sh
