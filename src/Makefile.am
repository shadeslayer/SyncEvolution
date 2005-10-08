INCLUDES = @EPACKAGE_CFLAGS@ @SYNC4J_CFLAGS@ 

# Please remove '-Wno-return-type' when Evo fixes the warning in their headers.
AM_CFLAGS = -Wall -Werror -Wno-return-type

bin_PROGRAMS = sync4jevolution

sync4jevolution_SOURCES = sync4jevolution.cpp

sync4jevolution_LDADD = @EPACKAGE_LIBS@ @SYNC4J_LIBS@ @LIBS@
