#
# doc: Linux-HA resource agents
#
# Copyright (C) 2009 Florian Haas
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

EXTRA_DIST 		= $(doc_DATA) $(REFENTRY_STYLESHEET) \
			  mkappendix.sh ralist.sh

CLEANFILES              = $(man_MANS) $(xmlfiles) metadata-*.xml

STYLESHEET_PREFIX 	?= http://docbook.sourceforge.net/release/xsl/current
MANPAGES_STYLESHEET 	?= $(STYLESHEET_PREFIX)/manpages/docbook.xsl
HTML_STYLESHEET 	?= $(STYLESHEET_PREFIX)/xhtml/docbook.xsl
FO_STYLESHEET 		?= $(STYLESHEET_PREFIX)/fo/docbook.xsl
REFENTRY_STYLESHEET	?= ra2refentry.xsl

XSLTPROC_OPTIONS 	?= --xinclude
XSLTPROC_MANPAGES_OPTIONS ?= $(XSLTPROC_OPTIONS)
XSLTPROC_HTML_OPTIONS 	?= $(XSLTPROC_OPTIONS)
XSLTPROC_FO_OPTIONS 	?= $(XSLTPROC_OPTIONS)

radir			= $(top_srcdir)/heartbeat

# OCF_ROOT=. is necessary due to a sanity check in ocf-shellfuncs
# (which tests whether $OCF_ROOT points to a directory
metadata-%.xml: $(radir)/%
	OCF_ROOT=. OCF_FUNCTIONS_DIR=$(radir) $< meta-data > $@

metadata-IPv6addr.xml: ../../heartbeat/IPv6addr
	OCF_ROOT=. OCF_FUNCTIONS_DIR=$(radir) $< meta-data > $@

# Please note: we can't name the man pages
# ocf:heartbeat:<name>. Believe me, I've tried. It looks like it
# works, but then it doesn't. While make can deal correctly with
# colons in target names (when properly escaped), it royally messes up
# when it is deals with _dependencies_ that contain colons. See Bug
# 12126 on savannah.gnu.org. But, maybe it gets fixed soon, it was
# first reported in 1995 and added to Savannah in in 2005...
if BUILD_DOC
man_MANS	       = ocf_heartbeat_AoEtarget.7 \
                          ocf_heartbeat_AudibleAlarm.7 \
                          ocf_heartbeat_ClusterMon.7 \
                          ocf_heartbeat_CTDB.7 \
                          ocf_heartbeat_Delay.7 \
                          ocf_heartbeat_Dummy.7 \
                          ocf_heartbeat_EvmsSCC.7 \
                          ocf_heartbeat_Evmsd.7 \
                          ocf_heartbeat_Filesystem.7 \
                          ocf_heartbeat_ICP.7 \
                          ocf_heartbeat_IPaddr.7 \
                          ocf_heartbeat_IPaddr2.7 \
                          ocf_heartbeat_IPsrcaddr.7 \
                          ocf_heartbeat_LVM.7 \
                          ocf_heartbeat_LVM-activate.7 \
                          ocf_heartbeat_LinuxSCSI.7 \
                          ocf_heartbeat_MailTo.7 \
                          ocf_heartbeat_ManageRAID.7 \
                          ocf_heartbeat_ManageVE.7 \
                          ocf_heartbeat_NodeUtilization.7 \
                          ocf_heartbeat_Pure-FTPd.7 \
                          ocf_heartbeat_Raid1.7 \
                          ocf_heartbeat_Route.7 \
                          ocf_heartbeat_SAPDatabase.7 \
                          ocf_heartbeat_SAPInstance.7 \
                          ocf_heartbeat_SendArp.7 \
                          ocf_heartbeat_ServeRAID.7 \
                          ocf_heartbeat_SphinxSearchDaemon.7 \
                          ocf_heartbeat_Squid.7 \
                          ocf_heartbeat_Stateful.7 \
                          ocf_heartbeat_SysInfo.7 \
                          ocf_heartbeat_VIPArip.7 \
                          ocf_heartbeat_VirtualDomain.7 \
                          ocf_heartbeat_WAS.7 \
                          ocf_heartbeat_WAS6.7 \
                          ocf_heartbeat_WinPopup.7 \
                          ocf_heartbeat_Xen.7 \
                          ocf_heartbeat_Xinetd.7 \
                          ocf_heartbeat_anything.7 \
                          ocf_heartbeat_apache.7 \
                          ocf_heartbeat_asterisk.7 \
                          ocf_heartbeat_aws-vpc-move-ip.7 \
                          ocf_heartbeat_aws-vpc-route53.7 \
                          ocf_heartbeat_awseip.7 \
                          ocf_heartbeat_awsvip.7 \
                          ocf_heartbeat_azure-lb.7 \
                          ocf_heartbeat_clvm.7 \
                          ocf_heartbeat_conntrackd.7 \
                          ocf_heartbeat_db2.7 \
                          ocf_heartbeat_dhcpd.7 \
                          ocf_heartbeat_docker.7 \
                          ocf_heartbeat_dnsupdate.7 \
                          ocf_heartbeat_eDir88.7 \
                          ocf_heartbeat_ethmonitor.7 \
                          ocf_heartbeat_exportfs.7 \
                          ocf_heartbeat_fio.7 \
                          ocf_heartbeat_galera.7 \
                          ocf_heartbeat_garbd.7 \
                          ocf_heartbeat_iSCSILogicalUnit.7 \
                          ocf_heartbeat_iSCSITarget.7 \
                          ocf_heartbeat_iface-bridge.7 \
                          ocf_heartbeat_iface-vlan.7 \
                          ocf_heartbeat_ipsec.7 \
                          ocf_heartbeat_ids.7 \
                          ocf_heartbeat_iscsi.7 \
                          ocf_heartbeat_jboss.7 \
                          ocf_heartbeat_jira.7 \
                          ocf_heartbeat_kamailio.7 \
                          ocf_heartbeat_lvmlockd.7 \
                          ocf_heartbeat_lxc.7 \
                          ocf_heartbeat_lxd-info.7 \
                          ocf_heartbeat_machine-info.7 \
                          ocf_heartbeat_mysql.7 \
                          ocf_heartbeat_mysql-proxy.7 \
                          ocf_heartbeat_nagios.7 \
                          ocf_heartbeat_named.7 \
                          ocf_heartbeat_nfsnotify.7 \
                          ocf_heartbeat_nfsserver.7 \
                          ocf_heartbeat_nginx.7 \
                          ocf_heartbeat_oraasm.7 \
                          ocf_heartbeat_oracle.7 \
                          ocf_heartbeat_oralsnr.7 \
                          ocf_heartbeat_pgsql.7 \
                          ocf_heartbeat_pingd.7 \
                          ocf_heartbeat_portblock.7 \
                          ocf_heartbeat_postfix.7 \
                          ocf_heartbeat_pound.7 \
                          ocf_heartbeat_proftpd.7 \
                          ocf_heartbeat_rabbitmq-cluster.7 \
                          ocf_heartbeat_redis.7 \
                          ocf_heartbeat_rsyncd.7 \
                          ocf_heartbeat_rsyslog.7 \
                          ocf_heartbeat_scsi2reservation.7 \
                          ocf_heartbeat_sfex.7 \
                          ocf_heartbeat_slapd.7 \
                          ocf_heartbeat_sg_persist.7 \
			  ocf_heartbeat_mpathpersist.7 \
                          ocf_heartbeat_symlink.7 \
                          ocf_heartbeat_syslog-ng.7 \
                          ocf_heartbeat_tomcat.7 \
                          ocf_heartbeat_varnish.7 \
                          ocf_heartbeat_vmware.7 \
                          ocf_heartbeat_zabbixserver.7

if USE_IPV6ADDR_AGENT
man_MANS           	+= ocf_heartbeat_IPv6addr.7
endif

xmlfiles		= $(man_MANS:.7=.xml)

%.1 %.5 %.7 %.8: %.xml
	$(XSLTPROC) \
	$(XSLTPROC_MANPAGES_OPTIONS) \
	$(MANPAGES_STYLESHEET) $<

ocf_heartbeat_%.xml: metadata-%.xml $(srcdir)/$(REFENTRY_STYLESHEET)
	$(XSLTPROC) --novalid \
	--stringparam package $(PACKAGE_NAME) \
	--stringparam version $(VERSION) \
	--output $@ \
	$(srcdir)/$(REFENTRY_STYLESHEET) $<

ocf_resource_agents.xml: $(xmlfiles) mkappendix.sh
	./mkappendix.sh $(xmlfiles) > $@

%.html: %.xml
	$(XSLTPROC) \
	$(XSLTPROC_HTML_OPTIONS) \
	--output $@ \
	$(HTML_STYLESHEET) $<

xml: ocf_resource_agents.xml
endif
