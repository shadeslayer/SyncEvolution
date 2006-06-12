## Process this file with automake to produce Makefile.in

SUBDIRS = src etc

EXTRA_DIST = HACKING
MAINTAINERCLEANFILES = Makefile.in config.h.in config.guess config.sub configure depcomp install-sh ltmain.sh missing mkinstalldirs
DISTCHECK_CONFIGURE_FLAGS = --with-sync4j=@SYNC4J@

# binary distribution
distbin : README all
	@ [ "$(BINSUFFIX)" ] || (echo "please invoke with e.g. 'make distbin BINSUFFIX=debian-3.1'"; exit 1 ) 
	rm -rf $(distdir)
	$(MAKE) prefix=`pwd`/$(distdir) install
	cp $(srcdir)/README $(srcdir)/NEWS $(srcdir)/COPYING $(distdir)
	tar zcf $(distdir)-$(BINSUFFIX).tar.gz $(distdir)
	rm -rf $(distdir)
