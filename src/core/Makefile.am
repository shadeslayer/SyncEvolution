AM_CPPFLAGS = @BACKEND_CPPFLAGS@ @GLIB_CFLAGS@ -I$(top_srcdir)/test

# applies to sources in SyncEvolution repository, but not
# the Funambol C++ client library
SYNCEVOLUTION_CXXFLAGS = @SYNCEVOLUTION_CXXFLAGS@

SYNCEVOLUTION_LDADD = @SYNCEVOLUTION_LDADD@
MAINTAINERCLEANFILES = Makefile.in

if ENABLE_UNIT_TESTS
SYNCEVOLUTION_CXXFLAGS += $(CPPUNIT_CXXFLAGS)
SYNCEVOLUTION_LDADD += $(CPPUNIT_LDFLAGS)
endif

if ENABLE_MODULES
pkglib_LTLIBRARIES = libsyncevolution.la
else
noinst_LTLIBRARIES = libsyncevolution.la
endif

CORE_SOURCES  = \
	ConfigTree.h \
	ConfigNode.h \
	HashConfigNode.h \
	VolatileConfigNode.h \
	VolatileConfigTree.h \
        EvolutionSmartPtr.h \
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
	CurlTransportAgent.h \
	CurlTransportAgent.cpp \
	\
	SoupTransportAgent.h \
	SoupTransportAgent.cpp \
	\
	SyncEvolutionUtil.cpp \
	SyncEvolutionUtil.h \
        \
	SyncEvolutionCmdline.cpp \
        SyncEvolutionCmdline.h \
	\
        EvolutionSyncSource.h \
        EvolutionSyncSource.cpp \
	\
	SynthesisDBPlugin.cpp \
	\
	SyncEvolutionXML.c \
        EvolutionSyncClient.h \
	EvolutionSyncClient.cpp \
	\
	SyncEvolutionConfig.h \
	SyncEvolutionConfig.cpp \
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


libsyncevolution_la_SOURCES = $(CORE_SOURCES)
libsyncevolution_la_LIBADD = @EPACKAGE_LIBS@ @GLIB_LIBS@ $(TRANSPORT_LIBS) @LIBS@ $(SYNTHESIS_LIBS) $(SYNCEVOLUTION_LDADD)
libsyncevolution_la_CXXFLAGS = $(TRANSPORT_CFLAGS) $(SYNCEVOLUTION_CXXFLAGS) $(SYNTHESIS_CFLAGS)
libsyncevolution_la_CPPFLAGS = $(AM_CPPFLAGS) -DTEMPLATE_DIR=\""$(sysconfdir)/default/syncevolution"\" -DLIBDIR=\""$(libdir)"\"

SyncEvolutionXML.c: $(srcdir)/../syncclient_sample_config.xml
	echo "const char *SyncEvolutionXML =" > $@
	sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/\(.*\)/"\1\\n"/' $< >>$@
	echo ";" >>$@

# include boost in distribution
#dist-hook:
#	cp -r $(srcdir)/boost $(distdir)
#	find $(distdir) -name .libs -o -name "*~" -o -name ".*" -o -name "*.o" -o -name "*.lo" -o -name CVS -o -name autom4te.cache | xargs rm -rf
