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


##
# All volume group variants call into this function for status.
# Some variants (tags) call this function and proceed with some
# other tests as well.
##
function vg_status_normal
{
	local i
	local dev
	local res=$OCF_NOT_RUNNING

	#
	# Check that all LVs are active
	#
	for i in `lvs $OCF_RESKEY_volgrpname --noheadings -o attr`; do
		if [[ ! $i =~ ....a. ]]; then
			return $OCF_NOT_RUNNING
		fi
	done

	#
	# Check if all links/device nodes are present
	#
	for i in `lvs $OCF_RESKEY_volgrpname --noheadings -o name`; do
		dev="/dev/$OCF_RESKEY_volgrpname/$i"

		is_dev_present $dev
		res=$?
		if [ $res -ne $OCF_SUCCESS ]; then
			# err log happens in is_dev_present
			return $res
		fi
	done

	return $res
}

function vg_status_tagged
{
	local my_name=$(local_node_name)
	local res

	vg_status_normal
	res=$?
	if [ $res -ne $OCF_SUCCESS ]; then
		# errors are logged in vg_status_normal
		return $res
	fi

	#
	# Verify that we are the correct owner
	#
	vg_tag_owner
	if [ $? -ne 1 ]; then
		ocf_log err "WARNING: $OCF_RESKEY_volgrpname should not be active"
		ocf_log err "WARNING: $my_name does not own $OCF_RESKEY_volgrpname"
		ocf_log err "WARNING: Attempting shutdown of $OCF_RESKEY_volgrpname"

		vgchange -an $OCF_RESKEY_volgrpname
		return $OCF_ERR_GENERIC
	fi

	return $OCF_SUCCESS
}

##
# Main status function for volume groups
##
function vg_status
{
	get_vg_mode
	case $? in
	0) vg_status_normal;;
	1) vg_status_tagged;; # slighty different because we verify tag ownership
	2) vg_status_normal;; # cluster exclusive is same as normal status.
	esac

	return $?
}

function vg_start_normal
{
	local vgchange_options=$(get_activate_options)
	local res

	ocf_log info "Activating volume group $OCF_RESKEY_volgrpname with options '$vgchange_options'"

	# for clones (clustered volume groups), we'll also have to force
	# monitoring, even if disabled in lvm.conf.
	if ocf_is_clone; then
		vgchange_options="$vgchange_options --monitor y"
	fi

	if ! ocf_run vgchange $vgchange_options $OCF_RESKEY_volgrpname; then
		ocf_log err "Failed to activate volume group, $OCF_RESKEY_volgrpname"
		return $OCF_ERR_GENERIC
	fi

	vg_status_normal
	res=$?
	if [ $res -ne $OCF_SUCCESS ]; then
		ocf_log err "LVM: $OCF_RESKEY_volgrpname did not activate correctly"
		return $res
	fi

	# OK Volume $OCF_RESKEY_volgrpname activated just fine!
	return $OCF_SUCCESS 
}

function vg_start_exclusive
{
	local vgchange_options=$(get_activate_options)
	local a
	local results
	local all_pvs
	local resilience
	local try_again=false

	ocf_log info "Starting volume group, $OCF_RESKEY_volgrpname, exclusively with options '$vgchange_options'"

	if ! vg_start_normal; then
		try_again=true

		# Failure to activate:
		# This could be caused by a remotely active LV.  Before
		# attempting any repair of the VG, we will first attempt
		# to deactivate the VG cluster-wide.
		# We must check for open LVs though, since these cannot
		# be deactivated.  We have no choice but to go one-by-one.

		# Allow for some settling
		sleep 5

		results=(`lvs -o name,attr --noheadings $OCF_RESKEY_volgrpname 2> /dev/null`)
		a=0
		while [ ! -z "${results[$a]}" ]; do
			if [[ ! ${results[$(($a + 1))]} =~ ....ao ]]; then
				if ! lvchange -an $OCF_RESKEY_volgrpname/${results[$a]}; then
					ocf_log err "Unable to perform required deactivation of $OCF_RESKEY_volgrpname before starting"
					return $OCF_ERR_GENERIC
				fi
			fi
			a=$(($a + 2))
		done
	fi

	if $try_again && ! vgchange $vgchange_options $OCF_RESKEY_volgrpname; then
		ocf_log err "Failed to activate volume group, $OCF_RESKEY_volgrpname"
		ocf_log notice "Attempting cleanup of $OCF_RESKEY_volgrpname"

		if ! vgreduce --removemissing --force $OCF_RESKEY_volgrpname; then
			ocf_log err "Failed to make $OCF_RESKEY_volgrpname consistent"
			return $OCF_ERR_GENERIC
		fi

		if ! vgchange $vgchange_options $OCF_RESKEY_volgrpname; then
			ocf_log err "Failed second attempt to activate $OCF_RESKEY_volgrpname"
			return $OCF_ERR_GENERIC
		fi

		ocf_log notice "Second attempt to activate $OCF_RESKEY_volgrpname successful"
		return $OCF_SUCCESS
	else
		# The activation commands succeeded, but did they do anything?
		# Make sure all the logical volumes are active
		results=(`lvs -o name,attr --noheadings 2> /dev/null $OCF_RESKEY_volgrpname`)
		a=0
		while [ ! -z "${results[$a]}" ]; do
			if [[ ! ${results[$(($a + 1))]} =~ ....a. ]]; then
				all_pvs=(`pvs --noheadings -o name 2> /dev/null`)
				resilience=" --config devices{filter=["
				for i in ${all_pvs[*]}; do
					resilience=$resilience'"a|'$i'|",'
				done
				resilience=$resilience"\"r|.*|\"]}"

				vgchange $vgchange_options $OCF_RESKEY_volgrpname $resilience
				break
			fi
			a=$(($a + 2))
		done

		#  We need to check the LVs again if we made the command resilient
		if [ ! -z "$resilience" ]; then
			results=(`lvs -o name,attr --noheadings $OCF_RESKEY_volgrpname $resilience 2> /dev/null`)
			a=0
			while [ ! -z ${results[$a]} ]; do
				if [[ ! ${results[$(($a + 1))]} =~ ....a. ]]; then
					ocf_log err "Failed to activate $OCF_RESKEY_volgrpname"
					return $OCF_ERR_GENERIC
				fi
				a=$(($a + 2))
			done
			ocf_log err "Orphan storage device in $OCF_RESKEY_volgrpname slowing operations"
		fi
	fi

	return $OCF_SUCCESS
}

function vg_start_tagged
{
	local a
	local results
	local all_pvs
	local resilience
	local vgchange_options=$(get_activate_options)

	ocf_log info "Starting volume group, $OCF_RESKEY_volgrpname, exclusively using tags with the options '$vgchange_options'"

	vg_tag_owner
	case $? in
	0)
		ocf_log info "Someone else owns this volume group"
		return $OCF_ERR_GENERIC
		;;
	1)
		ocf_log info "I own this volume group"
		;;
	2)
		ocf_log info "I can claim this volume group"
		;;
	esac

	if ! strip_and_add_tag; then
		# Errors printed by sub-function
		return $OCF_ERR_GENERIC
	fi

	if ! vgchange $vgchange_options $OCF_RESKEY_volgrpname; then
		ocf_log err "Failed to activate volume group, $OCF_RESKEY_volgrpname"
		ocf_log err "Attempting activation of logical volumes one-by-one."

		results=(`lvs -o name,attr --noheadings $OCF_RESKEY_volgrpname 2> /dev/null`)
		a=0
		while [ ! -z ${results[$a]} ]; do
			if [[ ${results[$(($a + 1))]} =~ r.......p ]] ||
				[[ ${results[$(($a + 1))]} =~ R.......p ]]; then
				# Attempt "partial" activation of any RAID LVs
				ocf_log err "Attempting partial activation of ${OCF_RESKEY_volgrpname}/${results[$a]}"
				if ! lvchange -ay --partial ${OCF_RESKEY_volgrpname}/${results[$a]}; then
					ocf_log err "Failed attempt to activate ${OCF_RESKEY_volgrpname}/${results[$a]} in partial mode"
					return $OCF_ERR_GENERIC
				fi
				ocf_log notice "Activation of ${OCF_RESKEY_volgrpname}/${results[$a]} in partial mode succeeded"
			elif [[ ${results[$(($a + 1))]} =~ m.......p ]] ||
				[[ ${results[$(($a + 1))]} =~ M.......p ]]; then
				ocf_log err "Attempting repair and activation of ${OCF_RESKEY_volgrpname}/${results[$a]}"
				if ! lvconvert --repair --use-policies ${OCF_RESKEY_volgrpname}/${results[$a]}; then
					ocf_log err "Failed to repair ${OCF_RESKEY_volgrpname}/${results[$a]}"
					return $OCF_ERR_GENERIC
				fi
				if ! lvchange -ay ${OCF_RESKEY_volgrpname}/${results[$a]}; then
					ocf_log err "Failed to activate ${OCF_RESKEY_volgrpname}/${results[$a]}"
					return $OCF_ERR_GENERIC
				fi
				ocf_log notice "Repair and activation of ${OCF_RESKEY_volgrpname}/${results[$a]} succeeded"
			else
				ocf_log err "Attempting activation of non-redundant LV ${OCF_RESKEY_volgrpname}/${results[$a]}"
				if ! lvchange -ay ${OCF_RESKEY_volgrpname}/${results[$a]}; then
					ocf_log err "Failed to activate ${OCF_RESKEY_volgrpname}/${results[$a]}"
					return $OCF_ERR_GENERIC
				fi
				ocf_log notice "Successfully activated non-redundant LV ${OCF_RESKEY_volgrpname}/${results[$a]}"
			fi
			a=$(($a + 2))
		done

		return $OCF_SUCCESS
	else
		# The activation commands succeeded, but did they do anything?
		# Make sure all the logical volumes are active
		results=(`lvs -o name,attr --noheadings $OCF_RESKEY_volgrpname 2> /dev/null`)
		a=0
		while [ ! -z ${results[$a]} ]; do
			if [[ ! ${results[$(($a + 1))]} =~ ....a. ]]; then
				all_pvs=(`pvs --noheadings -o name 2> /dev/null`)
				resilience=" --config devices{filter=["
				for i in ${all_pvs[*]}; do
					resilience=$resilience'"a|'$i'|",'
				done
				resilience=$resilience"\"r|.*|\"]}"

				vgchange $vgchange_options $OCF_RESKEY_volgrpname $resilience
				break
			fi
			a=$(($a + 2))
		done

		#  We need to check the LVs again if we made the command resilient
		if [ ! -z "$resilience" ]; then
			results=(`lvs -o name,attr --noheadings $OCF_RESKEY_volgrpname $resilience 2> /dev/null`)
			a=0
			while [ ! -z ${results[$a]} ]; do
				if [[ ! ${results[$(($a + 1))]} =~ ....a. ]]; then
					ocf_log err "Failed to activate $OCF_RESKEY_volgrpname"
					return $OCF_ERR_GENERIC
				fi
				a=$(($a + 2))
			done
			ocf_log err "Orphan storage device in $OCF_RESKEY_volgrpname slowing operations"
		fi
	fi

	return $OCF_SUCCESS
}

##
# Main start function for volume groups
##
function vg_start
{
	# scan for vg, restore transient failed pvs
	prep_for_activation

	get_vg_mode
	case $? in
	0) vg_start_normal;;
	1) vg_start_tagged;;
	2) vg_start_exclusive;;
	esac

	return $?
}

function vg_stop_normal
{
	vgdisplay "$OCF_RESKEY_volgrpname" 2>&1 | grep 'Volume group .* not found' >/dev/null && {
		ocf_log info "Volume group $OCF_RESKEY_volgrpname not found"
		return $OCF_SUCCESS
	}
	ocf_log info "Deactivating volume group $OCF_RESKEY_volgrpname"
	ocf_run vgchange -aln $OCF_RESKEY_volgrpname || return $OCF_ERR_GENERIC

	vg_status_normal
	# make sure vg isn't still running
	if [ $? -eq $OCF_SUCCESS ]; then
		ocf_log err "LVM: $OCF_RESKEY_volgrpname did not stop correctly"
		return $OCF_ERR_GENERIC 
	fi

	return $OCF_SUCCESS
}

function vg_stop_exclusive
{
	local a
	local results

	#  Shut down the volume group
	#  Do we need to make this resilient?
	a=0
	while ! vgchange -aln $OCF_RESKEY_volgrpname; do
		a=$(($a + 1))
		if [ $a -gt 10 ]; then
			break;
		fi
		ocf_log err "Unable to deactivate $OCF_RESKEY_volgrpname, retrying($a)"
		sleep 1
		which udevadm >& /dev/null && udevadm settle
	done

	#  Make sure all the logical volumes are inactive
	active=0
	results=(`lvs -o name,attr --noheadings $OCF_RESKEY_volgrpname 2> /dev/null`)
	a=0
	while [ ! -z ${results[$a]} ]; do
		if [[ ${results[$(($a + 1))]} =~ ....a. ]]; then
			active=1
			break
		fi
                a=$(($a + 2))
	done

	# lvs may not show active volumes if all PVs in VG are gone
	dmsetup table | grep -q "^${OCF_RESKEY_volgrpname//-/--}-[^-]"
	if [ $? -eq 0 ]; then
		active=1
	fi

	if [ $active -ne 0 ]; then
		ocf_log err "Logical volume $OCF_RESKEY_volgrpname/${results[$a]} failed to shutdown"
		return $OCF_ERR_GENERIC
	fi

	return $OCF_SUCCESS
}

function vg_stop_tagged
{
	local a
	local results

	#  Shut down the volume group
	#  Do we need to make this resilient?
	vgchange -an $OCF_RESKEY_volgrpname

	#  Make sure all the logical volumes are inactive
	active=0
	results=(`lvs -o name,attr --noheadings $OCF_RESKEY_volgrpname 2> /dev/null`)
	a=0
	while [ ! -z ${results[$a]} ]; do
		if [[ ${results[$(($a + 1))]} =~ ....a. ]]; then
			active=1
			break
		fi
	        a=$(($a + 2))
	done

	# lvs may not show active volumes if all PVs in VG are gone
	dmsetup table | grep -q "^${OCF_RESKEY_volgrpname//-/--}-[^-]"
	if [ $? -eq 0 ]; then
		active=1
	fi

	if [ $active -ne 0 ]; then
		ocf_log err "Logical volume $OCF_RESKEY_volgrpname/${results[$a]} failed to shutdown"
		return $OCF_ERR_GENERIC
	fi

	#  Make sure we are the owner before we strip the tags
	vg_tag_owner
	if [ $? -ne 0 ]; then
		strip_tags
	fi

	return $OCF_SUCCESS
}

##
# Main stop function for volume groups
##
function vg_stop
{
	get_vg_mode
	case $? in
	0) vg_stop_normal;;
	1) vg_stop_tagged;;
	2) vg_stop_exclusive;;
	esac

	return $?
}
