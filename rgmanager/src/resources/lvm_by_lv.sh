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

# lv_verify
#
# Verify the parameters passed in
#
lv_verify()
{
	# Anything to verify?  Perhaps the names?
	return $OCF_SUCCESS
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

# lv_exec_resilient
#
# Sometimes, devices can come back.  Their metadata will conflict
# with the good devices that remain.  This function filters out those
# failed devices when executing the given command
#
# Finishing with vgscan resets the cache/filter
lv_exec_resilient()
{
	declare command=$1
	declare all_pvs

	ocf_log notice "Making resilient : $command"

	if [ -z "$command" ]; then
		ocf_log err "lv_exec_resilient: Arguments not supplied"
		return $OCF_ERR_ARGS
	fi

	# pvs will print out only those devices that are valid
	# If a device dies and comes back, it will not appear
	# in pvs output (but you will get a Warning).
	all_pvs=(`pvs --noheadings -o pv_name | grep -v Warning`)

	# Now we use those valid devices in a filter which we set up.
	# The device will then be activated because there are no
	# metadata conflicts.
        command=$command" --config devices{filter=["
	for i in ${all_pvs[*]}; do
		command=$command'"a|'$i'|",'
	done
	command=$command"\"r|.*|\"]}"

	ocf_log notice "Resilient command: $command"
	if ! $command ; then
		ocf_log err "lv_exec_resilient failed"
		vgscan
		return $OCF_ERR_GENERIC
	else
		vgscan
		return $OCF_SUCCESS
	fi
}

# lv_activate_resilient
#
# Sometimes, devices can come back.  Their metadata will conflict
# with the good devices that remain.  We must filter out those
# failed devices when trying to reactivate
lv_activate_resilient()
{
	declare action=$1
	declare lv_path=$2
	declare op="-ay"

	if [ -z "$action" ] || [ -z "$lv_path" ]; then
		ocf_log err "lv_activate_resilient: Arguments not supplied"
		return $OCF_ERR_ARGS
	fi

	if [ $action != "start" ]; then
	        op="-an"
	elif [[ "$(lvs -o attr --noheadings $lv_path)" =~ r.......p ]] ||
	     [[ "$(lvs -o attr --noheadings $lv_path)" =~ R.......p ]]; then
		# We can activate partial RAID LVs and run just fine.
		ocf_log notice "Attempting activation of partial RAID LV, $lv_path"
		op="-ay --partial"
	fi

	if ! lv_exec_resilient "lvchange $op $lv_path" ; then
		ocf_log err "lv_activate_resilient $action failed on $lv_path"
		return $OCF_ERR_GENERIC
	else
		return $OCF_SUCCESS
	fi
}

lv_status_clustered()
{
	#
	# Check if device is active
	#
	if [[ ! "$(lvs -o attr --noheadings $lv_path)" =~ ....a. ]]; then
		return $OCF_NOT_RUNNING
	fi

	return $OCF_SUCCESS
}

# lv_status
#
# Is the LV active?
lv_status_single()
{
	declare lv_path="$OCF_RESKEY_vg_name/$OCF_RESKEY_lv_name"
	declare dev="/dev/$lv_path"
	declare realdev
	declare owner
	declare my_name

	#
	# Check if device is active
	#
	if [[ ! "$(lvs -o attr --noheadings $lv_path)" =~ ....a. ]]; then
		return $OCF_NOT_RUNNING
	fi

	if [[ "$(vgs -o attr --noheadings $OCF_RESKEY_vg_name)" =~ .....c ]]; then
		ocf_log notice "$OCF_RESKEY_vg_name is a cluster volume.  Ignoring..."
		return $OCF_SUCCESS
	fi

	#
	# Check if all links/device nodes are present
	#
        if [ -h "$dev" ]; then
		realdev=$(readlink -f $dev)
		if [ $? -ne 0 ]; then
			ocf_log err "Failed to follow link, $dev"
			return $OCF_ERR_ARGS
		fi

		if [ ! -b $realdev ]; then
			ocf_log err "Device node for $lv_path is not present"
			return $OCF_ERR_GENERIC
		fi
	else
	        ocf_log err "Symbolic link for $lv_path is not present"
		return $OCF_ERR_GENERIC
	fi

	#
	# Verify that we are the correct owner
	#
	owner=`lvs -o tags --noheadings $lv_path | tr -d ' '`
	my_name=$(local_node_name)
	if [ -z "$my_name" ]; then
		ocf_log err "Unable to determine local machine name"

		# FIXME: I don't really want to fail on 1st offense
		return $OCF_SUCCESS
	fi

	if [ -z "$owner" ] || [ "$my_name" != "$owner" ]; then
		ocf_log err "WARNING: $lv_path should not be active"
		ocf_log err "WARNING: $my_name does not own $lv_path"
		ocf_log err "WARNING: Attempting shutdown of $lv_path"

		lv_activate_resilient "stop" $lv_path
		return $OCF_ERR_GENERIC
	fi

	return $OCF_SUCCESS
}

function lv_status
{
	# We pass in the VG name to see of the logical volume is clustered
	if [[ $(vgs -o attr --noheadings $OCF_RESKEY_vg_name) =~ .....c ]]; then
		lv_status_clustered
	else
		lv_status_single
	fi
}

# lv_activate_and_tag
lv_activate_and_tag()
{
	declare action=$1
	declare tag=$2
	declare lv_path=$3
	typeset self_fence=""

	case ${OCF_RESKEY_self_fence} in
		"yes")          self_fence=1 ;;
		1)              self_fence=1 ;;
		*)              self_fence="" ;;
	esac

	if [ -z "$action" ] || [ -z "$tag" ] || [ -z "$lv_path" ]; then
		ocf_log err "Supplied args: 1) $action, 2) $tag, 3) $lv_path"
		return $OCF_ERR_ARGS
	fi

	if [ "$action" == "start" ]; then
		ocf_log notice "Activating $lv_path"
		lvchange --addtag $tag $lv_path
		if [ $? -ne 0 ]; then
			ocf_log err "Unable to add tag to $lv_path"
			return $OCF_ERR_GENERIC
		fi

		if ! lv_activate_resilient $action $lv_path; then
			ocf_log err "Unable to activate $lv_path"
			return $OCF_ERR_GENERIC
		fi
	else
		ocf_log notice "Deactivating $lv_path"
		if ! lv_activate_resilient $action $lv_path; then
			if [ "$self_fence" ]; then
				ocf_log err "Unable to deactivate $lv_path: REBOOTING"
				sync
				reboot -fn
			else
				ocf_log err "Unable to deactivate $lv_path"
			fi
			return $OCF_ERR_GENERIC
		fi

		ocf_log notice "Removing ownership tag ($tag) from $lv_path"

		lvchange --deltag $tag $lv_path
		if [ $? -ne 0 ]; then
			ocf_log err "Unable to delete tag from $lv_path"

			# Newer versions of LVM require the missing PVs to
			# be removed from the VG via a separate call before
			# the tag can be removed.
			ocf_log err "Attempting volume group clean-up and retry"
			vgreduce --removemissing --force $OCF_RESKEY_vg_name

			# Retry tag deletion
			lvchange --deltag $tag $lv_path
			if [ $? -ne 0 ]; then
				ocf_log err "Failed to delete tag from $lv_path"
				return $OCF_ERR_GENERIC
			fi
		fi

		if [ "`lvs --noheadings -o lv_tags $lv_path`" == $tag ]; then
			ocf_log notice "Removing ownership tag ($tag) from $lv_path"
			lvchange --deltag $tag $lv_path
			if [ $? -ne 0 ]; then
				ocf_log err "Unable to delete tag from $lv_path"
				return $OCF_ERR_GENERIC
			fi
		fi
	fi

	return $OCF_SUCCESS
}

# lv_activate
# $1: start/stop only
#
# Basically, if we want to [de]activate an LVM volume,
# we must own it.  That means that our tag must be on it.
# This requires a change to /etc/lvm/lvm.conf:
#	volume_list = [ "root_volume", "@my_hostname" ]
# where "root_volume" is your root volume group and
# "my_hostname" is $(local_node_name)
#
# If there is a node failure, we may wish to "steal" the
# LV.  For that, we need to check if the node that owns
# it is still part of the cluster.  We use the tag to
# determine who owns the volume then query for their
# liveness.  If they are dead, we can steal.
lv_activate()
{
	declare lv_path="$OCF_RESKEY_vg_name/$OCF_RESKEY_lv_name"
	declare owner=`lvs -o tags --noheadings $lv_path | tr -d ' '`
	declare my_name=$(local_node_name)

	if [ -z "$my_name" ]; then
		ocf_log err "Unable to determine cluster node name"
		return $OCF_ERR_GENERIC
	fi

	#
	# FIXME: This code block is repeated below... might be
	# nice to put it in a function
	#
	if [ ! -z $owner ] && [ $owner != $my_name ]; then
		if is_node_member_clustat $owner ; then
			ocf_log err "$owner owns $lv_path unable to $1"
			return $OCF_ERR_GENERIC
		fi
		ocf_log notice "Owner of $lv_path is not in the cluster"
		ocf_log notice "Stealing $lv_path"

		lvchange --deltag $owner $lv_path
		if [ $? -ne 0 ]; then
			ocf_log err "Failed to steal $lv_path from $owner"
			return $OCF_ERR_GENERIC
		fi

		# Warning --deltag doesn't always result in failure
		if [ ! -z `lvs -o tags --noheadings $lv_path | tr -d ' '` ]; then
			ocf_log err "Failed to steal $lv_path from $owner."
			return $OCF_ERR_GENERIC
		fi
	fi

	# If this is a partial VG, attempt to
	# restore any transiently failed PVs
	if [[ $(vgs -o attr --noheadings $OCF_RESKEY_vg_name) =~ ...p ]]; then
		ocf_log err "Volume group \"$OCF_RESKEY_vg_name\" has PVs marked as missing"
		restore_transient_failed_pvs
	fi

	if ! lv_activate_and_tag $1 $my_name $lv_path; then
		ocf_log err "Failed to $1 $lv_path"

		ocf_log notice "Attempting cleanup of $OCF_RESKEY_vg_name"

		if vgreduce --removemissing --force --config \
		    "activation { volume_list = \"$OCF_RESKEY_vg_name\" }" \
		    $OCF_RESKEY_vg_name; then
			ocf_log notice "$OCF_RESKEY_vg_name now consistent"
			owner=`lvs -o tags --noheadings $lv_path | tr -d ' '`
			if [ ! -z $owner ] && [ $owner != $my_name ]; then
				if is_node_member_clustat $owner ; then
					ocf_log err "$owner owns $lv_path unable to $1"
					return $OCF_ERR_GENERIC
				fi
				ocf_log notice "Owner of $lv_path is not in the cluster"
				ocf_log notice "Stealing $lv_path"

				lvchange --deltag $owner $lv_path
				if [ $? -ne 0 ]; then
					ocf_log err "Failed to steal $lv_path from $owner"
					return $OCF_ERR_GENERIC
				fi

				# Warning --deltag doesn't always result in failure
				if [ ! -z `lvs -o tags --noheadings $lv_path | tr -d ' '` ]; then
					ocf_log err "Failed to steal $lv_path from $owner."
					return $OCF_ERR_GENERIC
				fi
			fi

			if ! lv_activate_and_tag $1 $my_name $lv_path; then
				ocf_log err "Failed second attempt to $1 $lv_path"
				return $OCF_ERR_GENERIC
			else
				ocf_log notice "Second attempt to $1 $lv_path successful"
				return $OCF_SUCCESS
			fi
		else
			ocf_log err "Failed to $1 $lv_path"
			return $OCF_ERR_GENERIC
		fi
	fi
	return $OCF_SUCCESS
}

function lv_start_clustered
{
	if lvchange -aey $OCF_RESKEY_vg_name/$OCF_RESKEY_lv_name; then
		return $OCF_SUCCESS
	fi

	# FAILED exclusive activation:
	# This can be caused by an LV being active remotely.
	# Before attempting a repair effort, we should attempt
	# to deactivate the LV cluster-wide; but only if the LV
	# is not open.  Otherwise, it is senseless to attempt.
	if ! [[ "$(lvs -o attr --noheadings $OCF_RESKEY_vg_name/$OCF_RESKEY_lv_name)" =~ ....ao ]]; then
		# We'll wait a small amount of time for some settling before
		# attempting to deactivate.  Then the deactivate will be
		# immediately followed by another exclusive activation attempt.
		sleep 5
		if ! lvchange -an $OCF_RESKEY_vg_name/$OCF_RESKEY_lv_name; then
			# Someone could have the device open.
			# We can't do anything about that.
			ocf_log err "Unable to perform required deactivation of $OCF_RESKEY_vg_name/$OCF_RESKEY_lv_name before starting"
			return $OCF_ERR_GENERIC
		fi

		if lvchange -aey $OCF_RESKEY_vg_name/$OCF_RESKEY_lv_name; then
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
	ocf_log err "Failed to activate logical volume, $OCF_RESKEY_vg_name/$OCF_RESKEY_lv_name"
	ocf_log notice "Attempting cleanup of $OCF_RESKEY_vg_name/$OCF_RESKEY_lv_name"

	if ! lvconvert --repair --use-policies $OCF_RESKEY_vg_name/$OCF_RESKEY_lv_name; then
		ocf_log err "Failed to cleanup $OCF_RESKEY_vg_name/$OCF_RESKEY_lv_name"
		return $OCF_ERR_GENERIC
	fi

	if ! lvchange -aey $OCF_RESKEY_vg_name/$OCF_RESKEY_lv_name; then
		ocf_log err "Failed second attempt to activate $OCF_RESKEY_vg_name/$OCF_RESKEY_lv_name"
		return $OCF_ERR_GENERIC
	fi

	ocf_log notice "Second attempt to activate $OCF_RESKEY_vg_name/$OCF_RESKEY_lv_name successful"
	return $OCF_SUCCESS
}

function lv_start_single
{
	if ! lvs $OCF_RESKEY_vg_name >& /dev/null; then
		lv_count=0
	else
		lv_count=`lvs --noheadings -o name $OCF_RESKEY_vg_name | grep -v _mlog | grep -v _mimage | grep -v nconsistent | wc -l`
	fi
	if [ $lv_count -gt 1 ]; then
		ocf_log err "HA LVM requires Only one logical volume per volume group."
		ocf_log err "There are currently $lv_count logical volumes in $OCF_RESKEY_vg_name"
		ocf_log err "Failing HA LVM start of $OCF_RESKEY_vg_name/$OCF_RESKEY_lv_name"
		exit $OCF_ERR_GENERIC
	fi

	if ! lv_activate start; then
		return 1
	fi

	return 0
}

function lv_start
{
	# We pass in the VG name to see of the logical volume is clustered
	if [[ "$(vgs -o attr --noheadings $OCF_RESKEY_vg_name)" =~ .....c ]]; then
		lv_start_clustered
	else
		lv_start_single
	fi
}

function lv_stop_clustered
{
	lvchange -aln $OCF_RESKEY_vg_name/$OCF_RESKEY_lv_name
}

function lv_stop_single
{
	if ! lv_activate stop; then
		return 1
	fi

	return 0
}

function lv_stop
{
	# We pass in the VG name to see of the logical volume is clustered
	if [[ "$(vgs -o attr --noheadings $OCF_RESKEY_vg_name)" =~ .....c ]]; then
		lv_stop_clustered
	else
		lv_stop_single
	fi
}
