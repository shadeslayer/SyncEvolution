# "test" only works when compiling as part of SyncEvolution
AM_CPPFLAGS = $(SYNCEVOLUTION_CFLAGS) -I$(top_srcdir)/test $(BACKEND_CPPFLAGS)

EXTRA_DIST = configure-sub.in WebDAVSourceRegister.cpp
CLEANFILES =
bin_SCRIPTS =

SYNCSOURCES = syncdav.la
MOSTLYCLEANFILES = $(SYNCSOURCES)
if ENABLE_MODULES
backend_LTLIBRARIES = $(SYNCSOURCES)
else
noinst_LTLIBRARIES = $(SYNCSOURCES)
endif

EXTRA_DIST += syncevo-webdav-lookup.sh
bin_SCRIPTS += syncevo-webdav-lookup
CLEANFILES += syncevo-webdav-lookup

syncevo-webdav-lookup: syncevo-webdav-lookup.sh
	rm -f $@
	ln -s $< $@

MAINTAINERCLEANFILES = Makefile.in

SYNCDAV_SOURCES = \
	CalDAVSource.h \
	CalDAVSource.cpp \
	CardDAVSource.h \
	CardDAVSource.cpp \
	WebDAVSource.h \
	WebDAVSource.cpp \
	NeonCXX.h \
	NeonCXX.cpp

syncdav_la_SOURCES = $(SYNCDAV_SOURCES)
syncdav_la_LIBADD = $(NEON_LIBS) $(SYNCEVOLUTION_LIBS)
syncdav_la_LDFLAGS = -module -avoid-version
syncdav_la_CXXFLAGS = $(NEON_CFLAGS)
