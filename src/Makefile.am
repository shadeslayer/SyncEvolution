INCLUDES = @EPACKAGE_CFLAGS@ @SYNC4J_CFLAGS@ 

# Please remove '-Wno-return-type' when Evo fixes the warning in their headers.
AM_CFLAGS = -Wall -Werror -Wno-return-type

bin_PROGRAMS = syncevolution
bin_SCRIPTS = normalize_vcard.pl

EXTRA_DIST = normalize_vcard.pl

CORE_SOURCES  = \
        EvolutionContactSource.h \
        EvolutionSmartPtr.h \
        EvolutionSyncClient.h \
        EvolutionSyncSource.h \
        \
        EvolutionSyncSource.cpp \
	EvolutionSyncClient.cpp \
	EvolutionContactSource.cpp
CORE_LDADD = @EPACKAGE_LIBS@ @SYNC4J_LIBS@ @LIBS@

syncevolution_SOURCES = \
	syncevolution.cpp \
	$(CORE_SOURCES)

syncevolution_LDADD = $(CORE_LDADD)

# test suite - *not* declared as an obligatory check,
# because some of them are known to fail and thus
# prevent a successful "distcheck"
# TESTS = test
# check_PROGRAMS = test
EXTRA_PROGRAMS = test
test_SOURCES = \
        TestMain.cpp \
        TestEvolution.cpp \
	$(CORE_SOURCES)

test_CFLAGS = `cppunit-config --cflags`
test_LDFLAGS = `cppunit-config --libs` $(CORE_LDADD)

# SYNC4J_SUBDIR specifies the directory where we compile
# the client library by invoking make there.
# Installing its source is done explicitly.
#
# The path may be empty!
SYNC4J_SUBDIR = @SYNC4J_SUBDIR@
BUILT_SOURCES = $(SYNC4J_SUBDIR)/all libsync4j.la
CLEANFILES = libsync4j.la .libs/libsync4j.a .libs/libsync4j.la

clean distclean mostlyclean distdir : % : $(SYNC4J_SUBDIR)/%

$(SYNC4J_SUBDIR)/distclean :
	rm -rf $(SYNC4J_SUBDIR) client-api

$(SYNC4J_SUBDIR)/distdir :
	[ ! "$(SYNC4J_SUBDIR)" ] || ( cp -a client-api $(distdir) && ( find $(distdir)/client-api -name .libs -o -name "*.o" -o -name "*.lo" -o -name CVS -name autom4te.cache | xargs rm -rf ) )

$(SYNC4J_SUBDIR)/% :
	[ ! "$(SYNC4J_SUBDIR)" ] || ( cd ${@D} && $(MAKE) ${@F} )

libsync4j.la : $(SYNC4J_SUBDIR)/all
	mkdir -p .libs
	cp -a $(SYNC4J_SUBDIR)/src/.libs/libsync4j* .libs
	cp -a $(SYNC4J_SUBDIR)/src/libsync4j.la .

valgrind : test
	valgrind --leak-check=yes --suppressions=valgrind.supp ./test
