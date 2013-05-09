#!/bin/sh
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
# All volume group variants call into this for status.
# Some variants (tags) call this and proceed with some
# other tests as well.
##
vg_status_normal()
{
	local dev="/dev/$OCF_RESKEY_volgrpname"

 	#
	# Check if links/device nodes are present
 	#
	if [ -d $dev ]; then
		test "`cd $dev && ls`" != ""
		if [ $? -eq 0 ]; then
			# volume group with logical volumes present
			return $OCF_SUCCESS
 		fi

		ocf_log err "Volume group, $OCF_RESKEY_volgrpname, with no logical volumes is not supported by this RA!"
		return $OCF_ERR_GENERIC
	fi

	return $OCF_NOT_RUNNING
}

vg_status_tagged()
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
# Main status for volume groups
##
vg_status()
{
	get_vg_mode
	case $? in
	0) vg_status_normal;;
	1) vg_status_tagged;; # slighty different because we verify tag ownership
	2) vg_status_normal;; # cluster exclusive is same as normal status.
	esac

	return $?
}

vg_start_normal()
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

vg_start_exclusive()
{
	local vgchange_options=$(get_activate_options)
	local resilience
	local try_again=false

	ocf_log info "Starting volume group, $OCF_RESKEY_volgrpname, exclusively"

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

		set -- $(lvs -o name,attr --noheadings $OCF_RESKEY_volgrpname 2> /dev/null)
		while [ $# -ge 2 ]; do
			case $2 in
			????ao*)
				# open LVs cannot be deactivated.
				: ;;
			*)
				if ! lvchange -an $OCF_RESKEY_volgrpname/$1; then
					ocf_log err "Unable to perform required deactivation of $OCF_RESKEY_volgrpname/$1 before starting"
					return $OCF_ERR_GENERIC
				fi
				;;
			esac

			shift 2
		done
	fi

	if $try_again && ! vg_start_normal; then
		ocf_log err "Failed to activate volume group, $OCF_RESKEY_volgrpname"
		return $OCF_ERR_GENERIC
	fi

	# The activation commands succeeded, but did they do anything?
	# Make sure all the logical volumes are active
	set -- $(lvs -o name,attr --noheadings $OCF_RESKEY_volgrpname 2> /dev/null)
	while [ $# -ge 2 ]; do
		case $2 in
		????[!a]*)
			resilience=" --config devices{filter=["
			for pv in $(pvs --noheadings -o name 2> /dev/null); do
				resilience=$resilience'"a|'$pv'|",'
			done
			resilience=$resilience"\"r|.*|\"]}"
			vgchange $vgchange_options $OCF_RESKEY_volgrpname $resilience
			break
			;;
		esac
		shift 2
	done

	#  We need to check the LVs again if we made the command resilient
	if [ -n "$resilience" ]; then
		set -- $(lvs -o name,attr --noheadings $OCF_RESKEY_volgrpname $resilience 2> /dev/null)
		while [ $# -ge 2 ]; do
			case $2 in
			????[!a]*)
				ocf_log err "Failed to activate $OCF_RESKEY_volgrpname/$1"
				return $OCF_ERR_GENERIC
				;;
			esac
			shift 2
		done
		ocf_log err "Orphan storage device in $OCF_RESKEY_volgrpname slowing operations"
	fi

	return $OCF_SUCCESS
}

vg_start_tagged()
{
	local lv
	local attr
	local resilience
	local vgchange_options=$(get_activate_options)

	ocf_log info "Starting volume group, $OCF_RESKEY_volgrpname, exclusively using tags"

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

	if ! vg_start_normal; then
		ocf_log err "Failed to activate volume group, $OCF_RESKEY_volgrpname"
		ocf_log err "Attempting activation of logical volumes one-by-one."

		set -- $(lvs -o name,attr --noheadings $OCF_RESKEY_volgrpname 2> /dev/null)
		while [ $# -ge 2 ]; do
			lv=$1
			attr=$2
			shift 2

			case $attr in
			[rR]???????p*)
				# Attempt "partial" activation of any RAID LVs
				ocf_log err "Attempting partial activation of ${OCF_RESKEY_volgrpname}/${lv}"
				if ! lvchange -ay --partial ${OCF_RESKEY_volgrpname}/${lv}; then
					ocf_log err "Failed attempt to activate ${OCF_RESKEY_volgrpname}/${lv} in partial mode"
					return $OCF_ERR_GENERIC
				fi
				ocf_log notice "Activation of ${OCF_RESKEY_volgrpname}/${lv} in partial mode succeeded"
				continue
				;;
			[mM]???????p*)
				ocf_log err "Attempting repair and activation of ${OCF_RESKEY_volgrpname}/${lv}"
				if ! lvconvert --repair --use-policies ${OCF_RESKEY_volgrpname}/${lv}; then
					ocf_log err "Failed to repair ${OCF_RESKEY_volgrpname}/${lv}"
					return $OCF_ERR_GENERIC
				fi
				if ! lvchange -ay ${OCF_RESKEY_volgrpname}/${lv}; then
					ocf_log err "Failed to activate ${OCF_RESKEY_volgrpname}/${lv}"
					return $OCF_ERR_GENERIC
				fi
				ocf_log notice "Repair and activation of ${OCF_RESKEY_volgrpname}/${lv} succeeded"
				continue
				;;
			*)
				ocf_log err "Attempting activation of non-redundant LV ${OCF_RESKEY_volgrpname}/${lv}"
				if ! lvchange -ay ${OCF_RESKEY_volgrpname}/${lv}; then
					ocf_log err "Failed to activate ${OCF_RESKEY_volgrpname}/${lv}"
					return $OCF_ERR_GENERIC
				fi
				ocf_log notice "Successfully activated non-redundant LV ${OCF_RESKEY_volgrpname}/${lv}"
				;;
			esac
		done
		return $OCF_SUCCESS

	else
		# The activation commands succeeded, but did they do anything?
		# Make sure all the logical volumes are active
		set -- $(lvs -o name,attr --noheadings $OCF_RESKEY_volgrpname 2> /dev/null)
		while [ $# -ge 2 ]; do
			lv=$1
			attr=$2
			shift 2

			case $attr in
			????[!a]*)
				resilience=" --config devices{filter=["
				for pv in $(pvs --noheadings -o name 2> /dev/null); do
					resilience=$resilience'"a|'$pv'|",'
				done
				resilience=$resilience"\"r|.*|\"]}"

				vgchange $vgchange_options $OCF_RESKEY_volgrpname $resilience
				break
				;;
			esac
		done

		#  We need to check the LVs again if we made the command resilient
		if [ ! -z "$resilience" ]; then
			set -- $(lvs -o name,attr --noheadings $OCF_RESKEY_volgrpname $resilience 2> /dev/null)
			while [ $# -ge 2 ]; do
				lv=$1
				attr=$2
				shift 2

				case $attr in
				????[!a]*)
					ocf_log err "Failed to activate $OCF_RESKEY_volgrpname"
					return $OCF_ERR_GENERIC
					;;
				esac
			done
			ocf_log err "Orphan storage device in $OCF_RESKEY_volgrpname slowing operations"
		fi
	fi

	return $OCF_SUCCESS
}

##
# Main start for volume groups
##
vg_start()
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

vg_stop_normal()
{
	local stop_options=$(get_stop_options)
	local a
	local lv
	local attr
	local rc=$OCF_ERR_GENERIC

	vgdisplay "$OCF_RESKEY_volgrpname" 2>&1 | grep 'Volume group .* not found' >/dev/null && {
		ocf_log info "Volume group $OCF_RESKEY_volgrpname not found"
		return $OCF_SUCCESS
	}
	ocf_log info "Deactivating volume group $OCF_RESKEY_volgrpname"
	ocf_run vgchange $stop_options $OCF_RESKEY_volgrpname

	a=0
	while :; do
		a=$(($a + 1))
		if [ $a -gt 10 ]; then
			a=$(($a - 1))
			ocf_log err "Unable to deactivate $OCF_RESKEY_volgrpname, retried($a), failed"
			break;
		fi

		# make sure vg isn't still running
		vg_status_normal
		res=$?
		if [ $res -eq $OCF_SUCCESS ]; then
			ocf_log err "Unable to deactivate $OCF_RESKEY_volgrpname, retrying($a)"
			sleep 1
			which udevadm > /dev/null 2>&1 && udevadm settle
			ocf_run vgchange $stop_options $OCF_RESKEY_volgrpname
		else
			rc=$OCF_SUCCESS
			break;
		fi
		
	done

	if [ $rc -eq $OCF_SUCCESS ]; then
		#  Verify all logical volumes are gone.
		set -- $(lvs -o name,attr --noheadings $OCF_RESKEY_volgrpname 2> /dev/null)
		while [ $# -ge 2 ]; do
			lv=$1
			attr=$2
			shift 2

			case $attr in
			????a*)
				ocf_log err "Logical volume $OCF_RESKEY_volgrpname/${lv} failed to shutdown"
				return $OCF_ERR_GENERIC
				;;
			esac
		done
	fi

	return $rc
}

vg_stop_exclusive()
{
	local lv

	#  Shut down the volume group
	#  Do we need to make this resilient? 
	vg_stop_normal;

	# lvs may not show active volumes if all PVs in VG are gone
	dmsetup table | grep -q "^$(echo $OCF_RESKEY_volgrpname | sed -e 's/-/--/g')-[^-]"
	if [ $? -eq 0 ]; then
		ocf_log err "Volume group $OCF_RESKEY_volgrpname failed to shutdown"
		return $OCF_ERR_GENERIC
	fi

	return $OCF_SUCCESS
}

vg_stop_tagged()
{
	#  Shut down the volume group
	#  Do we need to make this resilient?
	vg_stop_normal;

	# lvs may not show active volumes if all PVs in VG are gone
	dmsetup table | grep -q "^$(echo $OCF_RESKEY_volgrpname | sed -e 's/-/--/g')-[^-]"
	if [ $? -eq 0 ]; then
		ocf_log err "Volume group $OCF_RESKEY_volgrpname failed to shutdown"
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
# Main stop for volume groups
##
vg_stop()
{
	get_vg_mode
	case $? in
	0) vg_stop_normal;;
	1) vg_stop_tagged;;
	2) vg_stop_exclusive;;
	esac

	return $?
}
