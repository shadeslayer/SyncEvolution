# TODO: install files properly in distribution
sysconf_DATA = localhost_1/spds/syncml/config.txt localhost_1/spds/sources/addressbook_1/config.txt \
       	       localhost_2/spds/syncml/config.txt localhost_2/spds/sources/addressbook_2/config.txt

localhost_2/spds/syncml/config.txt : localhost_1/spds/syncml/config.txt
	mkdir -p $(@D)
	sed -e s/sc-api-nat/sc-api-nat2/ $< >$@

localhost_2/spds/sources/addressbook_2/config.txt : localhost_1/spds/sources/addressbook_1/config.txt
	mkdir -p $(@D)
	sed -e s/addressbook_1/addressbook_2/ -e s/#1/#2/ $< >$@
