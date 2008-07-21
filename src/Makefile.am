INCLUDES = @EPACKAGE_CFLAGS@ @EBOOK_CFLAGS@ @ECAL_CFLAGS@ @GLIB_CFLAGS@ @FUNAMBOL_CFLAGS@ @SQLITE_CFLAGS@ @ADDRESSBOOK_CFLAGS@

# applies to sources in SyncEvolution repository, but not
# the Funambol C++ client library
SYNCEVOLUTION_CXXFLAGS = @SYNCEVOLUTION_CXXFLAGS@

bin_PROGRAMS = syncevolution
bin_SCRIPTS = synccompare
SYNCSOURCES = syncecal.la syncebook.la syncsqlite.la syncaddressbook.la
MOSTLYCLEANFILES = $(SYNCSOURCES)
if ENABLE_MODULES
pkglib_LTLIBRARIES = $(SYNCSOURCES)
else
EXTRA_LTLIBRARIES = $(SYNCSOURCES)
endif
SYNCEVOLUTION_LDADD = @SYNCEVOLUTION_LDADD@
EXTRA_PROGRAMS = client-test
EXTRA_DIST = shlibs.local
check_PROGRAMS = @CPPUNIT_TESTS@
TESTS = @CPPUNIT_TESTS@

DISTCLEANFILES = synccompare
MAINTAINERCLEANFILES = Makefile.in
CLEANFILES = libstdc++.a

# synccompare is created by replacing its 'import Algorithm::Diff;'
# with a simplified copy of Diff.pm.
#
# If there is a copy of synccompare.pl in the SyncEvolution test directory,
# then it is used instead of the one from the client library.
vpath %.pl $(srcdir)/../test $(FUNAMBOL_SUBDIR)/test/test
vpath %.pm $(srcdir)/../test $(FUNAMBOL_SUBDIR)/test/test/Algorithm
synccompare : Diff.pm synccompare.pl
	perl -e '$$diff = shift; open(DIFF, "<$$diff"); ($$_) = split(/__END__/, join("", <DIFF>)); s/\*import.*//m; s/require +Exporter;//; s/^#.*\n//mg; s/ +#.*\n//mg; $$diff = $$_;' -e 'while(<>) {' @MODIFY_SYNCCOMPARE@ -e 's/use +Algorithm::Diff;/"# embedded version of Algorithm::Diff follows, copyright by the original authors\n" . $$diff . "# end of embedded Algorithm::Diff\n"/e; print;}' $+ >$@
	chmod u+x $@

VOCL_SOURCES = \
	vocl/VObject.h \
	vocl/VProperty.h \
	vocl/VConverter.h \
	vocl/posixadapter.h \
	\
	vocl/VObject.cpp \
	vocl/VProperty.cpp \
	vocl/VConverter.cpp

CORE_SOURCES  = \
	ConfigTree.h \
	ConfigNode.h \
	VolatileConfigNode.h \
	VolatileConfigTree.h \
        EvolutionSmartPtr.h \
	\
	SyncEvolutionUtil.cpp \
	SyncEvolutionUtil.h \
        \
	SyncEvolutionCmdline.cpp \
        SyncEvolutionCmdline.h \
	\
        EvolutionSyncSource.h \
        EvolutionSyncSource.cpp \
	\
        EvolutionSyncClient.h \
	EvolutionSyncClient.cpp \
	\
	SyncEvolutionConfig.h \
	SyncEvolutionConfig.cpp \
	\
	FilterConfigNode.h \
	FilterConfigNode.cpp \
	\
	SafeConfigNode.h \
	SafeConfigNode.cpp \
	\
	PrefixConfigNode.h \
	PrefixConfigNode.cpp \
	\
	FileConfigNode.h \
	FileConfigNode.cpp \
	\
	FileConfigTree.h \
	FileConfigTree.cpp \
	\
	TrackingSyncSource.h \
	TrackingSyncSource.cpp \
	\
	$(VOCL_SOURCES)

SYNCECAL_SOURCES = \
        EvolutionCalendarSource.h \
        EvolutionMemoSource.h \
	EvolutionCalendarSource.cpp \
	EvolutionMemoSource.cpp
if ENABLE_MODULES
else
CORE_SOURCES += EvolutionCalendarSourceRegister.cpp
endif

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
if ENABLE_MODULES
else
CORE_SOURCES += EvolutionContactSourceRegister.cpp
endif

SYNCSQLITE_SOURCES = \
	SQLiteUtil.h \
	SQLiteUtil.cpp \
	SQLiteContactSource.h \
	SQLiteContactSource.cpp
if ENABLE_MODULES
else
CORE_SOURCES += SQLiteContactSourceRegister.cpp
endif

SYNCADDRESSBOOK_SOURCES = \
	AddressBookSource.h \
	AddressBookConstants.cpp \
	AddressBookSource.cpp
if ENABLE_MODULES
else
CORE_SOURCES += AddressBookSourceRegister.cpp
endif

CORE_LDADD = @EPACKAGE_LIBS@ @GLIB_LIBS@ @FUNAMBOL_LIBS@ @ADDRESSBOOK_LIBS@ @LIBS@
CORE_DEP = @FUNAMBOL_DEP@

# put link to static c++ library into current directory, needed if compiling with --enable-static-c++
libstdc++.a :
	path=`$(CXX) $(CORE_LDADD) $(LD_FLAGS) -print-file-name=libstdc++.a` && ln -s $$path .

syncevolution_SOURCES = \
	syncevolution.cpp \
	$(CORE_SOURCES)

# SYNCEVOLUTION_LDADD will be replaced with libsyncebook.la/libsyncecal.la/libsyncsqlite.la
# if linking statically against them, empty otherwise;
# either way this does not lead to a dependency on those libs - done explicitly
#
# LIBDBUS might be replaced with path to patched libdbus-1.a (required for
# DBUS_DEFAULT_TIMEOUT hack on Maemo).
syncevolution_LDADD = $(SYNCEVOLUTION_LDADD) @LIBDBUS@ $(CORE_LDADD)
syncevolution_CXXFLAGS = $(SYNCEVOLUTION_CXXFLAGS)
syncevolution_DEPENDENCIES = $(EXTRA_LTLIBRARIES) $(CORE_DEP)

syncecal_la_SOURCES = $(SYNCECAL_SOURCES)
syncecal_la_LIBADD = @ECAL_LIBS@
# _GNU_SOURCE and -ldl for libical.c + dlsym():
syncecal_la_CPPFLAGS = -D_GNU_SOURCE \
	-De_cal_check_timezones=syncevolution_check_timezones \
	-De_cal_tzlookup_ecal=syncevolution_tzlookup_ecal \
	-De_cal_tzlookup_icomp=syncevolution_tzlookup_icomp \
	-De_cal_match_tzid=syncevolution_match_tzid
syncecal_la_LDFLAGS = -module -rpath '$(pkglibdir)' -ldl
syncecal_la_CXXFLAGS = $(SYNCEVOLUTION_CXXFLAGS)

syncebook_la_SOURCES = $(SYNCEBOOK_SOURCES)
syncebook_la_LIBADD = @EBOOK_LIBS@
syncebook_la_LDFLAGS = -module -rpath '$(pkglibdir)'
syncebook_la_CXXFLAGS = $(SYNCEVOLUTION_CXXFLAGS)

syncsqlite_la_SOURCES = $(SYNCSQLITE_SOURCES)
syncsqlite_la_LIBADD = @SQLITE_LIBS@
syncsqlite_la_LDFLAGS = -module -rpath '$(pkglibdir)'
syncsqlite_la_CXXFLAGS = $(SYNCEVOLUTION_CXXFLAGS)

syncaddressbook_la_SOURCES = $(SYNCADDRESSBOOK_SOURCES)
syncaddressbook_la_LIBADD = @ADDRESSBOOK_LIBS@
syncaddressbook_la_LDFLAGS = -module -rpath '$(pkglibdir)'
syncaddressbook_la_CXXFLAGS = $(SYNCEVOLUTION_CXXFLAGS)

EXTRA_PROGRAMS += vcardconverter
vcardconverter_SOURCES = \
        vocl/vcardconverter.cpp \
	$(VOCL_SOURCES)
vcardconverter_LDADD = $(CORE_LDADD)


# FUNAMBOL_SUBDIR specifies the directory where we compile
# the client library by invoking make there.
# Installing its source is done explicitly.
#
# The path may be empty!
FUNAMBOLSRC = @FUNAMBOLSRC@
FUNAMBOL_SUBDIR = @FUNAMBOL_SUBDIR@
BUILT_SOURCES = $(FUNAMBOL_SUBDIR)/all

@FUNAMBOL_LIB@ : $(FUNAMBOL_SUBDIR)/all

# include client library and boost in distribution
dist-hook:
	cp -r $(srcdir)/boost $(distdir)
	[ ! "$(FUNAMBOL_SUBDIR)" ] || cp -r client-api $(distdir)
	find $(distdir) -name .libs -o -name "*~" -o -name ".*" -o -name "*.o" -o -name "*.lo" -o -name CVS -o -name autom4te.cache | xargs rm -rf

clean-local: testclean
	[ ! -d "$(FUNAMBOL_SUBDIR)" ] || (cd $(FUNAMBOL_SUBDIR) && $(MAKE) clean)

# files created during testing
testclean:
	rm -f *.test.vcf *.log *.tests *.diff *.dat

distclean-local:
	rm -rf $(FUNAMBOL_SUBDIR)
	rm -rf @CLEAN_CLIENT_SRC@ # "client-api", only set when it was created by configure

# With --disable-shared autotools links against libfunambol.a which does not
# pull any of the test suites into the test binary, so they would not be
# executed. The workaround is to explicitly set them as undefined on the
# link line.
client_test_SOURCES = client-test-app.cpp $(CORE_SOURCES)
CLIENT_LIB_TEST_FILES = ClientTest.h ClientTest.cpp client-test-main.cpp
nodist_client_test_SOURCES = $(CLIENT_LIB_TEST_FILES)
client_test_CPPFLAGS = -DHAVE_CONFIG_H -DENABLE_INTEGRATION_TESTS
client_test_CXXFLAGS = `cppunit-config --cflags` $(SYNCEVOLUTION_CXXFLAGS)
client_test_LDFLAGS = `cppunit-config --libs` `nm $(FUNAMBOL_SUBDIR)/src/.libs/libfunambol.a | grep funambolAutoRegisterRegistry | sed -e 's/.* /-u /'`
client_test_LDADD = $(SYNCEVOLUTION_LDADD) @LIBDBUS@ $(FUNAMBOL_SUBDIR)/src/libfunambol.la $(CORE_LDADD)
client_test_DEPENDENCIES = $(EXTRA_LTLIBRARIES) $(CORE_DEP)

# copy test files for several reasons:
# - automake expects them here
# - wchar needs to be removed from distribution to avoid need for defines
# - a locally modified file with suffix ".modified" can take precedence over
#   the upstream version

LOCAL_TEST_FILES=$(notdir $(wildcard $(srcdir)/../test/*))

# sources from SyncEvolution
$(filter $(LOCAL_TEST_FILES), $(CLIENT_LIB_TEST_FILES)) : % : $(srcdir)/../test/%
	sed -e 's/WCHAR_T/char/g' -e 's/TEXT(/(/g' -e 's/wcslen/strlen/g' -e 's/wcscmp/strcmp/g' $< >$@

# sources from compiled client library
$(filter-out $(LOCAL_TEST_FILES), $(CLIENT_LIB_TEST_FILES)) : % : $(FUNAMBOL_SUBDIR)/test/test/%
	sed -e 's/WCHAR_T/char/g' -e 's/TEXT(/(/g' -e 's/wcslen/strlen/g' -e 's/wcscmp/strcmp/g' $< >$@

$(CLIENT_LIB_TEST_FILES:%=$(FUNAMBOL_SUBDIR)/test/test/%): @FUNAMBOL_DEP@

# extra dependency - don't remember why
ClientTest.cpp : ClientTest.h

# verbatim files from SyncEvolution
$(filter $(LOCAL_TEST_FILES), testcases) : % : $(srcdir)/../test/%
	rm -rf $@
	cp -r $< $@

# verbatim files from client library:
# - SQLiteContactSource does not support all fields: filter those out
$(filter-out $(LOCAL_TEST_FILES), testcases) : % : $(FUNAMBOL_SUBDIR)/test/test/%
	rm -rf $@
	cp -r $< $@
	perl -e '$$_ = join("", <>); s/^(ADR|TEL|EMAIL|PHOTO).*?(?=^\S)//msg; print;' testcases/vcard21.vcf >testcases/vcard21_sqlite.vcf

test : client-test testcases synccompare
.PHONY : test

# rule to satisfy automatic dependencies on header files
$(FUNAMBOL_SUBDIR)/%.h :
	[ ! "$(FUNAMBOL_SUBDIR)" ] || ( cd $(FUNAMBOL_SUBDIR)/include && $(MAKE) all )

# default rule for building in $(FUNAMBOL_SUBDIR)
$(FUNAMBOL_SUBDIR)/% :
	[ ! "$(FUNAMBOL_SUBDIR)" ] || ( cd ${@D} && $(MAKE) ${@F} )

# compare the local copy of the C++ client library against
# the files where it was copied from
client-api-diff :
	diff -r -c -x CVS -x config.guess -x config.sub -x depcomp -x install-sh -x ltmain.sh -x missing -x aclocal.m4 -x configure -x '.#*' -x '*~' -x '*.in' -x autom4te.cache $(FUNAMBOLSRC) client-api

# special target for testing with valgrind
valgrind : test
	valgrind --leak-check=yes --suppressions=valgrind.supp ./test
