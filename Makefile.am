EXTRA_DIST		= autogen.sh .version make/release.mk \
			  make/git-version-gen make/gitlog-to-changelog

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

BUILT_SOURCES = .version
.version:
	echo $(VERSION) > $@-t && mv $@-t $@

dist-hook: gen-ChangeLog
	echo $(VERSION) > $(distdir)/.tarball-version

gen_start_date = 2000-01-01
.PHONY: gen-ChangeLog
gen-ChangeLog:
	if test -d .git; then 						\
		$(top_srcdir)/make/gitlog-to-changelog			\
			--since=$(gen_start_date) > $(distdir)/cl-t;	\
		rm -f $(distdir)/ChangeLog; 				\
		mv $(distdir)/cl-t $(distdir)/ChangeLog;		\
	fi
