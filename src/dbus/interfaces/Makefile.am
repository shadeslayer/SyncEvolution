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
if COND_GUI
BUILT_SOURCES +=  syncevo-marshal.c syncevo-marshal.h \
	syncevo-server-bindings.h syncevo-connection-bindings.h syncevo-session-bindings.h \
	syncevo-server-glue.h syncevo-connection-glue.h syncevo-session-glue.h \
	syncevo-server.xml syncevo-connection.xml syncevo-session.xml
endif
if COND_DOC
BUILT_SOURCES +=  syncevo-server-doc.xml syncevo-connection-doc.xml syncevo-session-doc.xml \
	syncevo-dbus-api-doc.xml syncevo-dbus-api-doc.html
endif

CLEANFILES = $(BUILT_SOURCES) stamp-syncevo-dbus-glue.h
EXTRA_DIST = syncevo-marshal.list \
	spec-strip-docs.xsl \
	spec-to-docbook.xsl \
	syncevo-connection-full.xml  \
	syncevo-server-full.xml \
	syncevo-session-full.xml

if COND_GUI
%-bindings.h: stamp-%-bindings.h
	@true
stamp-%-bindings.h: %.xml
	$(DBUS_BINDING_TOOL) --mode=glib-client --prefix=syncevo $< > xgen-$(@F) \
	&& (cmp -s xgen-$(@F) $(@F:stamp-%=%) || cp xgen-$(@F) $(@F:stamp-%=%)) \
	&& rm -f xgen-$(@F) \
	&& echo timestamp > $(@F)

syncevo-marshal.h: $(srcdir)/syncevo-marshal.list $(GLIB_GENMARSHAL)
	$(GLIB_GENMARSHAL) $< --header --prefix=syncevo_marshal > $@
syncevo-marshal.c: $(srcdir)/syncevo-marshal.list syncevo-marshal.h $(GLIB_GENMARSHAL)
	echo "#include \"syncevo-marshal.h\"" > $@ \
	&& $(GLIB_GENMARSHAL) --prefix=syncevo_marshal $(srcdir)/syncevo-marshal.list --body >> $@

%-glue.h: stamp-%-glue.h
	@true
stamp-%-glue.h: %.xml
	$(DBUS_BINDING_TOOL) --prefix=syncevo --mode=glib-server $< > xgen-$(@F) \
	&& (cmp -s xgen-$(@F) $(@F:stamp-%=%) || cp xgen-$(@F) $(@F:stamp-%=%)) \
	&& rm -f xgen-$(@F) \
	&& echo timestamp > $(@F)
endif
