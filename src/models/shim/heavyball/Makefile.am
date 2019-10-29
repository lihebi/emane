lib_LTLIBRARIES = libheavyballshim.la

libheavyballshim_la_CPPFLAGS= \
 -I@top_srcdir@/include        \
 $(AM_CPPFLAGS)                \
 $(libemane_CFLAGS)

libheavyballshim_la_LIBADD=                 \
 $(libuuid_LIBS)                             \
 @top_srcdir@/src/libemane/.libs/libemane.la

libheavyballshim_la_LDFLAGS=  \
 $(AM_LDFLAGS)                 \
 -avoid-version

libheavyballshim_la_SOURCES = \
 heavyball.cc                  \
 heavyball.h

EXTRA_DIST =                   \
 heavyballshim.xml.in

BUILT_SOURCES =                \
 heavyballshim.xml

edit = sed                     \
       -e 's|@datadir[@]|$(pkgdatadir)|g'

heavyballshim.xml: heavyballshim.xml.in
	$(edit) $< > $@

clean-local:
	rm -f $(BUILT_SOURCES)

install-exec-hook:
	$(mkinstalldirs) $(DESTDIR)$(datadir)/$(PACKAGE)/xml/models/shim/heavyball
	cp -f heavyballshim.xml $(SCHEMAS) $(DESTDIR)$(datadir)/$(PACKAGE)/xml/models/shim/heavyball

uninstall-local:
	rm -f $(DESTDIR)$(datadir)/$(PACKAGE)/xml/models/shim/heavyball/heavyballshim.xml
