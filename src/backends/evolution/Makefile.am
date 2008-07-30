AM_CPPFLAGS = -I$(srcdir)/../../core @EPACKAGE_CFLAGS@ @EBOOK_CFLAGS@ @ECAL_CFLAGS@ @GLIB_CFLAGS@ @FUNAMBOL_CFLAGS@

# applies to sources in SyncEvolution repository, but not
# the Funambol C++ client library
SYNCEVOLUTION_CXXFLAGS = @SYNCEVOLUTION_CXXFLAGS@

EXTRA_DIST = configure-sub.in

SYNCSOURCES = syncecal.la syncebook.la
MOSTLYCLEANFILES = $(SYNCSOURCES)
if ENABLE_MODULES
pkglib_LTLIBRARIES = $(SYNCSOURCES)
else
noinst_LTLIBRARIES = $(SYNCSOURCES)
endif

MAINTAINERCLEANFILES = Makefile.in

SYNCECAL_SOURCES = \
        EvolutionCalendarSource.h \
        EvolutionMemoSource.h \
	EvolutionCalendarSource.cpp \
	EvolutionMemoSource.cpp

if ENABLE_ECAL
SYNCECAL_SOURCES += \
	libical/icalstrdup.c \
	libical/icalstrdup.h \
	e-cal-check-timezones.c \
	e-cal-check-timezones.h
endif

SYNCEBOOK_SOURCES = \
        EvolutionContactSource.h \
	EvolutionContactSource.cpp

syncecal_la_SOURCES = $(SYNCECAL_SOURCES)
syncecal_la_LIBADD = @ECAL_LIBS@ ../../core/libsyncevolution.la
# _GNU_SOURCE and -ldl for libical.c + dlsym():
syncecal_la_CPPFLAGS = -D_GNU_SOURCE \
	-De_cal_check_timezones=syncevolution_check_timezones \
	-De_cal_tzlookup_ecal=syncevolution_tzlookup_ecal \
	-De_cal_tzlookup_icomp=syncevolution_tzlookup_icomp \
	-De_cal_match_tzid=syncevolution_match_tzid \
	$(AM_CPPFLAGS)
syncecal_la_LDFLAGS = -module -rpath '$(pkglibdir)' -ldl
syncecal_la_CXXFLAGS = $(SYNCEVOLUTION_CXXFLAGS)

syncebook_la_SOURCES = $(SYNCEBOOK_SOURCES)
syncebook_la_LIBADD = @EBOOK_LIBS@ ../../core/libsyncevolution.la
syncebook_la_LDFLAGS = -module -rpath '$(pkglibdir)'
syncebook_la_CXXFLAGS = $(SYNCEVOLUTION_CXXFLAGS)
