AM_CPPFLAGS = $(SYNCEVOLUTION_CFLAGS) -I$(top_srcdir)/test $(BACKEND_CPPFLAGS)

EXTRA_DIST = configure-sub.in

SYNCSOURCES = syncfile.la
MOSTLYCLEANFILES = $(SYNCSOURCES)
if ENABLE_MODULES
backend_LTLIBRARIES = $(SYNCSOURCES)
else
noinst_LTLIBRARIES = $(SYNCSOURCES)
endif

MAINTAINERCLEANFILES = Makefile.in

SYNCFILE_SOURCES = \
	FileSyncSource.h \
	FileSyncSource.cpp

syncfile_la_SOURCES = $(SYNCFILE_SOURCES)
syncfile_la_LIBADD = $(FILE_LIBS) $(SYNCEVOLUTION_LIBS)
syncfile_la_LDFLAGS = -module -avoid-version
syncfile_la_CXXFLAGS = $(SYNCEVOLUTION_CXXFLAGS)
# If you need special test cases for your sync source, then
# install them here. Here's how the sqlite backend does that:
#
#../../testcases/sqlite_vcard21.vcf: $(FUNAMBOL_SUBDIR)/test/test/testcases/vcard21.vcf
#	mkdir -p ${@D}
#	perl -e '$$_ = join("", <>); s/^(ADR|TEL|EMAIL|PHOTO).*?(?=^\S)//msg; s/;X-EVOLUTION-UI-SLOT=\d+//g; print;' $< >$@
#all: ../../testcases/sqlite_vcard21.vcf
