# The only purpose of this Makefile is to build
# the synthesis header files if
# a) the Synthesis source is bundled with SyncEvolution and
# b) the core engine (which includes Synthesis) is disabled
#
# The GTK GUI needs some of the headers (syerror.h).

all:
	cd $(SYNTHESIS_SUBDIR)/src && $(MAKE) synthesis/syerror.h synthesis/generic_types.h
