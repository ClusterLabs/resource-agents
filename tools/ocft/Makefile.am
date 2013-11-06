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

ocftcfgs_DATA      =
if OCFT_FEDORA_CASES
ocftcfgs_DATA      += fedora/Filesystem	\
	fedora/IPaddr2v4	\
	fedora/IPaddr2v6	\
	fedora/IPsrcaddr	\
	fedora/SendArp	\
	fedora/Xinetd	\
	fedora/apache	\
	fedora/mysql	\
	fedora/nfsserver	\
	fedora/pgsql	\
	fedora/tomcat
endif

if OCFT_DEFAULT_CASES
ocftcfgs_DATA      += default/apache	\
	default/IPaddr2	\
	default/IPaddr2v4	\
	default/IPaddr2v6	\
	default/IPv6addr	\
	default/Filesystem	\
	default/LVM	\
	default/Raid1	\
	default/IPsrcaddr	\
	default/MailTo		\
	default/jboss	\
	default/mysql		\
	default/mysql-proxy		\
	default/pgsql		\
	default/db2		\
	default/oracle		\
	default/drbd.linbit		\
	default/exportfs	\
	default/nfsserver	\
	default/portblock	\
	default/iscsi	\
	default/named	\
	default/postfix	\
	default/tomcat	\
	default/Xinetd	\
	default/SendArp
endif

ocftdir			= $(datadir)/$(PACKAGE_NAME)/ocft
ocft_DATA		= README	\
			  README.zh_CN	\
			  caselib

