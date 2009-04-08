%.xml: %-full.xml
	$(XSLT) -o $@ $(srcdir)/spec-strip-docs.xsl $<

noinst_DATA = syncevo.xml syncevo-marshal.list

BUILT_SOURCES = $(noinst_DATA)
CLEANFILES = $(BUILT_SOURCES)
EXTRA_DIST = $(BUILT_SOURCES) 
