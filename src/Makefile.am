INCLUDES = @EPACKAGE_CFLAGS@ @SYNC4J_CFLAGS@ 

# Please remove '-Wno-return-type' when Evo fixes the warning in their headers.
AM_CFLAGS = -Wall -Werror -Wno-return-type

bin_PROGRAMS = sync4jevolution

CORE_SOURCES  = \
        EvolutionSyncSource.cpp \
	EvolutionSyncClient.cpp \
	EvolutionContactSource.cpp
CORE_LDADD = @EPACKAGE_LIBS@ @SYNC4J_LIBS@ @LIBS@

sync4jevolution_SOURCES = \
	sync4jevolution.cpp \
	$(CORE_SOURCES)

sync4jevolution_LDADD = $(CORE_LDADD)

# test suite

TESTS = test
check_PROGRAMS = $(TESTS)
test_SOURCES = \
        TestMain.cpp \
        TestEvolution.cpp \
	$(CORE_SOURCES)

test_CFLAGS = `cppunit-config --cflags`
test_LDFLAGS = `cppunit-config --libs` $(CORE_LDADD)

valgrind : test
	valgrind --leak-check=yes --suppressions=valgrind.supp ./test
