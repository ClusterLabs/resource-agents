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

MAINTAINERCLEANFILES    = Makefile.in aclocal.m4 configure DRF/config-h.in \
			  missing install-sh autoscan.log configure.scan \
                          DRF/stamp-h.in libtool.m4 ltdl.m4 libltdl.tar

SUBDIRS =

if BUILD_RGMANAGER
SUBDIRS += rgmanager
endif

if BUILD_LINUX_HA
SUBDIRS	+= include heartbeat tools ldirectord doc
endif

EXTRA_DIST		= autogen.sh .version make/release.mk \
			  make/git-version-gen make/gitlog-to-changelog \
			  AUTHORS COPYING COPYING.GPLv3

install-exec-local:
if BUILD_LINUX_HA
	$(INSTALL) -d -m 1755 $(DESTDIR)$(HA_RSCTMPDIR)
	$(LN_S) ../../lib/heartbeat/ocf-binaries $(DESTDIR)${OCF_RA_DIR}/heartbeat/.ocf-binaries
	$(LN_S) ../../lib/heartbeat/ocf-directories $(DESTDIR)${OCF_RA_DIR}/heartbeat/.ocf-directories
	$(LN_S) ../../lib/heartbeat/ocf-returncodes $(DESTDIR)${OCF_RA_DIR}/heartbeat/.ocf-returncodes
	$(LN_S) ../../lib/heartbeat/ocf-shellfuncs $(DESTDIR)${OCF_RA_DIR}/heartbeat/.ocf-shellfuncs
endif
if BUILD_RGMANAGER
	$(INSTALL) -d $(DESTDIR)/$(LOGDIR)
endif

dist-clean-local:
	rm -f autoconf automake autoheader $(TARFILE)

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
