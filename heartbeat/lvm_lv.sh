#!/bin/bash

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

function lv_status
{
	local lv_path=$OCF_RESKEY_volgrpname/$OCF_RESKEY_lvname 
	local dev="/dev/$OCF_RESKEY_volgrpname/$OCF_RESKEY_lvname"

	#
	# Check if device is active
	#
	if [[ ! "$(lvs -o attr --noheadings $lv_path)" =~ ....a. ]]; then
		return $OCF_NOT_RUNNING
	fi

	#
	# Check if all links/device nodes are present
	#
	is_dev_present $dev
	return $?
}

function lv_start_normal
{
	local lv_path=$OCF_RESKEY_volgrpname/$OCF_RESKEY_lvname 
	local lvchange_options=$(get_activate_options)
	local res

	ocf_log info "Activating logical volume $lvpath with options '$lvchange_options'"

	if ! ocf_run lvchange $lvchange_options $lv_path; then
		ocf_log err "Failed to activate logical volume, $lv_path"
		return $OCF_ERR_GENERIC
	fi

	lv_status
	res=$?
	if [ $res -ne $OCF_SUCCESS ]; then
		ocf_log err "LVM: $OCF_RESKEY_volgrpname did not activate correctly"
		return $res
	fi

	return $OCF_SUCCESS 
}

function lv_start_exclusive
{
	if lv_start_normal; then
		return $OCF_SUCCESS
	fi

	# FAILED exclusive activation:
	# This can be caused by an LV being active remotely.
	# Before attempting a repair effort, we should attempt
	# to deactivate the LV cluster-wide; but only if the LV
	# is not open.  Otherwise, it is senseless to attempt.
	if ! [[ "$(lvs -o attr --noheadings $OCF_RESKEY_volgrpname/$OCF_RESKEY_lvname)" =~ ....ao ]]; then
		# We'll wait a small amount of time for some settling before
		# attempting to deactivate.  Then the deactivate will be
		# immediately followed by another exclusive activation attempt.
		sleep 5
		if ! lvchange -an $OCF_RESKEY_volgrpname/$OCF_RESKEY_lvname; then
			# Someone could have the device open.
			# We can't do anything about that.
			ocf_log err "Unable to perform required deactivation of $OCF_RESKEY_volgrpname/$OCF_RESKEY_lvname before starting"
			return $OCF_ERR_GENERIC
		fi

		if lv_start_normal; then
			# Second attempt after deactivation was successful, we now
			# have the lock exclusively
			return $OCF_SUCCESS
		fi
	fi

	# Failed to activate:
	# This could be due to a device failure (or another machine could
	# have snuck in between the deactivation/activation).  We don't yet
	# have a mechanism to check for remote activation, so we will proceed
	# with repair action.
	ocf_log err "Failed to activate logical volume, $OCF_RESKEY_volgrpname/$OCF_RESKEY_lvname"
	ocf_log notice "Attempting cleanup of $OCF_RESKEY_volgrpname/$OCF_RESKEY_lvname"

	# This part is only necessary for mirrored logical volumes.
	if ! lvconvert --repair --use-policies $OCF_RESKEY_volgrpname/$OCF_RESKEY_lvname; then
		ocf_log err "Failed to cleanup $OCF_RESKEY_volgrpname/$OCF_RESKEY_lvname"
		return $OCF_ERR_GENERIC
	fi

	if ! lv_start_normal; then
		ocf_log err "Failed second attempt to activate $OCF_RESKEY_volgrpname/$OCF_RESKEY_lvname"
		return $OCF_ERR_GENERIC
	fi

	ocf_log notice "Second attempt to activate $OCF_RESKEY_volgrpname/$OCF_RESKEY_lvname successful"
	return $OCF_SUCCESS
}

function lv_start
{
	# scan for vg, restore transient failed pvs
	prep_for_activation

	get_vg_mode
	case $? in
	0) lv_start_normal;;
	1) return $OCF_ERR_GENERIC;; # validation prevents us from ever getting here.
	2) lv_start_exclusive;;
	esac
}

function lv_stop
{
	local lv_path="$OCF_RESKEY_volgrpname/$OCF_RESKEY_lvname"

	ocf_log info "Deactivating logical volume $lv_path"
	while ! ocf_run lvchange -aln $lv_path; do
		a=$(($a + 1))
		if [ $a -gt 10 ]; then
			break;
		fi
		ocf_log err "Unable to deactivate $OCF_RESKEY_volgrpname, retrying($a)"
		sleep 1
		which udevadm >& /dev/null && udevadm settle
	done

	lv_status
	# make sure lv isn't still running
	if [ $? -eq $OCF_SUCCESS ]; then
		ocf_log err "LVM: $lv_path did not stop correctly."
		return $OCF_ERR_GENERIC 
	fi

	return $OCF_SUCCESS
}
