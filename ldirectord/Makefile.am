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
CLEANFILES	       	= ldirectord.8

EXTRA_DIST              = ldirectord ldirectord.cf README

SUBDIRS			= logrotate.d init.d OCF

ldirectord.8: ldirectord
	$(POD2MAN) --section=8 $< > $@

sbin_SCRIPTS		= ldirectord
man_MANS	        = ldirectord.8

harddir			= $(sysconfdir)/ha.d/resource.d

.PHONY: install-exec-hook
install-exec-hook:
	$(mkinstalldirs) $(DESTDIR)$(harddir)
	cd $(DESTDIR)$(harddir) && $(LN_S) -f $(sbindir)/ldirectord .

.PHONY: uninstall-hook
uninstall-hook:
	rm -f $(DESTDIR)$(harddir)/ldirectord
