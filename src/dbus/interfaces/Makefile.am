%.xml: %-full.xml
	$(XSLT) -o $@ $(srcdir)/spec-strip-docs.xsl $<

%-doc.xml: %-full.xml
	$(XSLT) -o $@ $(srcdir)/spec-to-docbook.xsl $<

BUILT_SOURCES =  syncevo-marshal.c syncevo-marshal.h \
	syncevo-server-bindings.h syncevo-connection-bindings.h syncevo-session-bindings.h \
	syncevo-server-glue.h syncevo-connection-glue.h syncevo-session-glue.h \
	syncevo-server-doc.xml syncevo-connection-doc.xml syncevo-session-doc.xml \
	syncevo-server.xml syncevo-connection.xml syncevo-session.xml

CLEANFILES = $(BUILT_SOURCES) stamp-syncevo-dbus-glue.h
EXTRA_DIST = syncevo-marshal.list \
	spec-strip-docs.xsl \
	spec-to-docbook.xsl \
	syncevo-connection-full.xml  \
	syncevo-server-full.xml \
	syncevo-session-full.xml

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
