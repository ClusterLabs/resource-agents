#!/bin/bash

#
# LVM Failover Script.
# NOTE: Changes to /etc/lvm/lvm.conf are required for proper operation.
#
# Copyright (C) 1997-2003 Sistina Software, Inc.  All rights reserved.
# Copyright (C) 2004-2011 Red Hat, Inc.  All rights reserved.
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

LC_ALL=C
LANG=C
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export LC_ALL LANG PATH

. $(dirname $0)/ocf-shellfuncs
. $(dirname $0)/utils/member_util.sh
. $(dirname $0)/lvm_by_lv.sh
. $(dirname $0)/lvm_by_vg.sh

rv=0

################################################################################
# ha_lvm_proper_setup_check
#
################################################################################
function ha_lvm_proper_setup_check
{
	##
	# Does the Volume Group exist?
	#  1) User may have forgotten to create it
	#  2) User may have misspelled it in the config file
	##
	if ! vgs $OCF_RESKEY_vg_name --config 'global{locking_type=0}'>& /dev/null; then
		ocf_log err "HA LVM: Unable to get volume group attributes for $OCF_RESKEY_vg_name"
		return $OCF_ERR_GENERIC
	fi

	##
	# Are we using the "tagging" or "CLVM" variant?
	#  The CLVM variant will have the cluster attribute set
	##
	if [[ "$(vgs -o attr --noheadings --config 'global{locking_type=0}' $OCF_RESKEY_vg_name 2>/dev/null)" =~ .....c ]]; then
		# Is clvmd running?
		if ! ps -C clvmd >& /dev/null; then
			ocf_log err "HA LVM: $OCF_RESKEY_vg_name has the cluster attribute set, but 'clvmd' is not running"
			return $OCF_ERR_GENERIC
		fi
		return $OCF_SUCCESS
	fi

	##
	# The "tagging" variant is being used if we have gotten this far.
	##

	##
	# The default for lvm.conf:activation/volume_list is empty,
	# this must be changed for HA LVM.
	##
	if ! lvm dumpconfig activation/volume_list >& /dev/null; then
		ocf_log err "HA LVM:  Improper setup detected"
		ocf_log err "* \"volume_list\" not specified in lvm.conf."
		return $OCF_ERR_GENERIC
	fi

	##
	# Machine's cluster node name must be present as
	# a tag in lvm.conf:activation/volume_list
	##
	if ! lvm dumpconfig activation/volume_list | grep $(local_node_name); then
		ocf_log err "HA LVM:  Improper setup detected"
		ocf_log err "* @$(local_node_name) missing from \"volume_list\" in lvm.conf"
		return $OCF_ERR_GENERIC
	fi

	##
	# The volume group to be failed over must NOT be in
	# lvm.conf:activation/volume_list; otherwise, machines
	# will be able to activate the VG regardless of the tags
	##
	if lvm dumpconfig activation/volume_list | grep "\"$OCF_RESKEY_vg_name\""; then
		ocf_log err "HA LVM:  Improper setup detected"
		ocf_log err "* $OCF_RESKEY_vg_name found in \"volume_list\" in lvm.conf"
		return $OCF_ERR_GENERIC
	fi

	##
	# Next, we need to ensure that their initrd has been updated
	# If not, the machine could boot and activate the VG outside
	# the control of rgmanager
	##
	# Fixme: we might be able to perform a better check...
	if [ "$(find /boot -name *.img -newer /etc/lvm/lvm.conf)" == "" ]; then
		ocf_log err "HA LVM:  Improper setup detected"
		ocf_log err "* initrd image needs to be newer than lvm.conf"

		# While dangerous if not done the first time, there are many
		# cases where we don't simply want to fail here.  Instead,
		# keep warning until the user remakes the initrd - or has
		# it done for them by upgrading the kernel.
		#return $OCF_ERR_GENERIC
	fi

	return $OCF_SUCCESS
}

################################################################################
# MAIN
################################################################################

case $1 in
start)
	ha_lvm_proper_setup_check || exit 1

	if [ -z "$OCF_RESKEY_lv_name" ]; then
		vg_start || exit 1
	else
		lv_start || exit 1
	fi
	;;

status|monitor)
	ocf_log notice "Getting status"

	if [ -z "$OCF_RESKEY_lv_name" ]; then
		vg_status
		exit $?
	else
		lv_status
		exit $?
	fi
	;;
		    
stop)
	ha_lvm_proper_setup_check

	if [ -z "$OCF_RESKEY_lv_name" ]; then
		vg_stop || exit 1
	else
		lv_stop || exit 1
	fi
	;;

recover|restart)
	$0 stop || exit $OCF_ERR_GENERIC
	$0 start || exit $OCF_ERR_GENERIC
	;;

meta-data)
	cat `echo $0 | sed 's/^\(.*\)\.sh$/\1.metadata/'`
	;;

validate-all|verify-all)
	if [ -z "$OCF_RESKEY_lv_name" ]; then
		vg_verify || exit 1
	else
		lv_verify || exit 1
	fi
	;;
*)
	echo "usage: $0 {start|status|monitor|stop|restart|meta-data|validate-all}"
	exit $OCF_ERR_UNIMPLEMENTED
	;;
esac

exit $rv
