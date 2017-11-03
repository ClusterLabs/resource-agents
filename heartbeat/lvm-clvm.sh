# lvm-clvmd.sh
#
# Description: LVM management with clvmd
#
#
# Author:      Resource agents contributors
#              Interface to LVM by Dejan Muhamedagic
# Support:     users@clusterlabs.org
# License:     GNU General Public License (GPL)
# Copyright:   (C) 2017 Dejan Muhamedagic
#

##
# Attempt to deactivate vg cluster wide and then start the vg exclusively
##
retry_exclusive_start()
{
	# Deactivate each LV in the group one by one cluster wide
	set -- $(lvs -o name,attr --noheadings $OCF_RESKEY_volgrpname 2> /dev/null)
	while [ $# -ge 2 ]; do
		case $2 in
		????ao*)
			# open LVs cannot be deactivated.
			return $OCF_ERR_GENERIC;;
		*)
			if ! lvchange -an $OCF_RESKEY_volgrpname/$1; then
				ocf_exit_reason "Unable to perform required deactivation of $OCF_RESKEY_volgrpname/$1 before starting"
				return $OCF_ERR_GENERIC
			fi
			;;
		esac
		shift 2
	done

	ocf_run vgchange $vgchange_activate_options $OCF_RESKEY_volgrpname
}

#
# the interface to the LVM RA
#

lvm_init() {
	vgchange_activate_options="-aey"
	vgchange_deactivate_options="-an"
}

lvm_validate_all() {
	if ! ps -C clvmd > /dev/null 2>&1; then
		ocf_exit_reason "$OCF_RESKEY_volgrpname has the cluster attribute set, but 'clvmd' is not running"
		exit $OCF_ERR_GENERIC
	fi
}

lvm_status() {
	return 0
}

lvm_pre_activate() {
	return 0
}

lvm_post_activate() {
	local rc=$1
	if [ $rc -ne 0 ]; then
		# Failure to exclusively activate cluster vg.:
		# This could be caused by a remotely active LV, Attempt
		# to disable volume group cluster wide and try again.
		# Allow for some settling
		sleep 5
		if ! retry_exclusive_start; then
			return $OCF_ERR_GENERIC
		fi
	fi
	return $rc
}

lvm_pre_deactivate() {
	return 0
}

lvm_post_deactivate() {
	local rc=$1
	return $rc
}

# vim:tabstop=4:shiftwidth=4:textwidth=0:wrapmargin=0
