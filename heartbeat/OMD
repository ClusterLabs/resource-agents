#!/bin/sh
#
#	    OMD (Open Monitoring Distribution) OCF RA. 
#             Checks the status of a given OMD site.
# 
#        - Use in a Pacemaker/DRBD environment for OMD -
#      for more information see http://blog.simon-meggle.de
#               Copyright 2011 (C) by Simon Meggle            
# 			 
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

. ${OCF_ROOT}/resource.d/heartbeat/.ocf-shellfuncs

# OMD binary
OMD=`which omd`

# If only your active Node runs OMD sites, change OMDDATA directly to the 
# mount point /mnt/omddata. This ensures that DRBD is mounted before trying 
# to start any clustered OMD site. 
# Otherwise, if your cluster should also run a OMD site on the inactive node to 
# monitor OMD sites on the active node, you need OMDDATA set to /opt/omd. Symlinks 
# within this directory will point to another directory. 
OMDDATA='/opt/omd'
#OMDDATA='/mnt/omddata'

#######################################################################

meta_data() {
	cat <<END
<?xml version="1.0"?>
<!DOCTYPE resource-agent SYSTEM "ra-api-1.dtd">
<resource-agent name="OMD" version="0.1">
<version>0.1</version>

<longdesc lang="en">
This is a Resource Agent for managing OMD (Open Monitoring Distribution, www.omdistro.org) sites.
</longdesc>
<shortdesc lang="en">OMD resource agent</shortdesc>

<parameters>

<parameter name="site" unique="1" required="1">
<longdesc lang="en">
Name of the OMD site to check.
</longdesc>
<shortdesc lang="en">
OMD Site name
</shortdesc>
<content type="string" />
</parameter>

</parameters>

<actions>
<action name="start"        timeout="30" />
<action name="stop"         timeout="30" />
<action name="monitor"      timeout="10" interval="10" depth="0" start-delay="0" />
<action name="reload"       timeout="15" />
<action name="meta-data"    timeout="5" />
<action name="validate-all"   timeout="5" />
</actions>
</resource-agent>
END
}

#######################################################################

# don't exit on TERM, to test that lrmd makes sure that we do exit
trap sigterm_handler TERM
sigterm_handler() {
	ocf_log info "They use TERM to bring us down. No such luck."
	return
}

omd_usage() {
	cat <<END
usage: $0 {start|stop|monitor|validate-all|meta-data}

Expects to have a fully populated OCF RA-compliant environment set.
END
}

omd_start() {
    omd_monitor
    if [ $? =  $OCF_SUCCESS ]; then
	return $OCF_SUCCESS
    fi
    ocf_log info "Starting OMD site ${OCF_RESKEY_site}..."
    $OMD start $OCF_RESKEY_site
}

omd_stop() {
    omd_monitor
    ocf_log info "Stopping OMD site ${OCF_RESKEY_site}..."
    $OMD stop $OCF_RESKEY_site
    return $OCF_SUCCESS
}

omd_monitor() {
# Precondition: check if OMD directories are present (/opt/omd). 
# Otherwise, OMD would never run.

if [ ! -d "${OMDDATA}/apache" ] || [ ! -d "$OMDDATA/sites" ] || [ ! -d "${OMDDATA}/versions" ] ; then
   ocf_log err "${OMDDATA} is not mounted on this node / OMD is not running."
   return ${OCF_NOT_RUNNING}
else
   if [ ! -f ${OMD} ] ; then
      ocf_log err "Cannot find OMD binary ${OMD}!"
	    return ${OCF_ERR_GENERIC}
   else
      STATE=`${OMD} status ${OCF_RESKEY_site} | grep "Overall state"`
      case ${STATE} in 
	"Overall state:  running")  ocf_log debug "OMD site ${OCF_RESKEY_site} is running properly."
				return ${OCF_SUCCESS}
	;;
	"Overall state:  stopped")  ocf_log debug "OMD site ${OCF_RESKEY_site} is stopped."
				return ${OCF_NOT_RUNNING}
	;;
	"Overall state:  partially running") ocf_log err "ERROR: OMD site ${OCF_RESKEY_site} is running only partially!"
				return ${OCF_ERR_GENERIC}
	;;
	*)   ocf_log err "ERROR: State of OMD site ${OCF_RESKEY_site} is unknown!"
				return ${OCF_ERR_GENERIC}
      esac
   fi
fi
}

omd_validate() {
    if [ ! -f ${OMD} ] ; then
       ocf_log err "Cannot find OMD binary ${OMD}!"
       return ${OCF_ERR_GENERIC}
    else
       return $OCF_SUCCESS
    fi
}

: ${OCF_RESKEY_CRM_meta_interval=0}
: ${OCF_RESKEY_CRM_meta_globally_unique:="true"}

case $__OCF_ACTION in
meta-data)	meta_data
		exit $OCF_SUCCESS
		;;
start)		omd_start;;
stop)		omd_stop;;
monitor)	omd_monitor;;
reload)		ocf_log err "Reloading..."
	        omd_start
		;;
validate-all)	omd_validate;;
usage|help)	omd_usage
		exit $OCF_SUCCESS
		;;
*)		omd_usage
		exit $OCF_ERR_UNIMPLEMENTED
		;;
esac
rc=$?
ocf_log debug "${OCF_RESOURCE_INSTANCE} $__OCF_ACTION : $rc"
exit $rc

