EXTRA_DIST		= autogen.sh

AUTOMAKE_OPTIONS	= foreign

MAINTAINERCLEANFILES	= Makefile.in aclocal.m4 configure \
			  missing install-sh

noinst_HEADERS		= make/copyright.cf

SUBDIRS			= rgmanager doc

install-exec-local:
			$(INSTALL) -d $(DESTDIR)/$(LOGDIR)
			$(INSTALL) -d $(DESTDIR)/$(CLUSTERVARRUN)
			$(INSTALL) -d $(DESTDIR)/$(CLUSTERVARLIB)

uninstall-local:
			rmdir $(DESTDIR)/$(LOGDIR) || :;
			rmdir $(DESTDIR)/$(CLUSTERVARRUN) || :;
			rmdir $(DESTDIR)/$(CLUSTERVARLIB) || :;
