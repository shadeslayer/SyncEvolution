AM_CPPFLAGS = $(SYNCEVOLUTION_CFLAGS) -I$(top_srcdir)/test $(BACKEND_CPPFLAGS)

EXTRA_DIST = configure-sub.in

SYNCSOURCES = syncmaemocal.la
MOSTLYCLEANFILES = $(SYNCSOURCES)
if ENABLE_MODULES
backend_LTLIBRARIES = $(SYNCSOURCES)
else
noinst_LTLIBRARIES = $(SYNCSOURCES)
endif

MAINTAINERCLEANFILES = Makefile.in

SYNCMAEMOCAL_SOURCES = \
	MaemoCalendarSource.h \
	MaemoCalendarSource.cpp

syncmaemocal_la_SOURCES = $(SYNCMAEMOCAL_SOURCES)
syncmaemocal_la_LIBADD = $(MCALB_LIBS) $(SYNCEVOLUTION_LIBS)
syncmaemocal_la_LDFLAGS = -module -avoid-version
syncmaemocal_la_CXXFLAGS = $(SYNCEVOLUTION_CXXFLAGS)
