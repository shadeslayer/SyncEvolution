## Process this file with automake to produce Makefile.in

SUBDIRS = src etc

EXTRA_DIST = HACKING
DISTCHECK_CONFIGURE_FLAGS = --with-sync4j=@SYNC4J@

# binary distribution
distbin : README all
	@ [ "$(BINSUFFIX)" ] || (echo "please invoke with e.g. 'make distbin BINSUFFIX=debian-3.1'"; exit 1 ) 
	rm -rf $(distdir)
	$(MAKE) prefix=`pwd`/$(distdir) install
	cp README $(distdir)
	tar zcf $(distdir)-$(BINSUFFIX).tar.gz $(distdir)
	rm -rf $(distdir)
