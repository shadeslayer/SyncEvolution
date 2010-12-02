AM_CPPFLAGS = $(SYNCEVOLUTION_CFLAGS) -I$(top_srcdir)/test $(BACKEND_CPPFLAGS)

EXTRA_DIST = configure-sub.in

SYNCSOURCES = syncsqlite.la
MOSTLYCLEANFILES = $(SYNCSOURCES)
if ENABLE_MODULES
backend_LTLIBRARIES = $(SYNCSOURCES)
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
syncsqlite_la_LIBADD = $(SQLITE_LIBS) $(SYNCEVOLUTION_LIBS)
syncsqlite_la_LDFLAGS = -module -avoid-version
syncsqlite_la_CXXFLAGS = $(SYNCEVOLUTION_CXXFLAGS)

# SQLiteContactSource does not support all fields from Funambol vCard 2.1
# test cases: filter them out before testing
#../../testcases/sqlite_vcard21.vcf: $(FUNAMBOL_SUBDIR)/test/test/testcases/vcard21.vcf
#	mkdir -p ${@D}
#	perl -e '$$_ = join("", <>); s/^(ADR|TEL|EMAIL|PHOTO).*?(?=^\S)//msg; s/;X-EVOLUTION-UI-SLOT=\d+//g; print;' $< >$@
# all: ../../testcases/sqlite_vcard21.vcf
