# Some of the core header files still have a dependency on Evolution,
# which is why its flags have to be listed. They are empty if Evolution
# access is disabled.
AM_CPPFLAGS = -I$(srcdir)/../../core @EPACKAGE_CFLAGS@ @EBOOK_CFLAGS@ @ECAL_CFLAGS@ @FILE_CFLAGS@ @FUNAMBOL_CFLAGS@

# Applies to sources in SyncEvolution repository, but not
# the Funambol C++ client library. Used to add -Wall -Werror
# only when compiling that source, but not for the client
# library.
SYNCEVOLUTION_CXXFLAGS = @SYNCEVOLUTION_CXXFLAGS@

EXTRA_DIST = configure-sub.in

SYNCSOURCES = syncfile.la
MOSTLYCLEANFILES = $(SYNCSOURCES)
if ENABLE_MODULES
pkglib_LTLIBRARIES = $(SYNCSOURCES)
else
noinst_LTLIBRARIES = $(SYNCSOURCES)
endif

MAINTAINERCLEANFILES = Makefile.in

SYNCFILE_SOURCES = \
	FileSyncSource.h \
	FileSyncSource.cpp

syncfile_la_SOURCES = $(SYNCFILE_SOURCES)
syncfile_la_LIBADD = @FILE_LIBS@
syncfile_la_LDFLAGS = -module -rpath '$(pkglibdir)'
syncfile_la_CXXFLAGS = $(SYNCEVOLUTION_CXXFLAGS)

# If you need special test cases for your sync source, then
# install them here. Here's how the sqlite backend does that:
#
#../../testcases/sqlite_vcard21.vcf: $(FUNAMBOL_SUBDIR)/test/test/testcases/vcard21.vcf
#	mkdir -p ${@D}
#	perl -e '$$_ = join("", <>); s/^(ADR|TEL|EMAIL|PHOTO).*?(?=^\S)//msg; s/;X-EVOLUTION-UI-SLOT=\d+//g; print;' $< >$@
#all: ../../testcases/sqlite_vcard21.vcf
