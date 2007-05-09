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

# lvm_exec_resilient
#
# Sometimes, devices can come back.  Their metadata will conflict
# with the good devices that remain.  This function filters out those
# failed devices when executing the given command
#
# Finishing with vgscan resets the cache/filter
lvm_exec_resilient()
{
	declare command=$1
	declare all_pvs

	ocf_log notice "Making resilient : $command"

	if [ -z $command ]; then
		ocf_log err "lvm_exec_resilient: Arguments not supplied"
		return $OCF_ERR_ARGS
	fi

	# pvs will print out only those devices that are valid
	# If a device dies and comes back, it will not appear
	# in pvs output (but you will get a Warning).
	all_pvs=(`pvs --noheadings -o pv_name | grep -v Warning`)

	# Now we use those valid devices in a filter which we set up.
	# The device will then be activated because there are no
	# metadata conflicts.
        command=$command" --config devices{filter=[";
	for i in ${all_pvs[*]}; do
		command=$command'"a|'$i'|",'
	done
	command=$command"\"r|.*|\"]}"

	ocf_log notice "Resilient command: $command"
	if ! $command ; then
		ocf_log err "lvm_exec_resilient failed"
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

	if [ -z $action ] || [ -z $lv_path ]; then
		ocf_log err "lv_activate_resilient: Arguments not supplied"
		return $OCF_ERR_ARGS
	fi

	if [ $action != "start" ]; then
	        op="-an"
	fi

	if ! lvm_exec_resilient "lvchange $op $lv_path" ; then
		ocf_log err "lv_activate_resilient $action failed on $lv_path"
		return $OCF_ERR_GENERIC
	else
		return $OCF_SUCCESS
	fi
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

	if [[ $(vgs -o attr --noheadings $OCF_RESKEY_vg_name) =~ .....c ]]; then
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

		lv_activate_resilient "stop" $lv_path
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

		if ! lv_activate_resilient $action $lv_path; then
			ocf_log err "Unable to activate $lv_path"
			return $OCF_ERR_GENERIC
		fi
	else
		ocf_log notice "Deactivating $lv_path"
		if ! lv_activate_resilient $action $lv_path; then
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

ha_lvm_proper_setup_check()
{
	# First, let's check that they have setup their lvm.conf correctly
	if ! lvm dumpconfig activation/volume_list >& /dev/null ||
	   ! lvm dumpconfig activation/volume_list | grep $(local_node_name); then
		ocf_log err "lvm.conf improperly configured for HA LVM."
		return $OCF_ERR_GENERIC
	fi

	# Next, we need to ensure that their initrd has been updated
	if [ -e /boot/initrd-`uname -r`.img ]; then
		if [ "$(find /boot/initrd-`uname -r`.img -newer /etc/lvm/lvm.conf)" == "" ]; then
			ocf_log err "HA LVM requires the initrd image to be newer than lvm.conf"
			return $OCF_ERR_GENERIC
		fi
	else
		# Best guess...
		if [ "$(find /boot/*.img -newer /etc/lvm/lvm.conf)" == "" ]; then
			ocf_log err "HA LVM requires the initrd image to be newer than lvm.conf"
			return $OCF_ERR_GENERIC
		fi
	fi

	return $OCF_SUCCESS
}

case $1 in
start)
	if [[ $(vgs -o attr --noheadings $OCF_RESKEY_vg_name) =~ .....c ]]; then
		ocf_log notice "$OCF_RESKEY_vg_name is a cluster volume.  Ignoring..."
		exit 0
	fi

	ha_lvm_proper_setup_check || exit 1
		
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
	if [[ $(vgs -o attr --noheadings $OCF_RESKEY_vg_name) =~ .....c ]]; then
		ocf_log notice "$OCF_RESKEY_vg_name is a cluster volume.  Ignoring..."
		exit 0
	fi

	if ! ha_lvm_proper_setup_check; then
		ocf_log err "WARNING: An improper setup can cause data corruption!"
	fi

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
	if [[ $(vgs -o attr --noheadings $OCF_RESKEY_vg_name) =~ .....c ]]; then
		ocf_log notice "$OCF_RESKEY_vg_name is a cluster volume.  Ignoring..."
		exit 0
	fi

	verify_all
	rv=$?
	;;
*)
	echo "usage: $0 {start|status|monitor|stop|restart|meta-data|validate-all}"
	exit $OCF_ERR_UNIMPLEMENTED
	;;
esac

exit $rv
