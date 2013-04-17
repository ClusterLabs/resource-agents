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
# returns mode to stdout
#
# 0 = normal (non-exclusive) local activation
# 1 = tagged-exclusive activation
# 2 = clvm-exclusive activation
##
get_vg_mode()
{
	if ocf_is_true "$OCF_RESKEY_exclusive"; then
		if [[ "$(vgs -o attr --noheadings $OCF_RESKEY_volgrpname)" =~ .....c ]]; then
			return 2
		else
			return 1
		fi
	else
		return 0
	fi
}

get_activate_options()
{
	local options="-a"

	get_vg_mode
	case $? in
	0) options="${options}ly";;
	1) options="${options}y";;
	2) options="${options}ey";;
	esac

	if ocf_is_true "$OCF_RESKEY_partial_activation"; then
		options="$options --partial"
	fi

	echo $options
}

is_dev_present()
{
	local dev=$1

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
			ocf_log err "Device node for $dev is not present"
			return $OCF_ERR_GENERIC
		fi
	else
		ocf_log err "Symbolic link for $dev is not present"
		return $OCF_ERR_NOT_RUNNING
	fi

	return $OCF_SUCCESS
}

# vg_tag_owner
#
# Returns:
#    1 == We are the owner
#    2 == We can claim it
#    0 == Owned by someone else
function vg_tag_owner
{
	local owner=`vgs -o tags --noheadings $OCF_RESKEY_volgrpname | tr -d ' '`
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
			ocf_log err "  $owner owns $OCF_RESKEY_volgrpname and is still a cluster member"
			return 0
		fi
		return 2
	fi

	return 1
}

function lvm_major_version
{
	# Get the LVM version number, for this to work we assume(thanks to panjiam):
	# 
	# LVM1 outputs like this
	#
	#	# vgchange --version
	#	vgchange: Logical Volume Manager 1.0.3
	#	Heinz Mauelshagen, Sistina Software  19/02/2002 (IOP 10)
	#
	# LVM2 and higher versions output in this format
	#
	#	# vgchange --version
	#	LVM version:     2.00.15 (2004-04-19)
	#	Library version: 1.00.09-ioctl (2004-03-31)
	#	Driver version:  4.1.0

	LVM_VERSION=`vgchange --version 2>&1 | \
		$AWK '/Logical Volume Manager/ {print $5"\n"; exit; }
		     /LVM version:/ {printf $3"\n"; exit;}'`
	rc=$?

	if
	  ( [ $rc -ne 0 ] || [ -z "$LVM_VERSION" ] )
	then
	  ocf_log err "LVM: could not determine LVM version. Defaulting to version 2."
	  return 2
	fi
	LVM_MAJOR="${LVM_VERSION%%.*}"

	if [ "$LVM_MAJOR" -eq "1" ]; then
		return 1
	fi

	return 2
}

function restore_transient_failed_pvs()
{
	local a=0
	local -a results

	results=(`pvs -o name,volgrpname,attr --noheadings | grep $OCF_RESKEY_volgrpname | grep -v 'unknown device'`)
	while [ ! -z "${results[$a]}" ] ; do
		if [[ ${results[$(($a + 2))]} =~ ..m ]] &&
			[ $OCF_RESKEY_volgrpname == ${results[$(($a + 1))]} ]; then

			ocf_log notice "Attempting to restore missing PV, ${results[$a]} in $OCF_RESKEY_volgrpname"
			vgextend --restoremissing $OCF_RESKEY_volgrpname ${results[$a]}
			if [ $? -ne 0 ]; then
				ocf_log notice "Failed to restore ${results[$a]}"
			else
				ocf_log notice "  ${results[$a]} restored"
			fi
		fi
		a=$(($a + 3))
	done
}

function prep_for_activation()
{
	lvm_major_version
	if [ $? -eq 1 ]; then
		ocf_run vgscan $OCF_RESKEY_volgrpname
	else
		ocf_run vgscan
	fi

	if [[ $(vgs -o attr --noheadings $OCF_RESKEY_volgrpname) =~ ...p ]]; then
		ocf_log err "Volume group \"$OCF_RESKEY_volgrpname\" has PVs marked as missing"
		restore_transient_failed_pvs
	fi
}

function strip_tags
{
	local i

	for i in `vgs --noheadings -o tags $OCF_RESKEY_volgrpname | sed s/","/" "/g`; do
		ocf_log info "Stripping tag, $i"

		# LVM version 2.02.98 allows changing tags if PARTIAL
		vgchange --deltag $i $OCF_RESKEY_volgrpname
	done

	if [ ! -z `vgs -o tags --noheadings $OCF_RESKEY_volgrpname | tr -d ' '` ]; then
		ocf_log err "Failed to remove ownership tags from $OCF_RESKEY_volgrpname"
		return $OCF_ERR_GENERIC
	fi

	return $OCF_SUCCESS
}

function strip_and_add_tag
{
	if ! strip_tags; then
		ocf_log err "Failed to remove tags from volume group, $OCF_RESKEY_volgrpname"
		return $OCF_ERR_GENERIC
	fi

	vgchange --addtag $(local_node_name) $OCF_RESKEY_volgrpname
	if [ $? -ne 0 ]; then
		ocf_log err "Failed to add ownership tag to $OCF_RESKEY_volgrpname"
		return $OCF_ERR_GENERIC
	fi

	ocf_log info "New tag \"$(local_node_name)\" added to $OCF_RESKEY_volgrpname"

	return $OCF_SUCCESS
}

function verify_exclusive_setup()
{
	##
	# Having cloned lvm resources with exclusive vg activation makes no sense at all.
	##
	if ocf_is_clone; then
		ocf_log_err "HA LVM: cloned lvm resources can not be activated exclusively"
		return $OCF_ERR_CONFIGURED
	fi

	##
	#  Are we using the "tagging" or "CLVM" variant for exclusive activation?
	#  The CLVM variant will have the cluster attribute set.
	##
	if [[ "$(vgs -o attr --noheadings --config 'global{locking_type=0}' $OCF_RESKEY_volgrpname 2>/dev/null)" =~ .....c ]]; then
		# Is clvmd running?
		if ! ps -C clvmd >& /dev/null; then
			ocf_log err "HA LVM: $OCF_RESKEY_volgrpname has the cluster attribute set, but 'clvmd' is not running"
			return $OCF_ERR_GENERIC
		fi
		return $OCF_SUCCESS
	fi

	##
	# The "tagging" variant is being used if we have gotten this far.
	##

	##
	# Make sure they are not trying to activate by logical volume
	# when tagging variant is in use.
	##
	if [ -n "$OCF_RESKEY_lvname" ]; then
		# Notes on why this is not safe.
		# When tags are being used, the entire volume group and logical volumes
		# have to be owned by a single node, logical volumes from from the same
		# volume group can not be split across multilple nodes unless cLVM
		# is in use to lock the volume group metadata
		ocf_log err "HA LVM: Only volume groups with the cluster attribute can have individual logical volumes activated exclusively."
		ocf_log_err "The volume group, $OCF_RESKEY_volgrpname, lacks the cluster attribute."
		ocf_log err "To correct this, remove the lv name from the config which will activate the entire group exclusively using ownership tags,"
		ocf_log err "or convert the group, $OCF_RESKEY_volgrpname, to a cluster group which requires the use of cLVM ."
	fi

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
	if lvm dumpconfig activation/volume_list | grep "\"$OCF_RESKEY_volgrpname\""; then
		ocf_log err "HA LVM:  Improper setup detected"
		ocf_log err "* $OCF_RESKEY_volgrpname found in \"volume_list\" in lvm.conf"
		return $OCF_ERR_GENERIC
	fi

	##
	# Next, we need to ensure that their initrd has been updated
	# If not, the machine could boot and activate the VG outside
	# the control of pacemaker
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

function verify_setup
{
	check_binary $AWK

	##
	# Off-the-shelf tests...  
	##
	VGOUT=`vgck ${OCF_RESKEY_volgrpname} 2>&1`
	if [ $? -ne 0 ]; then
		ocf_log err "Volume group [$OCF_RESKEY_volgrpname] does not exist or contains error! ${VGOUT}"
		exit $OCF_ERR_GENERIC
	fi

	##
	# Does the Volume Group exist?
	#  1) User may have forgotten to create it
	#  2) User may have misspelled it in the config file
	##
	lvm_major_version
	if [ $? -eq 1 ]; then
		VGOUT=`vgdisplay ${OCF_RESKEY_volgrpname} 2>&1`
	else
		VGOUT=`vgdisplay -v ${OCF_RESKEY_volgrpname} 2>&1`
	fi
	if [ $? -ne 0 ]; then
		ocf_log err "Volume group [$OCF_RESKEY_volgrpname] does not exist or contains error! ${VGOUT}"
		exit $OCF_ERR_GENERIC
	fi

	##
	# If exclusive activation is not enabled, then
	# further checking of proper setup is not necessary
	##
	if ! ocf_is_true "$OCF_RESKEY_exclusive"; then
		return $OCF_SUCCESS;
	fi

	##
	# exclusive activation is in use. do more checks
	##
	verify_exclusive_setup
	return $?
}

