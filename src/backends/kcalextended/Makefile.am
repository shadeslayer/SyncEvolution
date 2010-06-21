include $(top_srcdir)/m4-repo/autotroll.mk

AM_CPPFLAGS = -I$(srcdir)/../../ -I$(top_srcdir)/test $(BACKEND_CPPFLAGS)

EXTRA_DIST = configure-sub.in

SYNCSOURCES = synckcalextended.la
MOSTLYCLEANFILES = $(SYNCSOURCES)
if ENABLE_MODULES
backend_LTLIBRARIES = $(SYNCSOURCES)
else
noinst_LTLIBRARIES = $(SYNCSOURCES)
endif

MAINTAINERCLEANFILES = Makefile.in

synckcalextended_la_SOURCES = \
	KCalExtendedSource.h \
	KCalExtendedSource.cpp
synckcalextended_la_LIBADD = $(KCALEXTENDED_LIBS)
synckcalextended_la_LDFLAGS = -module -avoid-version
synckcalextended_la_CXXFLAGS = $(SYNCEVOLUTION_CXXFLAGS) $(KCALEXTENDED_CFLAGS)
