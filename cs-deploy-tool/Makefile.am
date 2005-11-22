AUTOMAKE_OPTIONS = foreign 1.7

SUBDIRS = src po docs

EXTRA_DIST = \
	cs-deploy-tool.spec			\
	cs-deploy-tool.desktop			\
	intltool-extract.in                     \
        intltool-merge.in                       \
        intltool-update.in                      \
	README					\
	ChangeLog

CLEANFILES=$(applications_DATA)
DISTCLEANFILES= intltool-extract intltool-merge intltool-update
MAINTAINERCLEANFILES=ChangeLog

@INTLTOOL_DESKTOP_RULE@

.PHONY: changelog srpm

applicationsdir=$(datadir)/applications
applications_DATA=cs-deploy-tool.desktop
applications_in_files=$(applications_DATA:.desktop=.desktop.in)

install-data-hook:

uninstall-local:

changelog:
	rcs2log | sed -e 's|/usr/local/CVS/redhat-config-network/||g' \
		-e 's|@.*\.redhat\.com|@redhat.com|g' > ChangeLog

clean:
	rm -f cs-deploy-tool.desktop
	rm -f *~

PKGNAME=${PACKAGE}
VERSION=@VERSION@
CVSTAG=r$(subst .,-,$(VERSION))

archive: distcheck changelog
	cvs ci -m "release $(VERSION)"
	cvs tag -cFR $(CVSTAG) .
	@rm -rf /tmp/${PKGNAME}-$(VERSION) /tmp/${PKGNAME}
	@CVSROOT=`cat CVS/Root`; cd /tmp; cvs -d $$CVSROOT export -r$(CVSTAG) ${PKGNAME}
	@cd /tmp/${PKGNAME};./autogen.sh
	@mv /tmp/${PKGNAME} /tmp/${PKGNAME}-$(VERSION)
	@dir=$$PWD; cd /tmp; tar cvzf $$dir/${PKGNAME}-$(VERSION).tar.gz ${PKGNAME}-$(VERSION)
	@chmod u+w -R /tmp/${PKGNAME}-$(VERSION)
#	@rm -rf /tmp/${PKGNAME}-$(VERSION)
	@echo "The archive is in ${PKGNAME}-$(VERSION).tar.gz"

was-srpm: archive
	rpmbuild --define "_sourcedir `pwd`" --define "_srcrpmdir `pwd`" --define "_specdir `pwd`" -ts @PACKAGE@-@VERSION@.tar.gz

srpm: dist
	rpmbuild --define "_sourcedir `pwd`" --define "_srcrpmdir `pwd`" --define "_specdir `pwd`" -ts @PACKAGE@-@VERSION@.tar.gz

srpm-test:
	rpmbuild --define "_sourcedir `pwd`" --define "_srcrpmdir `pwd`" --define "_specdir `pwd`" -ts @PACKAGE@-@VERSION@.tar.gz


