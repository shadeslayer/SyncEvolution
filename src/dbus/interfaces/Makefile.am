%.xml: %-full.xml
	$(XSLT) -o $@ $(srcdir)/spec-strip-docs.xsl $<

BUILT_SOURCES = syncevo.xml syncevo-dbus-glue.h syncevo-marshal.c syncevo-marshal.h syncevo-bindings.h

noinst_DATA = syncevo-marshal.list syncevo.xml
CLEANFILES = $(noinst_DATA) $(BUILT_SOURCES)
EXTRA_DIST = $(noinst_DATA) 

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

syncevo-dbus-glue.h: stamp-syncevo-dbus-glue.h
	@true
stamp-syncevo-dbus-glue.h: syncevo.xml
	$(DBUS_BINDING_TOOL) --prefix=syncevo --mode=glib-server $< > xgen-$(@F) \
	&& (cmp -s xgen-$(@F) $(@F:stamp-%=%) || cp xgen-$(@F) $(@F:stamp-%=%)) \
	&& rm -f xgen-$(@F) \
	&& echo timestamp > $(@F)
