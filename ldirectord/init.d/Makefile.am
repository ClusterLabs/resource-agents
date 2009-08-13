#
# ldirectord: Linux-HA heartbeat code
#
# Copyright (C) 2001 Michael Moerz
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
MAINTAINERCLEANFILES    = Makefile.in

initddir		= @INITDIR@

initd_SCRIPTS	        = ldirectord

install-initdSCRIPTS: $(initd_SCRIPTS)
	@$(NORMAL_INSTALL)
	$(mkinstalldirs) $(DESTDIR)$(initddir)
	@list='$(initd_SCRIPTS)'; for p in $$list; do \
	  f="`echo $$p|sed '$(transform)'`"; \
	  if test -f $$p; then \
	    echo " $(INSTALL_SCRIPT) $$p $(DESTDIR)$(initddir)/$$f@INIT_EXT@"; \
	    $(INSTALL_SCRIPT) $$p $(DESTDIR)$(initddir)/$$f@INIT_EXT@; \
	  elif test -f $(srcdir)/$$p; then \
	    echo " $(INSTALL_SCRIPT) $(srcdir)/$$p $(DESTDIR)$(initddir)/$$f@INIT_EXT@"; \
	    $(INSTALL_SCRIPT) $(srcdir)/$$p $(DESTDIR)$(initddir)/$$f@INIT_EXT@; \
	  else :; fi; \
	done

uninstall-initdSCRIPTS:
	@$(NORMAL_UNINSTALL)
	@list='$(initd_SCRIPTS)'; for p in $$list; do \
	  f="`echo $$p|sed '$(transform)'`"; \
	  echo " rm -f $(DESTDIR)$(initddir)/$$f@INIT_EXT@"; \
	  rm -f $(DESTDIR)$(initddir)/$$f@INIT_EXT@; \
	done

EXTRA_DIST              = $(initd_SCRIPTS) \
			  ldirectord.debian ldirectord.debian.default.in

