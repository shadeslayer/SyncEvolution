## Process this file with automake to produce Makefile.in

ACLOCAL_AMFLAGS = -I m4 -I m4-repo

man_MANS =
dist_doc_DATA =
SUBDIRS = src
if COND_CORE
SUBDIRS += po test
dist_doc_DATA += README NEWS COPYING $(TEST_README_FILES) test/syncevo-http-server-logging.conf
if COND_HTML_README
dist_doc_DATA += README.html
endif
if COND_MAN_PAGES
man_MANS += syncevolution.1
endif
endif
DIST_SUBDIRS = src po test

EXTRA_DIST = \
	HACKING \
	LICENSE.txt \
	LICENSE.LGPL-21 \
        README.rst \
	description \
	configure-pre.in \
	configure-post.in \
	gen-autotools.sh \
	autogen.sh \
	Doxyfile

MAINTAINERCLEANFILES = Makefile.in config.h.in config.guess config.sub configure depcomp install-sh ltmain.sh missing mkinstalldirs 
CLEANFILES =

TEST_README_FILES = $(wildcard $(srcdir)/test/README.*)

DEV_FILE_PATTERN = $(1)$(2)/etc $(1)$(2)/include $(1)$(2)/lib/*.so $(1)$(2)/lib/*.a $(1)$(2)/lib/*.la $(1)$(2)/lib/*/*.la $(1)$(2)/lib/pkgconfig

# binary distribution as .tar.gz
if COND_DBUS
# when building with D-Bus, we have no choice: the service has to go into /usr
# in order to be found
distbin : $(dist_doc_DATA) INSTALL-tar-gz all
	@ [ "$(BINSUFFIX)" ] || (echo "please invoke with e.g. 'make distbin BINSUFFIX=debian-3.1'"; exit 1 )
	@ [ "$(prefix)" = "/usr" ] || (echo "please reconfigure with --prefix=/usr"; exit 1 )
	rm -rf $(distdir)
	$(MAKE) DESTDIR=`pwd`/$(distdir) install
	$(MAKE) DESTDIR=`pwd`/$(distdir) installcheck
	rm -rf $(call DEV_FILE_PATTERN,$(distdir),/usr)
	for i in `find $(distdir) -type d | sort -r`; do rmdir $$i 2>/dev/null || true; done
	mkdir -p $(distdir)/usr/share/doc/syncevolution
	cp $(srcdir)/INSTALL-tar-gz $(distdir)/INSTALL
	cp $(filter-out all, $+) $(distdir)/usr/share/doc/syncevolution
	tar zcf $(distdir)-$(BINSUFFIX).tar.gz $(distdir)
	rm -rf $(distdir)
else
# without D-Bus, we can simply create an archive with a bin directory
# and everything works
distbin : $(dist_doc_DATA) all
	@ [ "$(BINSUFFIX)" ] || (echo "please invoke with e.g. 'make distbin BINSUFFIX=debian-3.1'"; exit 1 ) 
	rm -rf $(distdir)
	$(MAKE) prefix=`pwd`/$(distdir) install
	echo removing developer files and empty directories
	rm -rf $(call DEV_FILE_PATTERN,$(distdir),/)
	for i in `find $(distdir) -type d | sort -r`; do rmdir $$i 2>/dev/null || true; done
	cp $(filter-out all, $+) $(distdir)
	tar zcf $(distdir)-$(BINSUFFIX).tar.gz $(distdir)
	rm -rf $(distdir)
endif

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
# Dependency calculation is intentionally incomplete:
# - don't force dependency on specific EDS libs via backends, their versions change too much (handled via --enable-evolution-compatibility and dynamic loading of the backends)
# - ignore client-test dependencies (because users typically don't run it)
REQUIRES_deb = --requires="'$(shell set -x; cd dist; LD_LIBRARY_PATH=$(distdir)/usr/lib:$(distdir)/usr/lib/syncevolution dpkg-shlibdeps -L$(srcdir)/src/shlibs.local --ignore-missing-info -O $$(for i in $$(find $(distdir) -type f -perm /u+x | grep -v -e client-test -e lib/syncevolution/sync); do if file $$i | grep ELF >/dev/null; then echo $$i; fi; done) | sed -e 's/[^=]*=//')$(REQUIRES_deb_neon)'"
if NEON_COMPATIBILITY
# --enable-neon-compatibility in src/backends/webdav:
# replace dependencies from linking with hard-coded dlopen() dependencies
REQUIRES_deb_neon = , libneon27 (>= 0.29.0) | libneon27-gnutls (>= 0.29.0)
else
REQUIRES_deb_neon =
endif
VERSION_deb = 1:$(STABLE_VERSION)$(VERSION)
VERSION_rpm = `echo $(VERSION) | sed -e s/-/_/g`
RELEASE = 2

# The package name: BINSUFFIX is used to distinguish binaries
# for different Evolution releases.
PKGNAME=syncevolution$(patsubst %,-%,$(BINSUFFIX))

# This is a list of packages (potentially) provided on estamos.de.
# The current package conflicts with any of them, but not itself.
PKGS = $(addprefix syncevolution-evolution-, 2.6 2.8 2.12)

# When calling checkinstall we cannot install into /tmp
# because any file created there will be excluded: that makes
# sense, because "make install" might create temporary files
# there. The current directory might be in /tmp, so use $HOME.
#
# --replaces is necessary for migrating from syncevolution-evolution-<evover>
# to syncevolution-evolution (as per http://wiki.debian.org/Renaming_a_Package)
#
# When we build shared objects, then conflict with the corresponding
# system libs. The assumption is that the system library is named
# after the lib and its major version, which holds for libsmltk and
# libsynthesis in Debian.
deb rpm : dist/$(distdir) dist/debian/control description-pak
	conflicts=`ls -1 dist/$(distdir)/usr/lib/*.so.[0123456789] | sed -e 's;.*/;;' -e 's/\.so\.//' -e 's/$$/, /'` && \
	tmpdir=`mktemp -d $$HOME/syncevolution.XXXXXXXXXX` && \
	trap "rm -rf $$tmpdir" EXIT && \
	fakeroot checkinstall </dev/null \
		$(TYPE_$@) \
		$(REQUIRES_$@) \
		--fstrans=yes \
		--strip=no \
		--pkgversion=$(VERSION_$@) \
		--pkgrelease=$(RELEASE) \
		--pkgname=$(PKGNAME) \
		--pkgarch=$(PKGARCH) \
		--provides=syncevolution \
	        --replaces="'syncevolution, `echo $(filter-out $(PKGNAME), $(PKGS)) | sed -e 's/  */ (<< 1:0.8.1-2), /g'` (<< 1:0.8.1-2)'" \
		--conflicts="'$${conflicts}syncevolution, `echo $(filter-out $(PKGNAME), $(PKGS)) | sed -e 's/  */ (<< 1:0.8.1-2), /g'` (<< 1:0.8.1-2)'" \
		--maintainer="'Patrick Ohly <patrick.ohly@gmx.de>'" \
		--pkgsource='http://sourceforge.net/project/showfiles.php?group_id=146288' \
		--pkgaltsource='http://www.estamos.de/projects/SyncML/' \
		--pkggroup='gnome' \
		cp -r `pwd`/$</usr/* /usr
	if [ $@ = "rpm" ]; then cp /usr/src/rpm/RPMS/*/${PKGNAME}-${VERSION}-2.*.rpm .; fi

.PHONY: dist/$(distdir) clean_dist
dist/$(distdir): all
	rm -rf $@
	$(MAKE) install DESTDIR=`pwd`/$@
	$(MAKE) installcheck DESTDIR=`pwd`/$@
	rm -rf $(call DEV_FILE_PATTERN, $@)
clean-local: clean_dist
clean_dist:
	rm -rf dist description-pak

# required by dpkg-shlibdeps
dist/debian/control:
	mkdir -p ${@D}
	touch $@

# for checkinstall
description-pak : description
	cp $< $@

# Build "html" inside the build dir, using source files
# from the SyncEvolution source directory and (if built)
# the installed client-api.build directory.
.PHONY: doc clean-html
doc:
	rm -rf html
	export VERSION="SyncEvolution $(VERSION)"; \
	export OUTPUT_DIRECTORY="`pwd`"; \
	export PREDEFINED="@BACKEND_DEFINES@ ENABLE_UNIT_TESTS ENABLE_INTEGRATION_TESTS"; \
	export CLIENT_LIBRARY="@FUNAMBOL_SUBDIR@/test @FUNAMBOL_SUBDIR@/include "; \
	export STRIP_FROM_PATH="$(srcdir) `dirname @FUNAMBOL_SUBDIR@`"; \
	cd $(srcdir); doxygen
clean-local: clean-html
clean-html:
	rm -rf html

# configure.in is updated
# by gen-autotools.sh. This scripts is run
# each time "make all" (which depends on "Makefile")
# runs; this is easier than trying to anticipated
# when the script would produce different output.
# The no-op "true" seem to be necessary for make to
# re-check the configure.in time stamps.
$(srcdir)/configure.in: check-autotools
	@ true

# Rebuilding the autotools files should never be necessary
# with source tar balls. Some parts of it depend on "git describe".
# So avoid it when there is no .git repo.
.PHONY: check-autotools
check-autotools:
	@ cd $(srcdir) && ( [ ! -e .git ] || ./gen-autotools.sh )

dist-hook:
	@if test -d "$(srcdir)/.git"; \
	then \
		echo Creating ChangeLog && \
		( cd "$(top_srcdir)" && \
		  echo '# Generated by configure.  Do no edit.'; echo; \
		  $(top_srcdir)/missing --run perl $(top_srcdir)/build/gen-changelog.pl ) > ChangeLog.tmp \
		&& mv -f ChangeLog.tmp $(top_distdir)/ChangeLog \
		|| ( rm -f ChangeLog.tmp ; \
		     echo Failed to generate ChangeLog >&2 ); \
	else \
		echo A git checkout is required to generate a ChangeLog >&2; \
	fi

if ENABLE_EVOLUTION_COMPATIBILITY
# check .so (relevant for modular builds) and main syncevolution binary
# (relevant in that case and for static builds) for dependencies on
# problematic libraries and symbols
#
# ical_strdup is an exception because it is in SyncEvolution.
installcheck-local:
	for i in `find $(DESTDIR)/$(libdir)/syncevolution $(DESTDIR)/$(libdir)/libsyncevo* $(DESTDIR)/$(libdir)/libsynthesis* -name *.so` $(DESTDIR)/$(bindir)/syncevolution; do \
		if objdump -T -C $$i | grep -v :: | grep '\*UND\*' | sort | grep -v -w ical_strdup | grep -e ical -e " e_"; then \
			echo "$$i should not depend on EDS, libical or libbluetooth"; \
			exit 1; \
		fi; \
		if ldd $$i | grep -e libecal -e libebook -e libedata -e libical -e libbluetooth; then \
			echo "$$i should not be linked against EDS, libical or libbluetooth"; \
			exit 1; \
		fi; \
	done
endif


# patch README.rst properties on-the-fly
README.patched.rst: README.rst src/syncevolution
	perl -e '$$syncfound=0; $$sourcefound=0; $$res=0;' \
	                -e 'sub run { $$cmd = shift; $$buffer = `$$cmd`; die if $$?; return $$buffer; }' \
	                -e 'while (<>) {' \
	                -e 's/^:Version: .*/:Version: $(VERSION)/;' \
	                -e 's/:Date: .*/":Date: " . `date +%Y-%m-%d`/e;' \
	                -e 'if (s;<<insert sync-property>>\n;run("src/syncevolution --daemon=no --sync-property ?");e) { $$syncfound=1; }' \
	                -e 'if (s;<<insert source-property>>\n;run("src/syncevolution --daemon=no --source-property ?");e) { $$sourcefound=1; }' \
	                -e 'print;' \
	                -e '}' \
	                -e 'die "<<insert sync-property>> not in README.rst?!" unless $$syncfound;' \
	                -e 'die "<<insert source-property>> not in README.rst?!" unless $$sourcefound;' \
	                -e 'exit $$res;' \
	                $< >$@
CLEANFILES += README.patched.rst

# produce man pages
syncevolution.1: README.patched.rst
	$(RST2MAN) --exit-status=3 $< >$@
CLEANFILES += syncevolution.1

# README is the traditional name in the distribution,
# continue using it instead of README.rst.
README: README.patched.rst
	cp $< $@
CLEANFILES += README

# The README.html is also used on syncevolution.org as "Usage" page,
# therefore we must use <h3> headers and lower to fit into the page.
README.html: README.patched.rst
	$(RST2HTML) --initial-header-level=3 --exit-status=3 $< >$@
CLEANFILES += README.html
