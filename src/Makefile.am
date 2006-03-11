INCLUDES = @EPACKAGE_CFLAGS@ @SYNC4J_CFLAGS@ 

# Please remove '-Wno-return-type' when Evo fixes the warning in their headers.
AM_CFLAGS = -Wall -Werror -Wno-return-type

bin_PROGRAMS = syncevolution
bin_SCRIPTS = normalize_vcard.pl

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
check_PROGRAMS = test
test_SOURCES = \
        TestMain.cpp \
        TestEvolution.cpp \
	$(CORE_SOURCES)

test_CFLAGS = `cppunit-config --cflags`
test_LDFLAGS = `cppunit-config --libs` $(CORE_LDADD)

valgrind : test
	valgrind --leak-check=yes --suppressions=valgrind.supp ./test
