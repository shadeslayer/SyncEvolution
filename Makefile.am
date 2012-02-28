include $(top_srcdir)/setup-variables.am
include $(top_srcdir)/autotroll.am
include $(top_srcdir)/build/build.am

AUTOMAKE_OPTIONS = subdir-objects

## Process this file with automake to produce Makefile.in

ACLOCAL_AMFLAGS = -I m4 -I m4-repo ${ACLOCAL_FLAGS}

if COND_CORE
SUBDIRS += $(SYNTHESIS_SUBDIR)
else
SUBDIRS += $(SYNTHESIS_SUBDIR_INCLUDES)
endif

SUBDIRS += .

# choose D-Bus implementation
if COND_GIO_GDBUS
gdbus_dir = $(top_srcdir)/src/gdbusxx
gdbus_build_dir = src/gdbusxx
else
gdbus_dir = $(top_srcdir)/src/gdbus
gdbus_build_dir = src/gdbus
endif

disted_docs =
distbin_docs =

man_MANS =
include src/src.am
if COND_CORE
include test/test.am
SUBDIRS += po
disted_docs += README NEWS COPYING $(TEST_README_FILES) test/syncevo-http-server-logging.conf
distbin_docs += $(disted_docs)
dist_doc_DATA += $(disted_docs)
if COND_HTML_README
distbin_docs += README.html
# do not distribute in tarball.
doc_DATA += README.html
endif
if COND_MAN_PAGES
man_MANS += syncevolution.1
endif
endif
DIST_SUBDIRS += po

dist_noinst_DATA += \
  HACKING \
  LICENSE.txt \
  LICENSE.LGPL-21 \
  README.rst \
  description \
  autogen.sh \
  Doxyfile \
  po/LINGUAS.README

MAINTAINERCLEANFILES += Makefile.in config.h.in config.guess config.sub configure depcomp install-sh ltmain.sh missing mkinstalldirs

TEST_README_FILES = $(wildcard $(top_srcdir)/test/README.*)

DEV_FILE_PATTERN = $(1)$(2)/etc $(1)$(2)/include $(1)$(2)/lib/*.so $(1)$(2)/lib/*.a $(1)$(2)/lib/*.la $(1)$(2)/lib/*/*.la $(1)$(2)/lib/pkgconfig

# binary distribution as .tar.gz
if COND_DBUS
# when building with D-Bus, we have no choice: the service has to go into /usr
# in order to be found
distbin : $(distbin_docs) INSTALL-tar-gz all
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
distbin : $(distbin_docs) all
	@ [ "$(BINSUFFIX)" ] || (echo "please invoke with e.g. 'make distbin BINSUFFIX=debian-3.1'"; exit 1 ) 
	rm -rf $(distdir)
	$(MAKE) prefix=`pwd`/$(distdir) install
	@echo removing developer files and empty directories
	rm -rf $(call DEV_FILE_PATTERN,$(distdir),/)
	for i in `find $(distdir) -type d | sort -r`; do rmdir $$i 2>/dev/null || true; done
	cp $(filter-out all, $+) $(distdir)
	tar zcf $(distdir)-$(BINSUFFIX).tar.gz $(distdir)
	rm -rf $(distdir)
endif

iphone : SyncEvolution.plist

IPHONE_FILENAME = syncevolution-$(VERSION)-iphone.zip

SyncEvolution.plist : SyncEvolution.plist.in $(IPHONE_FILENAME)
	$(AM_V_GEN)sed -e 's/__FILENAME__/$(IPHONE_FILENAME)/' \
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
# - be more flexible about kdelibs5 than dpkg-shlibdeps: it is found as package
#   for libkdeui.so.5 and libkdecore.so.5 on Ubuntu Lucid, but after Debian
#   Squeeze the package was replaced by individual library packages. On such
#   distros, libkdeui5 is what we need.
# - same for kdepimlibs5 -> libakonadi-kde4
REQUIRES_deb = --requires="'$(shell set -x; cd dist; LD_LIBRARY_PATH=$(distdir)/usr/lib:$(distdir)/usr/lib/syncevolution dpkg-shlibdeps -L$(srcdir)/src/shlibs.local --ignore-missing-info -O $$(for i in $$(find $(distdir) -type f -perm /u+x | grep -v -e client-test -e lib/syncevolution/sync); do if file $$i | grep ELF >/dev/null; then echo $$i; fi; done) | sed -e 's/kdelibs5 ([^,]*),/kdelibs5 | libkdeui5,/g' -e 's/kdepimlibs5 ([^,]*),/kdepimlibs5 | libakonadi-kde4,/g' -e 's/[^=]*=//')$(REQUIRES_deb_neon)'"
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

all_phonies += dist/$(distdir) clean_dist
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
	mkdir -p ${dir $@}
	touch $@

# for checkinstall
description-pak : description
	cp $< $@

# Build "html" inside the build dir, using source files
# from the SyncEvolution source directory and (if built)
# the installed client-api.build directory.
all_phonies += doc clean-html
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

all_dist_hooks += dot_dist_hook
dot_dist_hook:
	@if test -d "$(srcdir)/.git"; \
	then \
	  printf '%s' 'Creating ChangeLog...' && \
	  ( cd "$(top_srcdir)" && \
	    printf '%s\n\n' '# Generated by configure.  Do not edit.'; \
	    $(top_srcdir)/missing --run perl $(top_srcdir)/build/gen-changelog.pl ) > ChangeLog.tmp && \
	  ( mv -f ChangeLog.tmp $(top_distdir)/ChangeLog && \
	    printf '%s\n' ' done.' ) || \
	  ( rm -f ChangeLog.tmp ; \
	    printf '%s\n' ' failed.'; \
	    echo Failed to generate ChangeLog >&2 ); \
	else \
	  echo 'A git checkout is required to generate a ChangeLog.' >&2; \
	fi

if ENABLE_EVOLUTION_COMPATIBILITY
# check .so (relevant for modular builds) and main syncevolution binary
# (relevant in that case and for static builds) for dependencies on
# problematic libraries and symbols
#
# ical_strdup is an exception because it is in SyncEvolution.
all_local_installchecks += toplevel_so_check
toplevel_so_check:
	for i in `find $(DESTDIR)/$(libdir)/syncevolution $(DESTDIR)/$(libdir)/libsyncevo* $(DESTDIR)/$(libdir)/libsynthesis* -name *.so` $(DESTDIR)/$(bindir)/syncevolution; \
	do \
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

# Be strict about running 'syncevolution' only when not doing
# cross-compilation: in that case, if running 'syncevolution' fails,
# abort the build process. Otherwise proceed with the fallback below,
# which is to keep the "see --sync/source-property ?" placeholders in
# the README.
if COND_CROSS_COMPILING
RUN_SYNCEVOLUTION_CHECK=if ($$?) { return ""; } else { return $$buffer; }
else
RUN_SYNCEVOLUTION_CHECK=die if $$?; return $$buffer;
endif

# patch README.rst properties on-the-fly
README.patched.rst: README.rst src/syncevolution
	$(AM_V_GEN)perl -e '$$syncfound=0; $$sourcefound=0; $$res=0;' \
	                -e 'sub run { $$cmd = shift; $$buffer = `env LD_LIBRARY_PATH=src/syncevo/.libs:src/gdbus/.libs:src/gdbusxx/.libs:src/build-synthesis/src/.libs:$$ENV{LD_LIBRARY_PATH} $$cmd`; $(RUN_SYNCEVOLUTION_CHECK) }' \
	                -e 'while (<>) {' \
	                -e 's/^:Version: .*/:Version: $(VERSION)/;' \
	                -e 's/:Date: .*/":Date: " . `date +%Y-%m-%d`/e;' \
	                -e 'if (s;(<< see "syncevolution --sync-property ." >>\n);run("src/syncevolution --daemon=no --sync-property ?") || $$1;e) { $$syncfound=1; }' \
	                -e 'if (s;(<< see "syncevolution --source-property ." >>\n);run("src/syncevolution --daemon=no --source-property ?") || $$1;e) { $$sourcefound=1; }' \
	                -e 'print;' \
	                -e '}' \
	                -e 'die "<<sync-property>> tag not in README.rst?!" unless $$syncfound;' \
	                -e 'die "<<source-property>> tag not in README.rst?!" unless $$sourcefound;' \
	                -e 'exit $$res;' \
	                $< >$@
CLEANFILES += README.patched.rst

# produce man pages
syncevolution.1: README.patched.rst
	$(AM_V_GEN)$(RST2MAN) --exit-status=3 $< >$@
CLEANFILES += syncevolution.1

# README is the traditional name in the distribution,
# continue using it instead of README.rst.
# TODO: replace some of the RST syntax
README: README.patched.rst
	$(AM_V_GEN)cp $< $@
CLEANFILES += README

# The README.html is also used on syncevolution.org as "Usage" page,
# therefore we must use <h3> headers and lower to fit into the page.
README.html: README.patched.rst
	$(AM_V_GEN)$(RST2HTML) --initial-header-level=3 --exit-status=3 $< >$@
CLEANFILES += README.html

.PHONY: $(all_phonies) ;

installcheck-local: $(all_local_installchecks) ;

dist-hook: $(all_dist_hooks)

# Force sequential installation. This is a workaround for relinking failures
# during concurrent distcheck (a backed was relinked against not yet installed
# libsyncevolution.la).
install-am: all-am
	@$(MAKE) $(AM_MAKEFLAGS) install-exec-am
	@$(MAKE) $(AM_MAKEFLAGS) install-data-am

.DELETE_ON_ERROR:
