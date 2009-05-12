AM_CPPFLAGS = -I$(srcdir)/../../core -I$(top_srcdir)/test $(BACKEND_CPPFLAGS)

EXTRA_DIST = configure-sub.in

SYNCSOURCES = syncaddressbook.la
MOSTLYCLEANFILES = $(SYNCSOURCES)
if ENABLE_MODULES
pkglib_LTLIBRARIES = $(SYNCSOURCES)
else
noinst_LTLIBRARIES = $(SYNCSOURCES)
endif

MAINTAINERCLEANFILES = Makefile.in

SYNCADDRESSBOOK_SOURCES = \
	AddressBookSource.h \
	AddressBookConstants.cpp \
	AddressBookSource.cpp

syncaddressbook_la_SOURCES = $(SYNCADDRESSBOOK_SOURCES)
syncaddressbook_la_LIBADD = $(ADDRESSBOOK_LIBS)
syncaddressbook_la_LDFLAGS = -module -rpath '$(pkglibdir)'
syncaddressbook_la_CXXFLAGS = $(SYNCEVOLUTION_CXXFLAGS)
