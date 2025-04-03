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
			  README README.galera

AM_CPPFLAGS		= -I$(top_srcdir)/include -I$(top_srcdir)/linux-ha

halibdir		= $(libexecdir)/heartbeat

ocfdir		        = $(OCF_RA_DIR_PREFIX)/heartbeat

dtddir			= $(datadir)/$(PACKAGE_NAME)
dtd_DATA		= ra-api-1.dtd metadata.rng

ocf_PROGRAMS		=

if USE_IPV6ADDR_AGENT
ocf_PROGRAMS		+= IPv6addr
endif

halib_PROGRAMS		=

if IPV6ADDR_COMPATIBLE
halib_PROGRAMS		+= send_ua
endif

IPv6addr_SOURCES        = IPv6addr.c IPv6addr_utils.c
IPv6addr_LDADD          = -lplumb $(LIBNETLIBS)

send_ua_SOURCES         = send_ua.c IPv6addr_utils.c
send_ua_LDADD           = $(LIBNETLIBS)

ocf_SCRIPTS	      = AoEtarget		\
			AudibleAlarm		\
			ClusterMon		\
			CTDB			\
			Delay			\
			Dummy			\
			EvmsSCC			\
			Evmsd			\
			Filesystem		\
			ICP			\
			IPaddr			\
			IPaddr2			\
			IPsrcaddr		\
			LVM			\
			LinuxSCSI		\
			lvmlockd		\
			LVM-activate		\
			MailTo			\
			ManageRAID		\
			ManageVE		\
			NodeUtilization		\
			Pure-FTPd		\
			Raid1			\
			Route			\
			SAPDatabase		\
			SAPInstance		\
			SendArp			\
			ServeRAID		\
			SphinxSearchDaemon	\
			Squid			\
			Stateful		\
			SysInfo			\
			VIPArip			\
			VirtualDomain		\
			WAS			\
			WAS6			\
			WinPopup		\
			Xen			\
			Xinetd			\
			ZFS			\
			aliyun-vpc-move-ip	\
			anything		\
			apache			\
			asterisk		\
			aws-vpc-move-ip		\
			aws-vpc-route53		\
			awseip			\
			awsvip			\
			azure-lb		\
			clvm			\
			conntrackd		\
			corosync-qnetd		\
			crypt			\
			db2			\
			dhcpd			\
			dnsupdate		\
			dummypy			\
			docker			\
			docker-compose		\
			dovecot			\
			eDir88			\
			ethmonitor		\
			exportfs		\
			fio			\
			galera			\
			garbd			\
			gcp-ilb			\
			gcp-vpc-move-ip		\
			iSCSILogicalUnit	\
			iSCSITarget		\
			ids			\
			iface-bridge		\
			iface-macvlan		\
			iface-vlan		\
			ipsec			\
			iscsi			\
			jboss			\
			jira			\
			kamailio		\
			lxc			\
			lxd-info		\
			machine-info		\
			mariadb			\
			mdraid			\
			minio			\
			mysql			\
			mysql-proxy		\
			nagios			\
			named			\
			nfsnotify		\
			nfsserver		\
			nginx			\
			nvmet-subsystem		\
			nvmet-namespace		\
			nvmet-port		\
			ocivip \
			openstack-cinder-volume \
			openstack-floating-ip   \
			openstack-info          \
			openstack-virtual-ip    \
			oraasm			\
			oracle			\
			oralsnr			\
			osceip			\
			ovsmonitor		\
			pgagent			\
			pgsql			\
			pingd			\
			podman			\
			podman-etcd		\
			portblock		\
			postfix			\
			pound			\
			proftpd			\
			rabbitmq-cluster	\
			rabbitmq-server-ha	\
			redis			\
			rkt			\
			rsyncd			\
			rsyslog			\
			scsi2reservation	\
			sfex			\
			sg_persist              \
			mpathpersist		\
			slapd			\
			smb-share		\
			storage-mon		\
			sybaseASE		\
			symlink			\
			syslog-ng		\
			tomcat			\
			varnish			\
			vdo-vol			\
			vmware			\
			vsftpd			\
			zabbixserver

if BUILD_AZURE_EVENTS
ocf_SCRIPTS	     += azure-events
endif

if BUILD_AZURE_EVENTS_AZ
ocf_SCRIPTS	     += azure-events-az
endif

if BUILD_GCP_PD_MOVE
ocf_SCRIPTS	     += gcp-pd-move
endif

if BUILD_GCP_VPC_MOVE_ROUTE
ocf_SCRIPTS	     += gcp-vpc-move-route
endif

if BUILD_GCP_VPC_MOVE_VIP
ocf_SCRIPTS	     += gcp-vpc-move-vip
endif

if BUILD_POWERVS_SUBNET
ocf_SCRIPTS	     += powervs-subnet
endif

if BUILD_IBM_CLOUD_VPC_MOVE_ROUTE
ocf_SCRIPTS	     += ibm-cloud-vpc-cr-vip
endif

if BUILD_IBM_CLOUD_VPC_MOVE_FIP
ocf_SCRIPTS	     += ibm-cloud-vpc-move-fip
endif

ocfcommondir		= $(OCF_LIB_DIR_PREFIX)/heartbeat
ocfcommon_DATA		= ocf-shellfuncs 	\
			  ocf-binaries	 	\
			  ocf-directories 	\
			  ocf-returncodes 	\
			  ocf-rarun		\
			  ocf-distro		\
			  apache-conf.sh 	\
			  aws.sh		\
			  http-mon.sh		\
			  sapdb-nosha.sh	\
			  sapdb.sh		\
			  lvm-clvm.sh		\
			  lvm-plain.sh		\
			  lvm-tag.sh		\
			  openstack-common.sh	\
			  ora-common.sh		\
			  mysql-common.sh	\
			  nfsserver-redhat.sh	\
			  findif.sh		\
			  ocf.py

# Legacy locations
hbdir			= $(sysconfdir)/ha.d
hb_DATA			= shellfuncs

check: $(ocf_SCRIPTS:=.check)

%.check: %
	OCF_ROOT=$(abs_srcdir) OCF_FUNCTIONS_DIR=$(abs_srcdir) ./$< meta-data | xmllint --path $(abs_srcdir) --noout --relaxng $(abs_srcdir)/metadata.rng -

do_spellcheck = printf '[%s]\n' "$(agent)"; \
                OCF_ROOT=$(abs_srcdir) OCF_FUNCTIONS_DIR=$(abs_srcdir) \
                ./$(agent) meta-data 2>/dev/null \
                | xsltproc $(top_srcdir)/make/extract_text.xsl - \
                | aspell pipe list -d en_US --ignore-case \
                  --home-dir=$(top_srcdir)/make -p spellcheck-ignore \
                | sed -n 's|^&\([^:]*\):.*|\1|p';
spellcheck:
	@$(foreach agent,$(ocf_SCRIPTS), $(do_spellcheck))

clean-local:
	rm -rf __pycache__ *.pyc
