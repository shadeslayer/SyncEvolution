include $(top_srcdir)/m4-repo/autotroll.mk

# "test" only works when compiling as part of SyncEvolution
AM_CPPFLAGS = $(SYNCEVOLUTION_CFLAGS) -I$(srcdir)/../../../test $(BACKEND_CPPFLAGS)
EXTRA_DIST = configure-sub.in
MAINTAINERCLEANFILES = Makefile.in

# directories required by Buteo for plugins and config files
syncdir = $(libdir)/sync
profilesdir = $(sysconfdir)/sync

if ENABLE_BUTEO
nobase_dist_profiles_DATA = \
    profiles/sync/google-calendar.xml \
    profiles/service/google-calendar.xml \
    profiles/sync/yahoo.xml \
    profiles/service/yahoo.xml \
    profiles/client/syncevo-buteo.xml
sync_LTLIBRARIES = libsyncevo-buteo-client.la

BUILT_SOURCES = ButeoBridge.moc.cpp

libsyncevo_buteo_client_la_SOURCES = \
	ButeoBridge.h \
	ButeoBridge.cpp
nodist_libsyncevo_buteo_client_la_SOURCES = \
	$(BUILT_SOURCES)

libsyncevo_buteo_client_la_LIBADD = $(BUTEO_LIBS) $(QT_LIBS) $(SYNCEVOLUTION_LIBS)
libsyncevo_buteo_client_la_LDFLAGS = -module -avoid-version $(QT_LDFLAGS)
libsyncevo_buteo_client_la_CPPFLAGS = $(AM_CPPFLAGS) $(QT_CPPFLAGS)

# Allow Qt to set some compile flags, but not the ones normally set via configure.
# In particular -W is not compatible with the SyncEvolution header files (we have
# unused parameters in some inline functions).
libsyncevo_buteo_client_la_CXXFLAGS = $(SYNCEVOLUTION_CXXFLAGS) $(BUTEO_CFLAGS) $(filter-out -O2 -g -W -Wall, $(QT_CXXFLAGS))
endif
