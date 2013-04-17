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
		ocf_log info "Symbolic link for $dev is not present"
		return $OCF_NOT_RUNNING
	fi

	return $OCF_SUCCESS
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

	results=(`pvs -o name,vg_name,attr --noheadings | grep $OCF_RESKEY_volgrpname | grep -v 'unknown device'`)
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

	return $OCF_SUCCESS;
}

