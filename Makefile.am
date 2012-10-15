#
# Copyright (C) 2008 Andrew Beekhof
# Copyright (C) 2011 Fabio M. Di Nitto
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
#

MAINTAINERCLEANFILES	= Makefile.in aclocal.m4 configure DRF/config-h.in \
			  missing install-sh autoscan.log configure.scan \
			  DRF/stamp-h.in libtool.m4 ltdl.m4 libltdl.tar \
			  compile config.guess config.sub depcomp

SPEC			= $(PACKAGE_NAME).spec

TARFILES		= $(PACKAGE_NAME)-$(VERSION).tar.bz2 \
			  $(PACKAGE_NAME)-$(VERSION).tar.gz

SUBDIRS =

if BUILD_RGMANAGER
SUBDIRS += rgmanager
RGMANAGER = without
else
RGMANAGER = with
endif

if BUILD_LINUX_HA
SUBDIRS	+= include heartbeat tools ldirectord doc
LINUX_HA = without
else
LINUX_HA = with
endif

EXTRA_DIST		= autogen.sh .version make/release.mk \
			  make/git-version-gen make/gitlog-to-changelog \
			  AUTHORS COPYING COPYING.GPLv3 ChangeLog \
			  $(SPEC).in

install-exec-local:
if BUILD_LINUX_HA
	$(INSTALL) -d -m 1755 $(DESTDIR)$(HA_RSCTMPDIR)
	$(LN_S) ../../lib/heartbeat/ocf-binaries $(DESTDIR)${OCF_RA_DIR_PREFIX}/heartbeat/.ocf-binaries
	$(LN_S) ../../lib/heartbeat/ocf-directories $(DESTDIR)${OCF_RA_DIR_PREFIX}/heartbeat/.ocf-directories
	$(LN_S) ../../lib/heartbeat/ocf-returncodes $(DESTDIR)${OCF_RA_DIR_PREFIX}/heartbeat/.ocf-returncodes
	$(LN_S) ../../lib/heartbeat/ocf-shellfuncs $(DESTDIR)${OCF_RA_DIR_PREFIX}/heartbeat/.ocf-shellfuncs
endif
if BUILD_RGMANAGER
if BUILD_LINUX_HA
	$(LN_S) ${CLUSTERDATA} $(DESTDIR)${OCF_RA_DIR_PREFIX}/redhat
endif
	$(INSTALL) -d $(DESTDIR)/$(LOGDIR)
endif

dist-clean-local:
	rm -f autoconf automake autoheader $(TARFILES)

uninstall-local:
			rmdir $(DESTDIR)/$(LOGDIR) || :;
if BUILD_LINUX_HA
	rm -f $(DESTDIR)${OCF_RA_DIR_PREFIX}/heartbeat/.ocf-binaries
	rm -f $(DESTDIR)${OCF_RA_DIR_PREFIX}/heartbeat/.ocf-directories
	rm -f $(DESTDIR)${OCF_RA_DIR_PREFIX}/heartbeat/.ocf-returncodes
	rm -f $(DESTDIR)${OCF_RA_DIR_PREFIX}/heartbeat/.ocf-shellfuncs
if BUILD_RGMANAGER
	rm -f $(DESTDIR)${OCF_RA_DIR_PREFIX}/redhat
endif
endif

BUILT_SOURCES = .version
.version:
	echo $(VERSION) > $@-t && mv $@-t $@

dist-hook: gen-ChangeLog $(SPEC)
	echo $(VERSION) > $(distdir)/.tarball-version
	rm -f $(distdir)/$(SPEC) && \
		cp $(top_srcdir)/$(SPEC) $(distdir)/$(SPEC)

gen_start_date = 2000-01-01
.PHONY: gen-ChangeLog
gen-ChangeLog:
	if test -d .git; then 						\
		LC_ALL=C $(top_srcdir)/make/gitlog-to-changelog		\
			--since=$(gen_start_date) > $(distdir)/cl-t;	\
		rm -f $(distdir)/ChangeLog.devel;			\
		mv $(distdir)/cl-t $(distdir)/ChangeLog.devel;		\
	fi

## make rpm/srpm section.
$(SPEC): $(SPEC).in
	rm -f $@-t $@
	date="$(shell LC_ALL=C date "+%a %b %d %Y")" && \
	specver="1" && \
	rcver="" && \
	dirty="" && \
	alphatag="" && \
	numcomm="" && \
	ver="$(VERSION)" && \
	if echo $$ver | grep -q -- "-dirty$$"; then \
		dirty="dirty" && \
		echo VERSION IS DIRTY && \
		ver=`echo $$ver | sed -e "s/-dirty$$//"`; \
	fi && \
	echo $$ver && \
	if echo $$ver | grep -q -- "-"; then \
		alphatag=`echo $$ver | sed -e "s/.*-//"` && \
		echo VERSION HAS ALPHATAG && \
		ver=`echo $$ver | sed -e "s/-.*//"`; \
	fi && \
	echo $$ver && \
	if [ -n "$$alphatag" ]; then \
		echo VERSION HAS NUMCOMMITS && \
		numcomm=`echo $$ver | sed -e 's/.*\.//'` && \
		ver=`echo $$ver | sed -e 's/.'$$numcomm'$$//'`; \
	fi && \
	if echo $$ver | grep -q "\.[[:digit:]]rc[[:digit:]]"; then \
		rpmver=`echo $$ver | sed -e "s/rc[[:digit:]].*//g"` && \
		rcver=`echo $$ver | sed -e "s/.*\(rc[[:digit:]]\).*/\1/g"` && \
		specver="0"; \
	else \
		rpmver=$$ver; \
	fi && \
	echo $$rpmver $$rcver && \
	sed \
		-e "s#@version@#$$rpmver#g" \
		-e "s#@alphatag@#$$alphatag#g" \
		-e "s#@numcomm@#$$numcomm#g" \
		-e "s#@dirty@#$$dirty#g" \
		-e "s#@date@#$$date#g" \
		-e "s#@specver@#$$specver#g" \
		-e "s#@rcver@#$$rcver#g" \
		-e "s#@rgmanager@#$(RGMANAGER)#g" \
		-e "s#@linux-ha@#$(LINUX_HA)#g" \
	$< > $@-t; \
	if [ -z "$$dirty" ]; then sed -i -e "s#%glo.*dirty.*##g" $@-t; fi; \
	if [ -z "$$alphatag" ]; then sed -i -e "s#%glo.*alphatag.*##g" $@-t; fi; \
	if [ -z "$$numcomm" ]; then sed -i -e "s#%glo.*numcomm.*##g" $@-t; fi; \
	if [ -z "$$rcver" ]; then sed -i -e "s#%glo.*rcver.*##g" $@-t; fi
	chmod a-w $@-t
	mv $@-t $@

$(TARFILES):
	$(MAKE) dist

RPMBUILDOPTS    = --define "_sourcedir $(abs_builddir)" \
		  --define "_specdir $(abs_builddir)" \
		  --define "_builddir $(abs_builddir)" \
		  --define "_srcrpmdir $(abs_builddir)" \
		  --define "_rpmdir $(abs_builddir)"

srpm: clean
	$(MAKE) $(SPEC) $(TARFILES)
	rpmbuild $(RPMBUILDOPTS) --nodeps -bs $(SPEC)

rpm: clean
	$(MAKE) $(SPEC) $(TARFILES)
	rpmbuild $(RPMBUILDOPTS) -ba $(SPEC)

clean-generic:
	rm -rf $(SPEC) $(TARFILES)
