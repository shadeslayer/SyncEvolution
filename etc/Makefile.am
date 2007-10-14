# the original source files suitable for Funambol running on localhost,
# will be manipulated as necessary for other example configs
localhost_source = source-config.txt \
                   syncml-config.txt

# copy for Funambol:
# everything but addressbook disabled
funambol_copy = funambol/spds/syncml/config.txt \
                funambol/spds/sources/addressbook/config.txt \
                funambol/spds/sources/calendar/config.txt \
		funambol/spds/sources/memo/config.txt \
                funambol/spds/sources/todo/config.txt

# copy for ScheduleWorld:
# everything enabled
scheduleworld_copy = \
		$(subst funambol,scheduleworld,$(funambol_copy))

all_copy = \
		$(funambol_copy) \
		$(scheduleworld_copy)

EXTRA_DIST = $(localhost_source)
CLEANFILES = $(all_copy)
MAINTAINERCLEANFILES = Makefile.in

all: $(all_copy)

# apparently only very recent versions of automake know about $(docdir):
# this is a custom rule to install the etc example files there
install-data-local: $(all_copy)
	set -e; \
        docdir="$(docdir)"; \
        if [ ! "$$docdir" ]; then docdir="$(datadir)/doc/$(PACKAGE)"; fi; \
	for file in $(all_copy); do \
		$(INSTALL) -d -m 744 "$(DESTDIR)/$$docdir/`dirname $$file`"; \
		$(INSTALL) -m 744 $$file "$(DESTDIR)/$$docdir/`dirname $$file`"; \
	done

uninstall-local:
	docdir="$(docdir)"; \
        if [ ! "$$docdir" ]; then docdir="$(datadir)/doc/$(PACKAGE)"; fi; \
	for file in $(all_copy); do \
		rm "$(DESTDIR)/$$docdir/$$file"; \
	done


funambol/spds/syncml/config.txt : syncml-config.txt
	mkdir -p $(@D)
	cp $< $@

funambol/spds/sources/addressbook/config.txt : source-config.txt
	mkdir -p $(@D)
	cp $< $@

funambol/spds/sources/calendar/config.txt : source-config.txt
	mkdir -p $(@D)
	sed -e s/addressbook/calendar/ -e 's:type = text/x-vcard:type = text/calendar:' -e 's/uri = card/uri = event/' -e 's/sync = two-way/sync = none/' $< >$@

funambol/spds/sources/todo/config.txt : funambol/spds/sources/addressbook/config.txt
	mkdir -p $(@D)
	sed -e s/addressbook/todo/ -e 's:type = text/x-vcard:type = text/x-todo:' -e 's/uri = card/uri = task/' -e 's/sync = two-way/sync = none/' $< >$@

funambol/spds/sources/memo/config.txt : funambol/spds/sources/addressbook/config.txt
	mkdir -p $(@D)
	sed -e s/addressbook/memo/ -e 's:type = text/x-vcard:type = text/plain:' -e 's/uri = card/uri = note/' -e 's/sync = two-way/sync = none/' $< >$@


# www.scheduleworld.com configuration:
# - different URL
# - RFC compliant types
# - standard sync sources, not the custom "todo" one
$(scheduleworld_copy) : scheduleworld% : funambol%
	mkdir -p $(@D)
	sed -e 's;^syncURL = ;#syncURL = ;' \
	    -e 's;^#syncURL = http://sync.scheduleworld;syncURL = http://sync.scheduleworld;' \
	    -e 's;^type = text/x-calendar;type = text/calendar;' \
	    -e 's;^type = text/x-vcard;type = text/vcard;' \
	    -e 's;^uri = task;uri = task2;' \
            -e 's;^uri = card;uri = card3;' \
            -e 's;^uri = event;uri = cal2;' \
            -e 's;^sync = none;sync = two-way;' \
	    $< >$@

