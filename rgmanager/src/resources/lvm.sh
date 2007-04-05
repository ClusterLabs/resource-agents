#!/bin/bash

#
#  Copyright Red Hat Inc., 2007
#
#  This program is free software; you can redistribute it and/or modify it
#  under the terms of the GNU General Public License as published by the
#  Free Software Foundation; either version 2, or (at your option) any
#  later version.
#
#  This program is distributed in the hope that it will be useful, but
#  WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#  General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; see the file COPYING.  If not, write to the
#  Free Software Foundation, Inc.,  675 Mass Ave, Cambridge, 
#  MA 02139, USA.
#

#
# LVM Failover Script.
#
# This script correctly handles:
#  - Relocation
#  - Fail-over
#  - Disk failure + Fail-over
# If you don't know what those mean, ASK!  (jbrassow@redhat.com)
# NOTE: Changes to /etc/lvm/lvm.conf are required for proper operation.
#
# This script should handle (but doesn't right now):
#  - Operations on VG level.  Make lv_name optional.  This would have
#    the effect of moving all LVs in a VG, not just one LV



LC_ALL=C
LANG=C
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export LC_ALL LANG PATH

. $(dirname $0)/ocf-shellfuncs
. $(dirname $0)/utils/member_util.sh

rv=0

meta_data()
{
	cat <<EOT
<?xml version="1.0" ?>
<resource-agent name="lvm" version="rgmanager 2.0">
    <version>1.0</version>

    <longdesc lang="en">
	This defines a LVM volume group that is ...
    </longdesc>

    <shortdesc lang="en">
	LVM Failover script
    </shortdesc>

    <parameters>
        <parameter name="name" primary="1">
            <longdesc lang="en">
                Descriptive name LVM Volume group
            </longdesc>
            <shortdesc lang="en">
                Name
            </shortdesc>
	    <content type="string"/>
        </parameter>

        <parameter name="vg_name" required="1">
            <longdesc lang="en">
                If you can see this, your GUI is broken.
            </longdesc>
            <shortdesc lang="en">
                If you can see this, your GUI is broken.
            </shortdesc>
	    <content type="string"/>
        </parameter>

        <parameter name="lv_name" required="1">
            <longdesc lang="en">
                If you can see this, your GUI is broken.
            </longdesc>
            <shortdesc lang="en">
                If you can see this, your GUI is broken.
            </shortdesc>
	    <content type="string"/>
        </parameter>

	<parameter name="nfslock" inherit="service%nfslock">
	    <longdesc lang="en">
	        If set and unmounting the file system fails, the node will
		try to kill lockd and issue reclaims across all remaining
		network interface cards.
	    </longdesc>
	    <shortdesc lang="en">
	        Enable NFS lock workarounds
	    </shortdesc>
	    <content type="boolean"/>
	</parameter>

    </parameters>

    <actions>
        <action name="start" timeout="5"/>
	<action name="stop" timeout="5"/>

	<action name="status" timeout="5" interval="1h"/>
	<action name="monitor" timeout="5" interval="1h"/>

	<action name="meta-data" timeout="5"/>
	<action name="validate-all" timeout="30"/>
    </actions>

    <special tag="rgmanager">
    	<attributes maxinstances="1"/>
    </special>

</resource-agent>
EOT
}

# verify_all
#
# Verify the parameters passed in
#
verify_all()
{
	declare lv_path="$OCF_RESKEY_vg_name/$OCF_RESKEY_lv_name"
	declare -i ret=0

	# Anything to verify?  Perhaps the names?
	ocf_log notice "Verifying $lv_path"

	return $ret
}

vg_status()
{
	return $OCF_ERR_GENERIC
}

vg_activate()
{
	return $OCF_ERR_GENERIC
}

# lv_status
#
# Is the LV active?
lv_status()
{
	declare lv_path="$OCF_RESKEY_vg_name/$OCF_RESKEY_lv_name"
	declare dev="/dev/$lv_path"
	declare realdev
	declare owner
	declare my_name

	#
	# Check if device is active
	#
	if [[ ! $(lvs -o attr --noheadings $lv_path) =~ ....a. ]]; then
	    return $OCF_ERR_GENERIC
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
	owner=`lvs -o tags --noheadings $lv_path`
	my_name=$(local_node_name)
	if [ -z $my_name ]; then
		ocf_log err "Unable to determine local machine name"

		# FIXME: I don't really want to fail on 1st offense
		return $OCF_SUCCESS
	fi

	if [ -z $owner ] || [ $my_name != $owner ]; then
		ocf_log err "WARNING: $lv_path should not be active"
		ocf_log err "WARNING: $my_name does not own $lv_path"
		ocf_log err "WARNING: Attempting shutdown of $lv_path"

		lvchange -an $lv_path
		return $OCF_ERR_GENERIC
	fi

	return $OCF_SUCCESS
}

# lv_activate_and_tag
lv_activate_and_tag()
{
	declare action=$1
	declare tag=$2
	declare lv_path=$3

	if [ -z $action ] || [ -z $tag ] || [ -z $lv_path ]; then
		ocf_log err "Supplied args: 1) $action, 2) $tag, 3) $lv_path"
		return $OCF_ERR_ARGS
	fi

	if [ $action == "start" ]; then
		ocf_log notice "Activating $lv_path"
		lvchange --addtag $tag $lv_path
		if [ $? -ne 0 ]; then
			ocf_log err "Unable to add tag to $lv_path"
			return $OCF_ERR_GENERIC
		fi
		lvchange -ay $lv_path
		if [ $? -ne 0 ]; then
			ocf_log err "Unable to activate $lv_path"
			return $OCF_ERR_GENERIC
		fi
	else
		ocf_log notice "Deactivating $lv_path"
		lvchange -an $lv_path
		if [ $? -ne 0 ]; then
			ocf_log err "Unable to deactivate $lv_path"
			return $OCF_ERR_GENERIC
		fi

		ocf_log notice "Removing ownership tag ($tag) from $lv_path"

		lvchange --deltag $tag $lv_path
		if [ $? -ne 0 ]; then
			ocf_log err "Unable to delete tag from $lv_path"
			return $OCF_ERR_GENERIC
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
	declare owner=`lvs -o tags --noheadings $lv_path`
	declare my_name=$(local_node_name)

	if [ -z $my_name ]; then
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
		if [ ! -z `lvs -o tags --noheadings $lv_path` ]; then
			ocf_log err "Failed to steal $lv_path from $owner."
			return $OCF_ERR_GENERIC
		fi
	fi

	if ! lv_activate_and_tag $1 $my_name $lv_path; then
		ocf_log err "Failed to $1 $lv_path"

		if [ "$1" == "start" ]; then
			ocf_log notice "Attempting cleanup of $OCF_RESKEY_vg_name"

			if vgreduce --removemissing --config \
			    "activation { volume_list = \"$OCF_RESKEY_vg_name\" }" \
			    $OCF_RESKEY_vg_name; then
				ocf_log notice "$OCF_RESKEY_vg_name now consistent"
				owner=`lvs -o tags --noheadings $lv_path`
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
					if [ ! -z `lvs -o tags --noheadings $lv_path` ]; then
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
				ocf_log err "Failed to make $OCF_RESKEY_vg_name consistent"
				return $OCF_ERR_GENERIC
			fi
		else
			ocf_log err "Failed to $1 $lv_path"
			return $OCF_ERR_GENERIC
		fi
	fi
	return $OCF_SUCCESS
}

case $1 in
start)
	if [ -z $OCF_RESKEY_lv_name ]; then
		vg_activate start || exit 1
	else
		lv_activate start || exit 1
	fi
	rv=0
	;;

status|monitor)
	if [ -z $OCF_RESKEY_lv_name ]; then
		vg_status || exit 1
	else
		lv_status || exit 1
	fi
	rv=0
	;;
		    
stop)
	if [ -z $OCF_RESKEY_lv_name ]; then
		vg_activate stop || exit 1
	else
		lv_activate stop || exit 1
	fi
	rv=0
	;;

recover|restart)
	$0 stop || exit $OCF_ERR_GENERIC
	$0 start || exit $OCF_ERR_GENERIC
	rv=0
	;;

meta-data)
	meta_data
	rv=0
	;;

validate-all)
	verify_all
	rv=$?
	;;
*)
	echo "usage: $0 {start|status|monitor|stop|restart|meta-data|validate-all}"
	exit $OCF_ERR_UNIMPLEMENTED
	;;
esac

exit $rv
