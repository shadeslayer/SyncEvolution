INCLUDES = @EPACKAGE_CFLAGS@ @GLIB_CFLAGS@ @SYNC4J_CFLAGS@ 

# Please remove '-Wno-return-type' when Evo fixes the warning in their headers.
AM_CFLAGS = -Wall -Werror -Wno-return-type

bin_PROGRAMS = syncevolution
bin_SCRIPTS = synccompare
EXTRA_DIST = normalize_vcard.pl testVCard.vcf testCalendar.ics testTask.ics vocl/README
EXTRA_PROGRAMS = client-test
check_PROGRAMS = @CPPUNIT_TESTS@
TESTS = @CPPUNIT_TESTS@

DISTCLEANFILES = synccompare
MAINTAINERCLEANFILES = Makefile.in

synccompare : normalize_vcard.pl
	cp $< $@
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
        EvolutionContactSource.h \
        EvolutionCalendarSource.h \
        EvolutionSmartPtr.h \
        EvolutionSyncClient.h \
        EvolutionSyncSource.h \
        \
        EvolutionSyncSource.cpp \
	EvolutionSyncClient.cpp \
	EvolutionCalendarSource.cpp \
	EvolutionContactSource.cpp \
	\
	$(VOCL_SOURCES)

CORE_LDADD = @EPACKAGE_LIBS@ @GLIB_LIBS@ @SYNC4J_LIBS@ @LIBS@

syncevolution_SOURCES = \
	syncevolution.cpp \
	$(CORE_SOURCES)

syncevolution_LDADD = $(CORE_LDADD)

# test suite - *not* declared as an obligatory check,
# because some of them are known to fail and thus
# prevent a successful "distcheck"
# TESTS = test
# check_PROGRAMS = test
EXTRA_PROGRAMS += TestEvolution
TestEvolution_SOURCES = \
        Test.h \
        TestMain.cpp \
        TestEvolution.cpp \
	$(CORE_SOURCES)

TestEvolution_CXXFLAGS = `cppunit-config --cflags`
TestEvolution_LDFLAGS = `cppunit-config --libs`
TestEvolution_LDADD = $(CORE_LDADD)

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

clean distclean mostlyclean distdir maintainer-clean : % : $(SYNC4J_SUBDIR)/%
clean : testclean

# With --disable-shared autotools links against libsync4j.a which does not
# pull any of the test suites into the test binary, so they would not be
# executed. The workaround is to explicitly set them as undefined on the
# link line.
client_test_SOURCES = client-test-app.cpp ClientTest.cpp client-test-main.cpp $(CORE_SOURCES)
client_test_LDFLAGS = `nm $(SYNC4J_SUBDIR)/src/.libs/libsync4j.a | grep funambolAutoRegisterRegistry | sed -e 's/.* /-u /'`
client_test_LDADD = $(SYNC4J_SUBDIR)/src/libsync4j.la $(CORE_LDADD)

vpath %.cpp : $(SYNC4J_SUBDIR)/test/test

TestEvolution syncevolution vcardconverter : \
	$(SYNC4J_SUBDIR)/src/libsync4j.la
test : TestEvolution addressbook.tests calendar.tests todo.tests synccompare vcardconverter

# test files are in CVS under a different name so that they
# can be copied to the work directory under the name expected
# by the "test" program
addressbook.tests : testVCard.vcf
	cp $< $@
calendar.tests : testCalendar.ics
	cp $< $@
todo.tests : testTask.ics
	cp $< $@

testclean :
	rm -f *.test.vcf *.log *.tests *.diff

$(SYNC4J_SUBDIR)/maintainer-clean $(SYNC4J_SUBDIR)/distclean :
	rm -rf $(SYNC4J_SUBDIR) client-api

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
	diff -r -c -x CVS -x '.#*' -x '*~' -x '*.in' -x autom4te.cache $(SYNC4JSRC) client-api

# special target for testing with valgrind
valgrind : test
	valgrind --leak-check=yes --suppressions=valgrind.supp ./test
