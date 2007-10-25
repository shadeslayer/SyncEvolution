## Process this file with automake to produce Makefile.in

SUBDIRS = src etc test

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

iphone : SyncEvolution.plist

IPHONE_FILENAME = syncevolution-$(VERSION)-iphone.zip

SyncEvolution.plist : SyncEvolution.plist.in $(IPHONE_FILENAME)
	sed -e 's/__FILENAME__/$(IPHONE_FILENAME)/' \
	    -e 's/__VERSION__/$(VERSION)/' \
	    -e 's/__SIZE__/$(shell ls -l $(IPHONE_FILENAME) | sed -e 's/  */ /g' | cut -d ' ' -f 5)/' \
	    $< >$@

$(IPHONE_FILENAME) : all
	rm -rf syncevolution-iphone $@
	$(MAKE) DESTDIR=`pwd`/syncevolution-iphone install
	rm -rf `ls -1 -d syncevolution-iphone/usr/share/doc/syncevolution/*/spds/sources/* | grep -v addressbook`
	perl -pi -e 's;SyncEvolution test #1;;;' \
	         -e 's;^type = text/(x-)?vcard;type = addressbook;m;' \
                 syncevolution-iphone/usr/share/doc/syncevolution/*/spds/sources/addressbook/config.txt
	cd syncevolution-iphone && zip -r ../$(IPHONE_FILENAME) .

TYPE_deb = -D
TYPE_rpm = -R
REQUIRES_deb = --requires="`dpkg-shlibdeps -L$(srcdir)/src/shlibs.local -O src/syncevolution | sed -e 's/[^=]*=//'`"
VERSION_deb = $(VERSION)
VERSION_rpm = `echo $(VERSION) | sed -e s/-/_/g`
RELEASE = 2

# The package name: BINSUFFIX is used to distinguish binaries
# for different Evolution releases.
PKGNAME=syncevolution$(patsubst %,-%,$(BINSUFFIX))

# This is a list of packages (potentially) provided on estamos.de.
# The current package conflicts with any of them, but not itself.
PKGS = $(addprefix syncevolution-evolution-, 2.0 2.2 2.4 2.6 2.8 2.10 2.12 2.14 2.16)

# When calling checkinstall we cannot install into /tmp
# because any file created there will be excluded: that makes
# sense, because "make install" might create temporary files
# there. The current directory might be in /tmp, so use $HOME.
deb rpm : all doc-pak
	tmpdir=`mktemp -d $$HOME/syncevolution.XXXXXXXXXX` && \
	trap "rm -rf $$tmpdir" EXIT && \
	docdir="$(docdir)" && \
        if [ ! "$$docdir" ]; then docdir="$(datadir)/doc/$(PACKAGE)"; fi && \
	fakeroot checkinstall \
		$(TYPE_$@) \
		$(REQUIRES_$@) \
		--fstrans=yes \
		--pkgversion=$(VERSION_$@) \
		--pkgrelease=$(RELEASE) \
		--pkgname=$(PKGNAME) \
		--provides=syncevolution \
		--conflicts="`echo $(filter-out $(PKGNAME), $(PKGS)) | sed -e 's/  */, /g'`" \
		--maintainer='Patrick Ohly <patrick.ohly@gmx.de>' \
		--pkgsource='http://sourceforge.net/project/showfiles.php?group_id=146288' \
		--pkgaltsource='http://www.estamos.de/projects/SyncML/' \
		--pkggroup='gnome' \
		--docdir=$$docdir $(MAKE) install

doc-pak : README NEWS COPYING
	mkdir $@
	cp $+ $@

description-pak : description
	cp $< $@
