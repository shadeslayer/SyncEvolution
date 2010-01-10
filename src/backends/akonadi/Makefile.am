AM_CPPFLAGS = -I$(srcdir)/../../ -I$(top_srcdir)/test $(BACKEND_CPPFLAGS)

EXTRA_DIST = configure-sub.in

SYNCSOURCES = syncakonadi.la
MOSTLYCLEANFILES = $(SYNCSOURCES)
if ENABLE_MODULES
backend_LTLIBRARIES = $(SYNCSOURCES)
else
noinst_LTLIBRARIES = $(SYNCSOURCES)
endif

MAINTAINERCLEANFILES = Makefile.in

syncakonadi_la_SOURCES = \
	akonadisyncsource.h \
	akonadisyncsource.cpp
syncakonadi_la_LIBADD = $(KDEPIM_LIBS) ../../syncevo/libsyncevolution.la
syncakonadi_la_LDFLAGS = -module -avoid-version
syncakonadi_la_CXXFLAGS = $(SYNCEVOLUTION_CXXFLAGS)
