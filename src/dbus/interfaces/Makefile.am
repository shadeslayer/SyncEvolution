doc_DATA =

%.xml: %-full.xml
	$(XSLT) -o $@ $(srcdir)/spec-strip-docs.xsl $<

if COND_DOC
%-doc.xml: %-full.xml
	$(XSLT) -o $@ $(srcdir)/spec-to-docbook.xsl $<

syncevo-dbus-api-doc.xml: syncevo-server-doc.xml syncevo-session-doc.xml syncevo-connection-doc.xml
	echo '<?xml version="1.0" encoding="UTF-8"?>' >$@
	echo '<reference><title>SyncEvolution D-Bus API $(VERSION)</title>' >>$@
	for xml in $+; do \
		tail -n +2 $$xml >>$@; \
	done
	echo '</reference>' >>$@

syncevo-dbus-api-doc.html: syncevo-dbus-api-doc.xml
	$(XSLT) -o $@ http://docbook.sourceforge.net/release/xsl/current/html/docbook.xsl $<

doc_DATA += syncevo-dbus-api-doc.html

endif

BUILT_SOURCES = 
if COND_DOC
BUILT_SOURCES +=  syncevo-server-doc.xml syncevo-connection-doc.xml syncevo-session-doc.xml \
	syncevo-dbus-api-doc.xml syncevo-dbus-api-doc.html
endif

CLEANFILES = $(BUILT_SOURCES)
EXTRA_DIST = \
	spec-strip-docs.xsl \
	spec-to-docbook.xsl \
	syncevo-connection-full.xml  \
	syncevo-server-full.xml \
	syncevo-session-full.xml

