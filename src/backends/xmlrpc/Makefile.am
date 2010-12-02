AM_CPPFLAGS = $(SYNCEVOLUTION_CFLAGS) -I$(top_srcdir)/test $(BACKEND_CPPFLAGS)

EXTRA_DIST = configure-sub.in

SYNCSOURCES = syncxmlrpc.la
MOSTLYCLEANFILES = $(SYNCSOURCES)
if ENABLE_MODULES
backend_LTLIBRARIES = $(SYNCSOURCES)
else
noinst_LTLIBRARIES = $(SYNCSOURCES)
endif

MAINTAINERCLEANFILES = Makefile.in

SYNCXMLRPC_SOURCES = \
	XMLRPCSyncSource.h \
	XMLRPCSyncSource.cpp

syncxmlrpc_la_SOURCES = $(SYNCXMLRPC_SOURCES)
syncxmlrpc_la_LIBADD = $(XMLRPC_LIBS) $(SYNCEVOLUTION_LIBS)
syncxmlrpc_la_LDFLAGS = -module -avoid-version
syncxmlrpc_la_CXXFLAGS = $(SYNCEVOLUTION_CXXFLAGS)
# If you need special test cases for your sync source, then
# install them here. Here's how the sqlite backend does that:
#
#../../testcases/sqlite_vcard21.vcf: $(FUNAMBOL_SUBDIR)/test/test/testcases/vcard21.vcf
#	mkdir -p ${@D}
#	perl -e '$$_ = join("", <>); s/^(ADR|TEL|EMAIL|PHOTO).*?(?=^\S)//msg; s/;X-EVOLUTION-UI-SLOT=\d+//g; print;' $< >$@
#all: ../../testcases/sqlite_vcard21.vcf
