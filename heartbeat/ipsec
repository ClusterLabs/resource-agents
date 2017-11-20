#!/bin/sh
#
#
#	IPSEC OCF RA. Handles IPSEC tunnels associated with a VIP
#
# Copyright (c) 2017 Red Hat Inc.
#                    All Rights Reserved.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of version 2 of the GNU General Public License as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it would be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
#
# Further, this software is distributed without any warranty that it is
# free of the rightful claim of any third person regarding infringement
# or the like.  Any license provided herein, whether implied or
# otherwise, applies only to this software file.  Patent licenses, if
# any, provided herein do not apply to combinations of this program with
# other software, or any other product whatsoever.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write the Free Software Foundation,
# Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
#

#######################################################################
# Initialization:

: ${OCF_FUNCTIONS_DIR=${OCF_ROOT}/lib/heartbeat}
. ${OCF_FUNCTIONS_DIR}/ocf-shellfuncs

#######################################################################

# Defaults

OCF_RESKEY_confdir_default="/etc/ipsec.d/"
: ${OCF_RESKEY_confdir=${OCF_RESKEY_confdir_default}}

meta_data() {
	cat <<END
<?xml version="1.0"?>
<!DOCTYPE resource-agent SYSTEM "ra-api-1.dtd">
<resource-agent name="ipsec">
<version>1.0</version>

<longdesc lang="en">
This is a Resource Agent to manage IPSEC tunnels associated with a Virtual IP
Address. It's meant to be collocated with a specific VIP, and will manage
setting up or down a specific tunnel.
</longdesc>
<shortdesc lang="en">Handles IPSEC tunnels for VIPs</shortdesc>

<parameters>
<parameter name="tunnel" unique="1" required="1">
<longdesc lang="en">
The name of the tunnel to be monitored.
</longdesc>
<shortdesc lang="en">Tunnel name</shortdesc>
<content type="string" default="" />
</parameter>
<parameter name="vip" unique="1" required="1">
<longdesc lang="en">
Virtual IP address that the tunnel is using.
</longdesc>
<shortdesc lang="en">VIP</shortdesc>
<content type="string" default="" />
</parameter>
<parameter name="confdir">
<longdesc lang="en">
The directory where the IPSEC tunnel configurations can be found.
</longdesc>
<shortdesc lang="en">Tunnel name</shortdesc>
<content type="string" default="${OCF_RESKEY_confdir_default}" />
</parameter>
</parameters>

<actions>
<action name="start"        timeout="20" />
<action name="stop"         timeout="20" />
<action name="monitor"      timeout="20" interval="10" depth="0" />
<action name="reload"       timeout="20" />
<action name="meta-data"    timeout="5" />
</actions>
</resource-agent>
END
}

#######################################################################

ipsec_usage() {
	cat <<END
usage: $0 {start|stop|monitor|validate-all|meta-data}

Expects to have a fully populated OCF RA-compliant environment set. And
should have a collocation constraint with a VIP associated with the
tunnel.
END
}

ipsec_start() {
	ipsec auto --add "${OCF_RESKEY_tunnel}"
	ipsec whack --listen &>> /tmp/ipsec-agent.log
	local return_code=$?
	if [ $return_code -eq  1 -o $return_code -eq 10 ]; then
		ocf_log warn "${OCF_RESOURCE_INSTANCE} : Unable to add tunnel ${OCF_RESKEY_tunnel} with return code ${return_code}"
		return $OCF_ERR_GENERIC
	else
		return $OCF_SUCCESS
	fi
}

ipsec_stop() {
	ipsec auto --down "${OCF_RESKEY_tunnel}"
	local return_code=$?
	ocf_log info "${OCF_RESOURCE_INSTANCE} : Put down tunnel ${OCF_RESKEY_tunnel} with return code ${return_code}"
	return $OCF_SUCCESS
}

ipsec_monitor() {
	# Monitor _MUST!_ differentiate correctly between running
	# (SUCCESS), failed (ERROR) or _cleanly_ stopped (NOT RUNNING).
	# That is THREE states, not just yes/no.

	ipsec status | grep "$OCF_RESKEY_tunnel" | grep -q unoriented
	state=$?
	if [ "$state" == "0" ]; then
		ip addr show | grep -q "${OCF_RESKEY_vip}"
		hosting_vip=$?
		if [ "hosting_vip" == "0" ]; then
			ocf_log warn "${OCF_RESOURCE_INSTANCE} : tunnel ${OCF_RESKEY_tunnel} is unoriented"
			return $OCF_ERR_GENERIC
		else
			return $OCF_NOT_RUNNING
		fi
	else
		return $OCF_SUCCESS
	fi
}

ipsec_validate() {
	# The tunnel needs to be defined in the configuration
	cat ${OCF_RESKEY_confdir}/*.conf | grep -q "conn $OCF_RESKEY_tunnel"
	state=$?
	if [ "$state" == "0" ]; then
		return $OCF_SUCCESS
	else
		return $OCF_ERR_GENERIC
	fi
}

case $__OCF_ACTION in
meta-data)	meta_data
		exit $OCF_SUCCESS
		;;
start)		ipsec_start;;
stop)		ipsec_stop;;
monitor)	ipsec_monitor;;
reload)		ocf_log info "Reloading ${OCF_RESOURCE_INSTANCE} ..."
		;;
usage|help)	ipsec_usage
		exit $OCF_SUCCESS
		;;
*)		ipsec_usage
		exit $OCF_ERR_UNIMPLEMENTED
		;;
esac
rc=$?
ocf_log debug "${OCF_RESOURCE_INSTANCE} $__OCF_ACTION : $rc"
exit $rc
