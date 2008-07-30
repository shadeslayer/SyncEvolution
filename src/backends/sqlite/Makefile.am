AM_CPPFLAGS = -I$(srcdir)/../../core @EPACKAGE_CFLAGS@ @EBOOK_CFLAGS@ @ECAL_CFLAGS@ @SQLITE_CFLAGS@ @FUNAMBOL_CFLAGS@

# applies to sources in SyncEvolution repository, but not
# the Funambol C++ client library
SYNCEVOLUTION_CXXFLAGS = @SYNCEVOLUTION_CXXFLAGS@

EXTRA_DIST = configure-sub.in

SYNCSOURCES = syncsqlite.la
MOSTLYCLEANFILES = $(SYNCSOURCES)
if ENABLE_MODULES
pkglib_LTLIBRARIES = $(SYNCSOURCES)
else
noinst_LTLIBRARIES = $(SYNCSOURCES)
endif

MAINTAINERCLEANFILES = Makefile.in

SYNCSQLITE_SOURCES = \
	SQLiteUtil.h \
	SQLiteUtil.cpp \
	SQLiteContactSource.h \
	SQLiteContactSource.cpp

syncsqlite_la_SOURCES = $(SYNCSQLITE_SOURCES)
syncsqlite_la_LIBADD = @SQLITE_LIBS@
syncsqlite_la_LDFLAGS = -module -rpath '$(pkglibdir)'
syncsqlite_la_CXXFLAGS = $(SYNCEVOLUTION_CXXFLAGS)
