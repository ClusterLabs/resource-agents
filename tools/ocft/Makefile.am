# Author: John Shi
# jshi@suse.de
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

MAINTAINERCLEANFILES = Makefile.in

EXTRA_DIST		= $(ocftcfgs_DATA) $(ocft_DATA)

sbin_SCRIPTS		= ocft

ocftcfgsdir		= $(datadir)/$(PACKAGE_NAME)/ocft/configs
ocftcfgs_DATA      =  apache  	\
			 IPaddr2	\
			 IPaddr2v4	\
			 IPaddr2v6	\
			 IPv6addr	\
			 Filesystem	\
			 LVM	\
		       	 Raid1	\
			 IPsrcaddr  	\
			 MailTo		\
		       	 mysql		\
		       	 mysql-proxy		\
		       	 pgsql		\
		       	 db2		\
		       	 oracle		\
		       	 drbd.linbit		\
		       	 nfsserver	\
		       	 portblock	\
		       	 iscsi	\
		       	 named	\
		       	 postfix	\
		       	 Xinetd	\
			 SendArp

ocftdir			= $(datadir)/$(PACKAGE_NAME)/ocft
ocft_DATA		= README	\
			  README.zh_CN	\
			  caselib

