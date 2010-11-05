EXTRA_DIST		= autogen.sh

AUTOMAKE_OPTIONS	= foreign

MAINTAINERCLEANFILES	= Makefile.in aclocal.m4 configure \
			  missing install-sh autoscan.log \
			  configure.scan

noinst_HEADERS		= make/copyright.cf

SUBDIRS			= rgmanager doc

install-exec-local:
			$(INSTALL) -d $(DESTDIR)/$(LOGDIR)

uninstall-local:
			rmdir $(DESTDIR)/$(LOGDIR) || :;
