#!/bin/bash

#
# LVM Failover Script.
# NOTE: Changes to /etc/lvm/lvm.conf are required for proper operation.

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
	# The default for lvm.conf:activation/volume_list is empty,
	# this must be changed for HA LVM.
	##
	if ! lvm dumpconfig activation/volume_list >& /dev/null; then
		ocf_log err "HA LVM:  Improper setup detected"
		ocf_log err "- \"volume_list\" not specified in lvm.conf."
		return $OCF_ERR_GENERIC
	fi

	##
	# Machine's cluster node name must be present as
	# a tag in lvm.conf:activation/volume_list
	##
	if ! lvm dumpconfig activation/volume_list | grep $(local_node_name); then
		ocf_log err "HA LVM:  Improper setup detected"
		ocf_log err "- @$(local_node_name) missing from \"volume_list\" in lvm.conf"
		return $OCF_ERR_GENERIC
	fi

	##
	# The volume group to be failed over must NOT be in
	# lvm.conf:activation/volume_list; otherwise, machines
	# will be able to activate the VG regardless of the tags
	##
	if lvm dumpconfig activation/volume_list | grep "\"$OCF_RESKEY_vg_name\""; then
		ocf_log err "HA LVM:  Improper setup detected"
		ocf_log err "- $OCF_RESKEY_vg_name found in \"volume_list\" in lvm.conf"
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
		ocf_log err "- initrd image needs to be newer than lvm.conf"
		return $OCF_ERR_GENERIC
	fi

	return $OCF_SUCCESS
}

################################################################################
# MAIN
################################################################################

case $1 in
start)
	if ! [[ $(vgs -o attr --noheadings $OCF_RESKEY_vg_name) =~ .....c ]]; then
		ha_lvm_proper_setup_check || exit 1
	fi

	if [ -z $OCF_RESKEY_lv_name ]; then
		vg_start || exit 1
	else
		lv_start || exit 1
	fi
	;;

status|monitor)
	ocf_log notice "Getting status"

	if [ -z $OCF_RESKEY_lv_name ]; then
		vg_status || exit 1
	else
		lv_status || exit 1
	fi
	;;
		    
stop)
	if ! [[ $(vgs -o attr --noheadings $OCF_RESKEY_vg_name) =~ .....c ]]; then
		if ! ha_lvm_proper_setup_check; then
			ocf_log err "WARNING: An improper setup can cause data corruption!"
		fi
	fi

	if [ -z $OCF_RESKEY_lv_name ]; then
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
	if [ -z $OCF_RESKEY_lv_name ]; then
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
