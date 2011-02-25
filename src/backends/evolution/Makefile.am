AM_CPPFLAGS = $(SYNCEVOLUTION_CFLAGS) -I$(top_srcdir)/test $(BACKEND_CPPFLAGS)

EXTRA_DIST = configure-sub.in

SYNCSOURCES = syncecal.la syncebook.la
MOSTLYCLEANFILES = $(SYNCSOURCES)
if ENABLE_MODULES
backend_LTLIBRARIES = $(SYNCSOURCES)
else
noinst_LTLIBRARIES = $(SYNCSOURCES)
endif

MAINTAINERCLEANFILES = Makefile.in

SYNCECAL_SOURCES = \
	EvolutionSyncSource.h \
	EvolutionSyncSource.cpp \
        EvolutionCalendarSource.h \
        EvolutionMemoSource.h \
	EvolutionCalendarSource.cpp \
	EvolutionMemoSource.cpp

if ENABLE_ECAL
SYNCECAL_SOURCES += \
	e-cal-check-timezones.c \
	e-cal-check-timezones.h
endif

SYNCEBOOK_SOURCES = \
	EvolutionSyncSource.h \
	EvolutionSyncSource.cpp \
        EvolutionContactSource.h \
	EvolutionContactSource.cpp

syncecal_la_SOURCES = $(SYNCECAL_SOURCES)
syncecal_la_LIBADD = $(ECAL_LIBS) $(SYNCEVOLUTION_LIBS)
# _GNU_SOURCE and -ldl for libical.c + dlsym():
syncecal_la_CPPFLAGS = -D_GNU_SOURCE \
	-De_cal_check_timezones=syncevolution_check_timezones \
	-De_cal_tzlookup_ecal=syncevolution_tzlookup_ecal \
	-De_cal_tzlookup_icomp=syncevolution_tzlookup_icomp \
	-De_cal_match_tzid=syncevolution_match_tzid \
	$(AM_CPPFLAGS)
syncecal_la_LDFLAGS = -module -avoid-version -ldl
syncecal_la_CXXFLAGS = $(SYNCEVOLUTION_CXXFLAGS)

syncebook_la_SOURCES = $(SYNCEBOOK_SOURCES)
syncebook_la_LIBADD = $(EBOOK_LIBS) $(SYNCEVOLUTION_LIBS)
syncebook_la_LDFLAGS = -module -avoid-version 
syncebook_la_CXXFLAGS = $(SYNCEVOLUTION_CXXFLAGS)
