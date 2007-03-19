# the original source files suitable for Funambol running on localhost,
# will be manipulated as necessary for other example configs
localhost_source = localhost_1/spds/syncml/config.txt \
                   localhost_1/spds/sources/addressbook_1/config.txt

# copy for Funambol:
# everything but addressbook disabled
funambol_copy = funambol_2/spds/syncml/config.txt \
		funambol_2/spds/sources/addressbook_2/config.txt \
                funambol_2/spds/sources/calendar_2/config.txt \
                funambol_2/spds/sources/todo_2/config.txt \
		funambol_2/spds/sources/memo_2/config.txt \
                funambol_1/spds/syncml/config.txt \
                funambol_1/spds/sources/addressbook_1/config.txt \
                funambol_1/spds/sources/calendar_1/config.txt \
		funambol_1/spds/sources/memo_1/config.txt \
                funambol_1/spds/sources/todo_1/config.txt

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

# apparently only very recent versions of automake know about $(docdir):
# this is a custom rule to install the etc example files there
install-data-local: $(all_copy)
	set -e; \
	for file in $(all_copy); do \
		$(INSTALL) -d -m 744 "$(DESTDIR)/$(docdir)/`dirname $$file`"; \
		$(INSTALL) -m 744 $$file "$(DESTDIR)/$(docdir)/`dirname $$file`"; \
	done


funambol_1/spds/syncml/config.txt : localhost_1/spds/syncml/config.txt
	mkdir -p $(@D)
	cp $< $@

funambol_1/spds/sources/addressbook_1/config.txt : localhost_1/spds/sources/addressbook_1/config.txt
	mkdir -p $(@D)
	cp $< $@

funambol_2/spds/syncml/config.txt : funambol_1/spds/syncml/config.txt
	mkdir -p $(@D)
	sed -e s/sc-api-nat/sc-pim-ppc/ $< >$@

funambol_2/spds/sources/addressbook_2/config.txt : funambol_1/spds/sources/addressbook_1/config.txt
	mkdir -p $(@D)
	sed -e s/addressbook_1/addressbook_2/ -e s/#1/#2/ $< >$@

funambol_2/spds/sources/calendar_2/config.txt : funambol_1/spds/sources/addressbook_1/config.txt
	mkdir -p $(@D)
	sed -e s/addressbook_1/calendar_2/ -e s/#1/#2/ -e 's:type = text/x-vcard:type = text/calendar:' -e 's/uri = card/uri = cal/' $< >$@

funambol_2/spds/sources/todo_2/config.txt : funambol_1/spds/sources/addressbook_1/config.txt
	mkdir -p $(@D)
	sed -e s/addressbook_1/todo_2/ -e s/#1/#2/ -e 's:type = text/x-vcard:type = text/x-todo:' -e 's/uri = card/uri = todo/' $< >$@

funambol_2/spds/sources/memo_2/config.txt : funambol_1/spds/sources/addressbook_1/config.txt
	mkdir -p $(@D)
	sed -e s/addressbook_1/memo_2/ -e s/#1/#2/ -e 's:type = text/x-vcard:type = text/x-journal:' -e 's/uri = card/uri = note/' -e 's/sync = two-way/sync = none/' $< >$@

funambol_1/spds/sources/calendar_1/config.txt : funambol_1/spds/sources/addressbook_1/config.txt
	mkdir -p $(@D)
	sed -e s/addressbook_1/calendar_1/ -e 's:type = text/x-vcard:type = text/calendar:' -e 's/uri = card/uri = cal/' $< >$@

funambol_1/spds/sources/todo_1/config.txt : funambol_1/spds/sources/addressbook_1/config.txt
	mkdir -p $(@D)
	sed -e s/addressbook_1/todo_1/ -e 's:type = text/x-vcard:type = text/x-todo:' -e 's/uri = card/uri = todo/' $< >$@

funambol_1/spds/sources/memo_1/config.txt : funambol_1/spds/sources/addressbook_1/config.txt
	mkdir -p $(@D)
	sed -e s/addressbook_1/memo_1/ -e 's:type = text/x-vcard:type = text/x-journal:' -e 's/uri = card/uri = note/' -e 's/sync = two-way/sync = none/' $< >$@


# www.scheduleworld.com configuration:
# - different URL
# - RFC compliant types
# - standard sync sources, not the custom "todo" one
$(scheduleworld_copy) : scheduleworld_% : funambol_%
	mkdir -p $(@D)
	sed -e 's;^syncURL = http://localhost;#syncURL = http://localhost;' \
	    -e 's;^#syncURL = http://sync.scheduleworld;syncURL = http://sync.scheduleworld;' \
	    -e 's;^type = text/x-calendar;type = text/calendar;' \
	    -e 's;^type = text/x-vcard;type = text/vcard;' \
	    -e 's;^uri = todo;uri = task2;' \
            -e 's;^uri = card;uri = card3;' \
            -e 's;^uri = cal;uri = cal2;' \
            -e 's;^sync = none;sync = two-way;' \
	    $< >$@

