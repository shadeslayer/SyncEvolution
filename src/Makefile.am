INCLUDES = @EPACKAGE_CFLAGS@ @EBOOK_CFLAGS@ @ECAL_CFLAGS@ @GLIB_CFLAGS@ @SYNC4J_CFLAGS@ @SQLITE_CFLAGS@ @ADDRESSBOOK_CFLAGS@

# Please remove '-Wno-return-type' when Evo fixes the warning in their headers.
AM_CFLAGS = -Wall -Werror -Wno-return-type

bin_PROGRAMS = syncevolution
bin_SCRIPTS = synccompare
EXTRA_LTLIBRARIES = syncecal.la syncebook.la syncsqlite.la syncaddressbook.la
MOSTLYCLEANFILES = syncecal.la syncebook.la syncsqlite.la syncaddressbook.la
pkglib_LTLIBRARIES = @SYNCEVOLUTION_LTLIBRARIES@
EXTRA_PROGRAMS = client-test
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
vpath %.pl $(srcdir)/../test $(SYNC4J_SUBDIR)/test/test
vpath %.pm $(srcdir)/../test $(SYNC4J_SUBDIR)/test/test/Algorithm
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
        EvolutionSmartPtr.h \
        EvolutionSyncClient.h \
        EvolutionSyncSource.h \
	EvolutionClientConfig.h \
        \
        EvolutionSyncSource.cpp \
	EvolutionSyncClient.cpp \
	\
	DeviceManagementNode.h \
	DeviceManagementNode.cpp \
	\
	$(VOCL_SOURCES)

SYNCECAL_SOURCES = \
        EvolutionCalendarSource.h \
        EvolutionMemoSource.h \
	EvolutionCalendarSource.cpp \
	EvolutionMemoSource.cpp

SYNCEBOOK_SOURCES = \
        EvolutionContactSource.h \
	EvolutionContactSource.cpp

SYNCSQLITE_SOURCES = \
	SQLiteSyncSource.h \
	SQLiteSyncSource.cpp \
	SQLiteContactSource.h \
	SQLiteContactSource.cpp

SYNCADDRESSBOOK_SOURCES = \
	AddressBookSource.h \
	AddressBookConstants.cpp \
	AddressBookSource.cpp

CORE_LDADD = @EPACKAGE_LIBS@ @GLIB_LIBS@ @SYNC4J_LIBS@ @ADDRESSBOOK_LIBS@ @LIBS@

# put link to static c++ library into current directory, needed if compiling with --enable-static-c++
libstdc++.a :
	path=`$(CXX) $(CORE_LDADD) $(LD_FLAGS) -print-file-name=libstdc++.a` && ln -s $$path .

syncevolution_SOURCES = \
	syncevolution.cpp \
	$(CORE_SOURCES)

# SYNCEVOLUTION_LDADD@ will be replaced with libsyncebook.la/libsyncecal.la/libsyncsqlite.la
# if linking statically against them, empty otherwise;
# either way this does not lead to a dependency on those libs - done explicitly
#
# LIBDBUS might be replaced with path to patched libdbus-1.a (required for
# DBUS_DEFAULT_TIMEOUT hack on Maemo).
syncevolution_LDADD = @SYNCEVOLUTION_LDADD@ @LIBDBUS@ $(CORE_LDADD)
syncevolution_DEPENDENCIES = @SYNCEVOLUTION_MODULES@

syncecal_la_SOURCES = $(SYNCECAL_SOURCES)
syncecal_la_LIBADD = @ECAL_LIBS@
syncecal_la_LDFLAGS = -module -rpath '$(pkglibdir)'

syncebook_la_SOURCES = $(SYNCEBOOK_SOURCES)
syncebook_la_LIBADD = @EBOOK_LIBS@
syncebook_la_LDFLAGS = -module -rpath '$(pkglibdir)'

syncsqlite_la_SOURCES = $(SYNCSQLITE_SOURCES)
syncsqlite_la_LIBADD = @SQLITE_LIBS@
syncsqlite_la_LDFLAGS = -module -rpath '$(pkglibdir)'

syncaddressbook_la_SOURCES = $(SYNCADDRESSBOOK_SOURCES)
syncaddressbook_la_LIBADD = @ADDRESSBOOK_LIBS@
syncaddressbook_la_LDFLAGS = -module -rpath '$(pkglibdir)'

# test suite - *not* declared as an obligatory check,
# because some of them are known to fail and thus
# prevent a successful "distcheck"
# TESTS = test
# check_PROGRAMS = test
#EXTRA_PROGRAMS += TestEvolution
#TestEvolution_SOURCES = \
#        Test.h \
#        TestMain.cpp \
#        TestEvolution.cpp \
#	$(CORE_SOURCES)

#TestEvolution_CXXFLAGS = `cppunit-config --cflags`
#TestEvolution_LDFLAGS = `cppunit-config --libs`
#TestEvolution_LDADD = @SYNCEVOLUTION_MODULES@ @LIBDBUS@ $(CORE_LDADD)
#TestEvolution_DEPENDENCIES = @SYNCEVOLUTION_MODULES@

EXTRA_PROGRAMS += vcardconverter
vcardconverter_SOURCES = \
        vocl/vcardconverter.cpp \
	$(VOCL_SOURCES)
vcardconverter_LDADD = $(CORE_LDADD)


# SYNC4J_SUBDIR specifies the directory where we compile
# the client library by invoking make there.
# Installing its source is done explicitly.
#
# The path may be empty!
SYNC4JSRC = @SYNC4JSRC@
SYNC4J_SUBDIR = @SYNC4J_SUBDIR@
BUILT_SOURCES = $(SYNC4J_SUBDIR)/all

clean distclean mostlyclean maintainer-clean distdir : % : $(SYNC4J_SUBDIR)/%
clean distclean mostlyclean maintainer-clean : % : %-am
clean : testclean

# With --disable-shared autotools links against libsync4j.a which does not
# pull any of the test suites into the test binary, so they would not be
# executed. The workaround is to explicitly set them as undefined on the
# link line.
client_test_SOURCES = client-test-app.cpp $(CORE_SOURCES)
nodist_client_test_SOURCES = ClientTest.cpp client-test-main.cpp ClientTest.h
client_test_CPPFLAGS = -DHAVE_CONFIG_H -DENABLE_INTEGRATION_TESTS
client_test_CXXFLAGS = `cppunit-config --cflags`
client_test_LDFLAGS = `cppunit-config --libs` `nm $(SYNC4J_SUBDIR)/src/.libs/libsync4j.a | grep funambolAutoRegisterRegistry | sed -e 's/.* /-u /'`
client_test_LDADD = @SYNCEVOLUTION_MODULES@ @LIBDBUS@ $(SYNC4J_SUBDIR)/src/libsync4j.la $(CORE_LDADD)
client_test_DEPENDENCIES = @SYNCEVOLUTION_MODULES@

# copy test files for several reasons:
# - automake expects them here
# - wchar needs to be removed from distribution to avoid need for defines
# - a locally modified file with suffix ".modified" can take precedence over
#   the upstream version

LOCAL_TEST_FILES=$(notdir $(wildcard $(srcdir)/../test/*))

# sources from SyncEvolution
$(filter $(LOCAL_TEST_FILES), ClientTest.h ClientTest.cpp client-test-main.cpp) : % : $(srcdir)/../test/%
	sed -e 's/WCHAR_T/char/g' -e 's/TEXT(/(/g' -e 's/wcslen/strlen/g' -e 's/wcscmp/strcmp/g' $< >$@

# sources from compiled client library
$(filter-out $(LOCAL_TEST_FILES), ClientTest.h ClientTest.cpp client-test-main.cpp) : % : $(SYNC4J_SUBDIR)/test/test/%
	sed -e 's/WCHAR_T/char/g' -e 's/TEXT(/(/g' -e 's/wcslen/strlen/g' -e 's/wcscmp/strcmp/g' $< >$@

# extra dependency - don't remember why
ClientTest.cpp : ClientTest.h

# verbatim files from SyncEvolution
$(filter $(LOCAL_TEST_FILES), testcases) : % : $(srcdir)/../test/%
	rm -rf $@
	cp -r $< $@

# verbatim files from client library
$(filter-out $(LOCAL_TEST_FILES), testcases) : % : $(SYNC4J_SUBDIR)/test/test/%
	rm -rf $@
	cp -r $< $@

test : client-test testcases synccompare


# TestEvolution syncevolution vcardconverter : \
#        @CORE_LDADD_DEP@ \
#	$(SYNC4J_SUBDIR)/src/libsync4j.la
#test : TestEvolution addressbook.tests calendar.tests todo.tests memo.tests synccompare vcardconverter

# test files are in CVS under a different name so that they
# can be copied to the work directory under the name expected
# by the "test" program
#addressbook.tests : testVCard.vcf
#	cp $< $@
#calendar.tests : testCalendar.ics
#	cp $< $@
#todo.tests : testTask.ics
#	cp $< $@
#memo.tests : testMemo.ics
#	cp $< $@

testclean :
	rm -f *.test.vcf *.log *.tests *.diff

$(SYNC4J_SUBDIR)/maintainer-clean $(SYNC4J_SUBDIR)/distclean :
	rm -rf $(SYNC4J_SUBDIR) @CLEAN_CLIENT_SRC@

$(SYNC4J_SUBDIR)/distdir :
	[ ! "$(SYNC4J_SUBDIR)" ] || ( cp -r client-api $(distdir) && ( find $(distdir)/client-api -name .libs -o -name "*.o" -o -name "*.lo" -o -name CVS -name autom4te.cache | xargs rm -rf ) )

# rule to satisfy automatic dependencies on header files
$(SYNC4J_SUBDIR)/%.h :
	[ ! "$(SYNC4J_SUBDIR)" ] || ( cd $(SYNC4J_SUBDIR)/include && $(MAKE) all )

# default rule for building in $(SYNC4J_SUBDIR)
$(SYNC4J_SUBDIR)/% :
	[ ! "$(SYNC4J_SUBDIR)" ] || ( cd ${@D} && $(MAKE) ${@F} )

# compare the local copy of the C++ client library against
# the files where it was copied from
client-api-diff :
	diff -r -c -x CVS -x config.guess -x config.sub -x depcomp -x install-sh -x ltmain.sh -x missing -x aclocal.m4 -x configure -x '.#*' -x '*~' -x '*.in' -x autom4te.cache $(SYNC4JSRC) client-api

# special target for testing with valgrind
valgrind : test
	valgrind --leak-check=yes --suppressions=valgrind.supp ./test
