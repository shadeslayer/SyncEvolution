AM_CPPFLAGS = -I$(srcdir)/../../ -I$(top_srcdir)/test $(BACKEND_CPPFLAGS)

EXTRA_DIST = configure-sub.in

SYNCSOURCES = syncdav.la
MOSTLYCLEANFILES = $(SYNCSOURCES)
if ENABLE_MODULES
backend_LTLIBRARIES = $(SYNCSOURCES)
else
noinst_LTLIBRARIES = $(SYNCSOURCES)
endif

MAINTAINERCLEANFILES = Makefile.in

SYNCDAV_SOURCES = \
	CalDAVSource.h \
	CalDAVSource.cpp \
	WebDAVSource.h \
	WebDAVSource.cpp \
	NeonCXX.h \
	NeonCXX.cpp

syncdav_la_SOURCES = $(SYNCDAV_SOURCES)
syncdav_la_LIBADD = $(NEON_LIBS) ../../syncevo/libsyncevolution.la
syncdav_la_LDFLAGS = -module -avoid-version
syncdav_la_CXXFLAGS = $(SYNCEVOLUTION_CXXFLAGS) $(NEON_CFLAGS)
