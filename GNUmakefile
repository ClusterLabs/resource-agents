#
# Copyright (C) 2008 Andrew Beekhof
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

-include Makefile

PACKAGE		?= resource-agents
TARFILE		?= $(PACKAGE).tar.bz2

RPM_ROOT	= $(shell pwd)
RPM_OPTS	= --define "_sourcedir $(RPM_ROOT)" 	\
		  --define "_specdir   $(RPM_ROOT)" 	\
		  --define "_srcrpmdir $(RPM_ROOT)" 	\


getdistro = $(shell test -e /etc/SuSE-release || echo fedora; test -e /etc/SuSE-release && echo suse)
DISTRO ?= $(call getdistro)
TAG    ?= HEAD

gitarchive:
	rm -f $(TARFILE)
	git archive --format=tar --prefix $(PACKAGE)/ $(TAG) | bzip2 > $(TARFILE)
	echo `date`: Rebuilt $(TARFILE)

srpm:	gitarchive
	rm -f *.src.rpm
	@echo To create custom builds, edit the flags and options in $(PACKAGE).spec first
	rpmbuild -bs --define "dist .$(DISTRO)" $(RPM_OPTS) $(PACKAGE).spec

rpm:	srpm
	rpmbuild --rebuild $(RPM_ROOT)/*.src.rpm


