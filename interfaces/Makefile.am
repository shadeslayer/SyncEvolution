%.xml: %-full.xml
	$(XSLT) -o $@ $(srcdir)/spec-strip-docs.xsl $<

BUILT_SOURCES = syncevo.xml

noinst_DATA = syncevo-marshal.list syncevo.xml
CLEANFILES = $(noinst_DATA)
EXTRA_DIST = $(noinst_DATA) 
