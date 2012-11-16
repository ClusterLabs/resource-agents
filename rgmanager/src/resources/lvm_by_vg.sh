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

# vg_owner
#
# Returns:
#    1 == We are the owner
#    2 == We can claim it
#    0 == Owned by someone else
function vg_owner
{
	local owner=`vgs -o tags --noheadings $OCF_RESKEY_vg_name | tr -d ' '`
	local my_name=$(local_node_name)

	if [ -z "$my_name" ]; then
		ocf_log err "Unable to determine cluster node name"
		return 0
	fi

	if [ -z "$owner" ]; then
		# No-one owns this VG yet, so we can claim it
		return 2
	fi

	if [ $owner != $my_name ]; then
		if is_node_member_clustat $owner ; then
			ocf_log err "  $owner owns $OCF_RESKEY_vg_name and is still a cluster member"
			return 0
		fi
		return 2
	fi

	return 1
}

restore_transient_failed_pvs()
{
        local a=0
        local -a results

        results=(`pvs -o name,vg_name,attr --noheadings | grep $OCF_RESKEY_vg_name | grep -v 'unknown device'`)
        while [ ! -z "${results[$a]}" ] ; do
                if [[ ${results[$(($a + 2))]} =~ ..m ]] &&
                   [ $OCF_RESKEY_vg_name == ${results[$(($a + 1))]} ]; then
                        ocf_log notice "Attempting to restore missing PV, ${results[$a]} in $OCF_RESKEY_vg_name"
                        vgextend --restoremissing $OCF_RESKEY_vg_name ${results[$a]}
                        if [ $? -ne 0 ]; then
                                ocf_log notice "Failed to restore ${results[$a]}"
                        else
                                ocf_log notice "  ${results[$a]} restored"
                        fi
                fi
                a=$(($a + 3))
        done
}

function strip_tags
{
	local i

	for i in `vgs --noheadings -o tags $OCF_RESKEY_vg_name | sed s/","/" "/g`; do
		ocf_log info "Stripping tag, $i"

		# LVM version 2.02.98 allows changing tags if PARTIAL
		vgchange --deltag $i $OCF_RESKEY_vg_name
	done

	if [ ! -z `vgs -o tags --noheadings $OCF_RESKEY_vg_name | tr -d ' '` ]; then
		ocf_log err "Failed to remove ownership tags from $OCF_RESKEY_vg_name"
		return $OCF_ERR_GENERIC
	fi

	return $OCF_SUCCESS
}

function strip_and_add_tag
{
	if ! strip_tags; then
		ocf_log err "Failed to remove tags from volume group, $OCF_RESKEY_vg_name"
		return $OCF_ERR_GENERIC
	fi

	vgchange --addtag $(local_node_name) $OCF_RESKEY_vg_name
	if [ $? -ne 0 ]; then
		ocf_log err "Failed to add ownership tag to $OCF_RESKEY_vg_name"
		return $OCF_ERR_GENERIC
        fi

	ocf_log info "New tag \"$(local_node_name)\" added to $OCF_RESKEY_vg_name"

	return $OCF_SUCCESS
}

function vg_status_clustered
{
	return $OCF_SUCCESS
}

# vg_status
#
# Are all the LVs active?
function vg_status_single
{
	local i
	local dev
	local my_name=$(local_node_name)

	#
	# Check that all LVs are active
	#
	for i in `lvs $OCF_RESKEY_vg_name --noheadings -o attr`; do
		if [[ ! $i =~ ....a. ]]; then
			return $OCF_NOT_RUNNING
		fi
	done

	#
	# Check if all links/device nodes are present
	#
	for i in `lvs $OCF_RESKEY_vg_name --noheadings -o name`; do
		dev="/dev/$OCF_RESKEY_vg_name/$i"

		if [ -h $dev ]; then
			realdev=$(readlink -f $dev)
			if [ $? -ne 0 ]; then
				ocf_log err "Failed to follow link, $dev"
				return $OCF_ERR_GENERIC
			fi

			if [ ! -b $realdev ]; then
				ocf_log err "Device node for $dev is not present"
				return $OCF_ERR_GENERIC
			fi
		else
			ocf_log err "Symbolic link for $lv_path is not present"
			return $OCF_ERR_GENERIC
		fi
	done

	#
	# Verify that we are the correct owner
	#
	vg_owner
	if [ $? -ne 1 ]; then
		ocf_log err "WARNING: $OCF_RESKEY_vg_name should not be active"
		ocf_log err "WARNING: $my_name does not own $OCF_RESKEY_vg_name"
		ocf_log err "WARNING: Attempting shutdown of $OCF_RESKEY_vg_name"

		# FIXME: may need more force to shut this down
		vgchange -an $OCF_RESKEY_vg_name
		return $OCF_ERR_GENERIC
	fi

	return $OCF_SUCCESS
}

##
# Main status function for volume groups
##
function vg_status
{
	if [[ "$(vgs -o attr --noheadings $OCF_RESKEY_vg_name)" =~ .....c ]]; then
		vg_status_clustered
	else
		vg_status_single
	fi
}

function vg_verify
{
	# Anything to verify?
	return $OCF_SUCCESS
}

function vg_start_clustered
{
	local a
	local results
	local all_pvs
	local resilience
	local try_again=false

	ocf_log info "Starting volume group, $OCF_RESKEY_vg_name"

	if ! vgchange -aey $OCF_RESKEY_vg_name; then
		try_again=true

		# Failure to activate:
		# This could be caused by a remotely active LV.  Before
		# attempting any repair of the VG, we will first attempt
		# to deactivate the VG cluster-wide.
		# We must check for open LVs though, since these cannot
		# be deactivated.  We have no choice but to go one-by-one.

		# Allow for some settling
		sleep 5

		results=(`lvs -o name,attr --noheadings $OCF_RESKEY_vg_name 2> /dev/null`)
		a=0
		while [ ! -z "${results[$a]}" ]; do
			if [[ ! ${results[$(($a + 1))]} =~ ....ao ]]; then
				if ! lvchange -an $OCF_RESKEY_vg_name/${results[$a]}; then
					ocf_log err "Unable to perform required deactivation of $OCF_RESKEY_vg_name before starting"
					return $OCF_ERR_GENERIC
				fi
			fi
			a=$(($a + 2))
		done
	fi

	if try_again && ! vgchange -aey $OCF_RESKEY_vg_name; then
		ocf_log err "Failed to activate volume group, $OCF_RESKEY_vg_name"
		ocf_log notice "Attempting cleanup of $OCF_RESKEY_vg_name"

		if ! vgreduce --removemissing --force $OCF_RESKEY_vg_name; then
			ocf_log err "Failed to make $OCF_RESKEY_vg_name consistent"
			return $OCF_ERR_GENERIC
		fi

		if ! vgchange -aey $OCF_RESKEY_vg_name; then
			ocf_log err "Failed second attempt to activate $OCF_RESKEY_vg_name"
			return $OCF_ERR_GENERIC
		fi

		ocf_log notice "Second attempt to activate $OCF_RESKEY_vg_name successful"
		return $OCF_SUCCESS
	else
		# The activation commands succeeded, but did they do anything?
		# Make sure all the logical volumes are active
		results=(`lvs -o name,attr --noheadings 2> /dev/null $OCF_RESKEY_vg_name`)
		a=0
		while [ ! -z "${results[$a]}" ]; do
			if [[ ! ${results[$(($a + 1))]} =~ ....a. ]]; then
				all_pvs=(`pvs --noheadings -o name 2> /dev/null`)
				resilience=" --config devices{filter=["
			        for i in ${all_pvs[*]}; do
			                resilience=$resilience'"a|'$i'|",'
        			done
				resilience=$resilience"\"r|.*|\"]}"

				vgchange -aey $OCF_RESKEY_vg_name $resilience
				break
			fi
			a=$(($a + 2))
		done

		#  We need to check the LVs again if we made the command resilient
		if [ ! -z "$resilience" ]; then
			results=(`lvs -o name,attr --noheadings $OCF_RESKEY_vg_name $resilience 2> /dev/null`)
			a=0
			while [ ! -z ${results[$a]} ]; do
				if [[ ! ${results[$(($a + 1))]} =~ ....a. ]]; then
		                        ocf_log err "Failed to activate $OCF_RESKEY_vg_name"
                		        return $OCF_ERR_GENERIC
                		fi
				a=$(($a + 2))
			done
			ocf_log err "Orphan storage device in $OCF_RESKEY_vg_name slowing operations"
		fi
	fi

	return $OCF_SUCCESS
}

function vg_start_single
{
	local a
	local results
	local all_pvs
	local resilience

	ocf_log info "Starting volume group, $OCF_RESKEY_vg_name"

	vg_owner
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

	if ! vgchange -ay $OCF_RESKEY_vg_name; then
		ocf_log err "Failed to activate volume group, $OCF_RESKEY_vg_name"
		ocf_log err "Attempting activation of logical volumes one-by-one."

		results=(`lvs -o name,attr --noheadings $OCF_RESKEY_vg_name 2> /dev/null`)
		a=0
		while [ ! -z ${results[$a]} ]; do
			if [[ ${results[$(($a + 1))]} =~ r.......p ]] ||
		   	   [[ ${results[$(($a + 1))]} =~ R.......p ]]; then
				# Attempt "partial" activation of any RAID LVs
				ocf_log err "Attempting partial activation of ${OCF_RESKEY_vg_name}/${results[$a]}"
				if ! lvchange -ay --partial ${OCF_RESKEY_vg_name}/${results[$a]}; then
					ocf_log err "Failed attempt to activate ${OCF_RESKEY_vg_name}/${results[$a]} in partial mode"
					return $OCF_ERR_GENERIC
				fi
				ocf_log notice "Activation of ${OCF_RESKEY_vg_name}/${results[$a]} in partial mode succeeded"
			elif [[ ${results[$(($a + 1))]} =~ m.......p ]] ||
		   	     [[ ${results[$(($a + 1))]} =~ M.......p ]]; then
				ocf_log err "Attempting repair and activation of ${OCF_RESKEY_vg_name}/${results[$a]}"
				if ! lvconvert --repair --use-policies ${OCF_RESKEY_vg_name}/${results[$a]}; then
					ocf_log err "Failed to repair ${OCF_RESKEY_vg_name}/${results[$a]}"
					return $OCF_ERR_GENERIC
				fi
				if ! lvchange -ay ${OCF_RESKEY_vg_name}/${results[$a]}; then
					ocf_log err "Failed to activate ${OCF_RESKEY_vg_name}/${results[$a]}"
					return $OCF_ERR_GENERIC
				fi
				ocf_log notice "Repair and activation of ${OCF_RESKEY_vg_name}/${results[$a]} succeeded"
			else
				ocf_log err "Attempting activation of non-redundant LV ${OCF_RESKEY_vg_name}/${results[$a]}"
				if ! lvchange -ay ${OCF_RESKEY_vg_name}/${results[$a]}; then
					ocf_log err "Failed to activate ${OCF_RESKEY_vg_name}/${results[$a]}"
					return $OCF_ERR_GENERIC
				fi
				ocf_log notice "Successfully activated non-redundant LV ${OCF_RESKEY_vg_name}/${results[$a]}"
			fi
			a=$(($a + 2))
		done

		return $OCF_SUCCESS
	else
		# The activation commands succeeded, but did they do anything?
		# Make sure all the logical volumes are active
		results=(`lvs -o name,attr --noheadings $OCF_RESKEY_vg_name 2> /dev/null`)
		a=0
		while [ ! -z ${results[$a]} ]; do
			if [[ ! ${results[$(($a + 1))]} =~ ....a. ]]; then
				all_pvs=(`pvs --noheadings -o name 2> /dev/null`)
				resilience=" --config devices{filter=["
			        for i in ${all_pvs[*]}; do
			                resilience=$resilience'"a|'$i'|",'
        			done
				resilience=$resilience"\"r|.*|\"]}"

				vgchange -ay $OCF_RESKEY_vg_name $resilience
				break
			fi
			a=$(($a + 2))
		done

		#  We need to check the LVs again if we made the command resilient
		if [ ! -z "$resilience" ]; then
			results=(`lvs -o name,attr --noheadings $OCF_RESKEY_vg_name $resilience 2> /dev/null`)
			a=0
			while [ ! -z ${results[$a]} ]; do
				if [[ ! ${results[$(($a + 1))]} =~ ....a. ]]; then
		                        ocf_log err "Failed to activate $OCF_RESKEY_vg_name"
                		        return $OCF_ERR_GENERIC
                		fi
				a=$(($a + 2))
			done
			ocf_log err "Orphan storage device in $OCF_RESKEY_vg_name slowing operations"
		fi
	fi

	return $OCF_SUCCESS
}

##
# Main start function for volume groups
##
function vg_start
{
	local a=0
	local results

	if [[ $(vgs -o attr --noheadings $OCF_RESKEY_vg_name) =~ ...p ]]; then
                ocf_log err "Volume group \"$OCF_RESKEY_vg_name\" has PVs marked as missing"
                restore_transient_failed_pvs
        fi

	if [[ "$(vgs -o attr --noheadings $OCF_RESKEY_vg_name)" =~ .....c ]]; then
		vg_start_clustered
	else
		vg_start_single
	fi
}

function vg_stop_clustered
{
	local a
	local results
	typeset self_fence=""

	case ${OCF_RESKEY_self_fence} in
		"yes")          self_fence=1 ;;
		1)              self_fence=1 ;;
		*)              self_fence="" ;;
	esac

	#  Shut down the volume group
	#  Do we need to make this resilient?
	vgchange -aln $OCF_RESKEY_vg_name

	#  Make sure all the logical volumes are inactive
	active=0
	results=(`lvs -o name,attr --noheadings $OCF_RESKEY_vg_name 2> /dev/null`)
	a=0
	while [ ! -z ${results[$a]} ]; do
		if [[ ${results[$(($a + 1))]} =~ ....a. ]]; then
			active=1
			break
		fi
                a=$(($a + 2))
	done

	# lvs may not show active volumes if all PVs in VG are gone
	dmsetup table | grep -q "^${OCF_RESKEY_vg_name//-/--}-[^-]"
	if [ $? -eq 0 ]; then
		active=1
	fi

	if [ $active -ne 0 ]; then
		if [ "$self_fence" ]; then
			ocf_log err "Unable to deactivate $lv_path REBOOT"
			sync
			reboot -fn
		else
			ocf_log err "Logical volume $OCF_RESKEY_vg_name/${results[$a]} failed to shutdown"
		fi
		return $OCF_ERR_GENERIC
	fi

	return $OCF_SUCCESS
}

function vg_stop_single
{
	local a
	local results
	typeset self_fence=""

	case ${OCF_RESKEY_self_fence} in
		"yes")          self_fence=1 ;;
		1)              self_fence=1 ;;
		*)              self_fence="" ;;
	esac

	#  Shut down the volume group
	#  Do we need to make this resilient?
	vgchange -an $OCF_RESKEY_vg_name

	#  Make sure all the logical volumes are inactive
        active=0
	results=(`lvs -o name,attr --noheadings $OCF_RESKEY_vg_name 2> /dev/null`)
	a=0
	while [ ! -z ${results[$a]} ]; do
		if [[ ${results[$(($a + 1))]} =~ ....a. ]]; then
			active=1
			break
		fi
	        a=$(($a + 2))
	done

        # lvs may not show active volumes if all PVs in VG are gone
        dmsetup table | grep -q "^${OCF_RESKEY_vg_name//-/--}-[^-]"
        if [ $? -eq 0 ]; then
                active=1
        fi

        if [ $active -ne 0 ]; then
		if [ "$self_fence" ]; then
			ocf_log err "Unable to deactivate $lv_path REBOOT"
			sync
			reboot -fn
		else
			ocf_log err "Logical volume $OCF_RESKEY_vg_name/${results[$a]} failed to shutdown"
		fi
		return $OCF_ERR_GENERIC
	fi

	#  Make sure we are the owner before we strip the tags
	vg_owner
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
	if [[ "$(vgs -o attr --noheadings $OCF_RESKEY_vg_name)" =~ .....c ]]; then
		vg_stop_clustered
	else
		vg_stop_single
	fi
}
