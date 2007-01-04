localhost_source = localhost_1/spds/syncml/config.txt \
                   localhost_1/spds/sources/addressbook_1/config.txt

localhost_copy_syncml = \
		localhost_2/spds/syncml/config.txt
localhost_copy_sources = \
		localhost_2/spds/sources/addressbook_2/config.txt \
                localhost_2/spds/sources/calendar_2/config.txt \
                localhost_2/spds/sources/todo_2/config.txt \
                localhost_1/spds/sources/calendar_1/config.txt \
                localhost_1/spds/sources/todo_1/config.txt

localhost_copy = \
		$(localhost_copy_syncml) \
		$(localhost_copy_sources)
scheduleworld_copy = \
		$(subst localhost,scheduleworld,$(localhost_copy) $(localhost_source)) \
		scheduleworld_1/spds/sources/memo_1/config.txt \
		scheduleworld_2/spds/sources/memo_2/config.txt

all_copy = \
		$(localhost_copy) \
		$(scheduleworld_copy)

EXTRA_DIST = $(localhost_source)
CLEANFILES = $(all_copy)
MAINTAINERCLEANFILES = Makefile.in
nobase_sysconf_DATA = $(localhost_source) $(all_copy)

localhost_2/spds/syncml/config.txt : localhost_1/spds/syncml/config.txt
	mkdir -p $(@D)
	sed -e s/sc-api-nat/sc-pim-ppc/ $< >$@

localhost_2/spds/sources/addressbook_2/config.txt : localhost_1/spds/sources/addressbook_1/config.txt
	mkdir -p $(@D)
	sed -e s/addressbook_1/addressbook_2/ -e s/#1/#2/ $< >$@

localhost_2/spds/sources/calendar_2/config.txt : localhost_1/spds/sources/addressbook_1/config.txt
	mkdir -p $(@D)
	sed -e s/addressbook_1/calendar_2/ -e s/#1/#2/ -e 's:type = text/x-vcard:type = text/calendar:' -e 's/uri = card/uri = cal/' $< >$@

localhost_2/spds/sources/todo_2/config.txt : localhost_1/spds/sources/addressbook_1/config.txt
	mkdir -p $(@D)
	sed -e s/addressbook_1/todo_2/ -e s/#1/#2/ -e 's:type = text/x-vcard:type = text/x-todo:' -e 's/uri = card/uri = todo/' $< >$@

localhost_2/spds/sources/memo_2/config.txt : localhost_1/spds/sources/addressbook_1/config.txt
	mkdir -p $(@D)
	sed -e s/addressbook_1/memo_2/ -e s/#1/#2/ -e 's:type = text/x-vcard:type = text/x-journal:' -e 's/uri = card/uri = note/' $< >$@

localhost_1/spds/sources/calendar_1/config.txt : localhost_1/spds/sources/addressbook_1/config.txt
	mkdir -p $(@D)
	sed -e s/addressbook_1/calendar_1/ -e 's:type = text/x-vcard:type = text/calendar:' -e 's/uri = card/uri = cal/' $< >$@

localhost_1/spds/sources/todo_1/config.txt : localhost_1/spds/sources/addressbook_1/config.txt
	mkdir -p $(@D)
	sed -e s/addressbook_1/todo_1/ -e 's:type = text/x-vcard:type = text/x-todo:' -e 's/uri = card/uri = todo/' $< >$@

localhost_1/spds/sources/memo_1/config.txt : localhost_1/spds/sources/addressbook_1/config.txt
	mkdir -p $(@D)
	sed -e s/addressbook_1/memo_1/ -e 's:type = text/x-vcard:type = text/x-journal:' -e 's/uri = card/uri = note/' $< >$@


# www.scheduleworld.com configuration:
# - different URL
# - RFC compliant types
# - standard sync sources, not the custom "todo" one
$(scheduleworld_copy) : scheduleworld_% : localhost_%
	mkdir -p $(@D)
	sed -e 's;^syncURL = http://localhost;#syncURL = http://localhost;' \
	    -e 's;^#syncURL = http://sync.scheduleworld;syncURL = http://sync.scheduleworld;' \
	    -e 's;^type = text/x-calendar;type = text/calendar;' \
	    -e 's;^type = text/x-vcard;type = text/vcard;' \
	    -e 's;^uri = todo;uri = task2;' \
            -e 's;^uri = card;uri = card3;' \
            -e 's;^uri = cal;uri = cal2;' \
	    $< >$@

