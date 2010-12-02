include $(top_srcdir)/m4-repo/autotroll.mk

AM_CPPFLAGS = $(SYNCEVOLUTION_CFLAGS) -I$(top_srcdir)/test $(BACKEND_CPPFLAGS)

EXTRA_DIST = configure-sub.in

SYNCSOURCES = syncqtcontacts.la
MOSTLYCLEANFILES = $(SYNCSOURCES)
if ENABLE_MODULES
backend_LTLIBRARIES = $(SYNCSOURCES)
else
noinst_LTLIBRARIES = $(SYNCSOURCES)
endif

MAINTAINERCLEANFILES = Makefile.in

syncqtcontacts_la_SOURCES = \
	QtContactsSource.h \
	QtContactsSource.cpp

syncqtcontacts_la_LIBADD = $(QT_CONTACTS_LIBS) $(QT_LIBS) $(SYNCEVOLUTION_LIBS)
syncqtcontacts_la_LDFLAGS = -module -avoid-version $(QT_LDFLAGS)
syncqtcontacts_la_CPPFLAGS = $(AM_CPPFLAGS) $(QT_CPPFLAGS)
# Allow Qt to set some compile flags, but not the ones normally set via configure.
# In particular -W is not compatible with the SyncEvolution header files (we have
# unused parameters in some inline functions).
syncqtcontacts_la_CXXFLAGS = $(SYNCEVOLUTION_CXXFLAGS) $(filter-out -O2 -g -W -Wall, $(QT_CXXFLAGS))
