# Makefile.am for OCF RAs
#
# Author: Sun Jing Dong
# Copyright (C) 2004 IBM
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
MAINTAINERCLEANFILES = Makefile.in

EXTRA_DIST		= $(ocf_SCRIPTS) $(ocfcommon_DATA) \
			  $(common_DATA) $(hb_DATA) $(dtd_DATA) \
			  README

INCLUDES		= -I$(top_srcdir)/include -I$(top_srcdir)/linux-ha

ocfdir		        = $(OCF_RA_DIR_PREFIX)/heartbeat

dtddir			= $(datadir)/$(PACKAGE_NAME)
dtd_DATA		= ra-api-1.dtd

if USE_IPV6ADDR
ocf_PROGRAMS           = IPv6addr
else
ocf_PROGRAMS           =
endif

IPv6addr_SOURCES        = IPv6addr.c

IPv6addr_LDADD          = -lplumb $(LIBNETLIBS)

ocf_SCRIPTS	     =  ClusterMon		\
			CTDB			\
			Dummy			\
		        IPaddr			\
		        IPaddr2			\
		        drbd			\
			anything		\
			AoEtarget		\
			apache			\
			asterisk		\
			nginx			\
			AudibleAlarm		\
			conntrackd		\
			db2			\
			Delay			\
			eDir88			\
			EvmsSCC			\
			Evmsd			\
			ethmonitor		\
			exportfs		\
			Filesystem		\
			fio			\
			ids			\
			iscsi			\
			ICP			\
			IPsrcaddr		\
			iSCSITarget		\
			iSCSILogicalUnit	\
			jboss			\
			LinuxSCSI		\
			LVM			\
			lxc			\
			MailTo			\
			ManageRAID		\
			ManageVE		\
			mysql			\
			mysql-proxy		\
			named			\
			nfsserver		\
			oracle			\
			oralsnr			\
			pingd			\
			portblock		\
			postfix			\
			pgsql			\
			proftpd			\
			Pure-FTPd		\
			Raid1			\
			Route			\
			rsyncd			\
			rsyslog			\
			SAPDatabase		\
			SAPInstance		\
			SendArp			\
			ServeRAID		\
			slapd			\
			SphinxSearchDaemon	\
			Squid			\
			Stateful		\
			SysInfo			\
			scsi2reservation	\
			sfex			\
			symlink			\
			syslog-ng		\
			tomcat			\
			VIPArip			\
			VirtualDomain		\
			vmware			\
			WAS			\
			WAS6			\
			WinPopup		\
			Xen			\
			Xinetd

ocfcommondir		= $(OCF_LIB_DIR_PREFIX)/heartbeat
ocfcommon_DATA		= ocf-shellfuncs 	\
			  ocf-binaries	 	\
			  ocf-directories 	\
			  ocf-returncodes 	\
			  apache-conf.sh 	\
			  http-mon.sh

# Legacy locations
hbdir			= $(sysconfdir)/ha.d
hb_DATA			= shellfuncs
