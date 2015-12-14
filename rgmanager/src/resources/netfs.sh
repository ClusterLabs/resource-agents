#!/bin/bash

#
# NFS/CIFS file system mount/umount/etc. agent
#
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

. $(dirname $0)/utils/fs-lib.sh

do_metadata()
{
	cat <<EOT
<?xml version="1.0" ?>
<!DOCTYPE resource-agent SYSTEM "ra-api-1-modified.dtd">
<resource-agent name="netfs" version="rgmanager 2.0">
    <version>1.0</version>

    <longdesc lang="en">
        This defines an NFS/CIFS mount for use by cluster services.
    </longdesc>
    <shortdesc lang="en">
        Defines an NFS/CIFS file system mount.
    </shortdesc>

    <parameters>
        <parameter name="name" primary="1">
	    <longdesc lang="en">
	        Symbolic name for this file system.
	    </longdesc>
            <shortdesc lang="en">
                File System Name
            </shortdesc>
	    <content type="string"/>
        </parameter>

        <parameter name="mountpoint" unique="1" required="1">
	    <longdesc lang="en">
	        Path in file system heirarchy to mount this file system.
	    </longdesc>
            <shortdesc lang="en">
                Mount Point
            </shortdesc>
	    <content type="string"/>
        </parameter>

        <parameter name="host" required="1">
	    <longdesc lang="en">
	    	Server IP address or hostname
	    </longdesc>
            <shortdesc lang="en">
	    	IP or Host
            </shortdesc>
	    <content type="string"/>
        </parameter>

        <parameter name="export" required="1">
	    <longdesc lang="en">
	    	NFS Export directory name or CIFS share
	    </longdesc>
            <shortdesc lang="en">
	    	Export
            </shortdesc>
	    <content type="string"/>
        </parameter>

        <parameter name="fstype">
	    <longdesc lang="en">
	    	File System type (nfs, nfs4 or cifs)
	    </longdesc>
            <shortdesc lang="en">
	    	File System Type
            </shortdesc>
	    <content type="string" default="nfs"/>
        </parameter>

        <parameter name="no_unmount" required="0">
	    <longdesc lang="en">
	    	Do not unmount the filesystem during a stop or relocation operation
	    </longdesc>
            <shortdesc lang="en">
	    	Skip unmount opration
            </shortdesc>
	    <content type="boolean"/>
        </parameter>

        <parameter name="force_unmount">
            <longdesc lang="en">
                If set, the cluster will kill all processes using 
                this file system when the resource group is 
                stopped.  Otherwise, the unmount will fail, and
                the resource group will be restarted.
            </longdesc>
            <shortdesc lang="en">
                Force Unmount
            </shortdesc>
	    <content type="boolean"/>
        </parameter>

        <parameter name="self_fence">
            <longdesc lang="en">
                If set and unmounting the file system fails, the node will
                immediately reboot.  Generally, this is used in conjunction
                with force_unmount support, but it is not required.
            </longdesc>
            <shortdesc lang="en">
                Seppuku Unmount
            </shortdesc>
            <content type="boolean"/>
        </parameter>

        <parameter name="options">
            <longdesc lang="en">
	    	Provides a list of mount options.  If none are specified,
		the NFS file system is mounted -o sync.
            </longdesc>
            <shortdesc lang="en">
                Mount Options
            </shortdesc>
	    <content type="string"/>
        </parameter>

        <parameter name="use_findmnt">
            <longdesc lang="en">
        Use findmnt to determine if and where a filesystem is mounted.
        Disabling this uses the failback method (should be used if autofs
        maps are located on network storage (ie. nfs, iscsi, etc).
            </longdesc>
            <shortdesc lang="en">
        Utilize findmnt to detect if and where filesystems are mounted
            </shortdesc>
            <content type="boolean"/>
        </parameter>

    </parameters>

    <actions>
        <action name="start" timeout="900"/>
	<action name="stop" timeout="30"/>
	<!-- Recovery isn't possible; we don't know if resources are using
	     the file system. -->

	<!-- Checks to see if it's mounted in the right place -->
	<action name="status" interval="1m" timeout="10"/>
	<action name="monitor" interval="1m" timeout="10"/>

	<!-- Checks to see if we can read from the mountpoint -->
	<action name="status" depth="10" timeout="30" interval="5m"/>
	<action name="monitor" depth="10" timeout="30" interval="5m"/>

	<!-- Checks to see if we can write to the mountpoint (if !ROFS) -->
	<action name="status" depth="20" timeout="30" interval="10m"/>
	<action name="monitor" depth="20" timeout="30" interval="10m"/>

	<action name="meta-data" timeout="5"/>
	<action name="validate-all" timeout="5"/>
    </actions>

    <special tag="rgmanager">
        <child type="nfsexport" forbid="1"/>
        <child type="nfsclient" forbid="1"/>
    </special>
</resource-agent>
EOT
}


verify_host()
{
	if [ -z "$OCF_RESKEY_host" ]; then
	       ocf_log err "No server hostname or IP address specified."
	       return 1
	fi

	host $OCF_RESKEY_host 2>&1 | grep -vq "not found"
	if [ $? -eq 0 ]; then
		return 0
	fi

	ocf_log err "Hostname or IP address \"$OCF_RESKEY_host\" not valid"

	return $OCF_ERR_ARGS
}


verify_fstype()
{
	# Auto detect?
	[ -z "$OCF_RESKEY_fstype" ] && return 0

	case $OCF_RESKEY_fstype in
	nfs|nfs4|cifs)
		return 0
		;;
	*)
		ocf_log err "File system type $OCF_RESKEY_fstype not supported"
		return $OCF_ERR_ARGS
		;;
	esac
}


verify_options()
{
	declare -i ret=0

	#
	# From mount(1)
	#
	for o in `echo $OCF_RESKEY_options | sed -e s/,/\ /g`; do
		case $o in
		async|atime|auto|defaults|dev|exec|_netdev|noatime)
			continue
			;;
		noauto|nodev|noexec|nosuid|nouser|ro|rw|suid|sync)
			continue
			;;
		dirsync|user|users)
			continue
			;;
		esac

		case $OCF_RESKEY_fstype in
		cifs)
			continue
			;;
		nfs|nfs4)
			case $o in
			#
			# NFS / NFS4 common
			#
			rsize=*|wsize=*|timeo=*|retrans=*|acregmin=*)
				continue
				;;
			acregmax=*|acdirmin=*|acdirmax=*|actimeo=*)
				continue
				;;
			retry=*|port=*|bg|fg|soft|hard|intr|cto|ac|noac)
				continue
				;;
			esac

			#
			# NFS v2/v3 only
			#
			if [ "$OCF_RESKEY_fstype" = "nfs" ]; then
				case $o in
				mountport=*|mounthost=*)
					continue
					;;
				mountprog=*|mountvers=*|nfsprog=*|nfsvers=*)
					continue
					;;
				namelen=*)
					continue
					;;
				tcp|udp|lock|nolock)
					continue
					;;
				esac
			fi

			#
			# NFS4 only
			#
			if [ "$OCF_RESKEY_fstype" = "nfs4" ]; then
				case $o in
				proto=*|clientaddr=*|sec=*)
					continue
					;;
				esac
			fi

			;;
		esac

		ocf_log err "Option $o not supported for $OCF_RESKEY_fstype"
		ret=$OCF_ERR_ARGS
	done

	return $ret
}


do_validate()
{
	verify_name || return $OCF_ERR_ARGS
	verify_fstype|| return $OCF_ERR_ARGS
	verify_host || return $OCF_ERR_ARGS
	verify_mountpoint || return $OCF_ERR_ARGS
	verify_options || return $OCF_ERR_ARGS
	# verify_target || return $OCF_ERR_ARGS
}


#
# Override real_device to use fs-lib's functions for start/stop_filesystem
#
real_device() {
	export REAL_DEVICE="$1"
}


#
# do_mount - nfs / cifs are mounted differently than blockdevs
#
do_mount() {
	declare opts=""
	declare mount_options=""
	declare ret_val
	declare mp="$OCF_RESKEY_mountpoint"

	#
	# Get the filesystem type, if specified.
	#
	fstype_option=""
	fstype=${OCF_RESKEY_fstype}
		case "$fstype" in
	""|"[ 	]*")
		fstype=""
		;;
	*)	# found it
		fstype_option="-t $fstype"
		;;
	esac

	#
	# Get the mount options, if they exist.
	#
	mount_options=""
	opts=${OCF_RESKEY_options}
	case "$opts" in 
	""|"[ 	]*")
		opts=""
		;;
	*)	# found it
		mount_options="-o $opts"
		;;
	esac

        case $OCF_RESKEY_fstype in
	nfs|nfs4)
		mount -t $OCF_RESKEY_fstype $mount_options $OCF_RESKEY_host:"$OCF_RESKEY_export" "$mp"
		;;
	cifs)
		mount -t $OCF_RESKEY_fstype $mount_options //$OCF_RESKEY_host/"$OCF_RESKEY_export" "$mp"
		;;
	esac

	ret_val=$?
	if [ $ret_val -ne 0 ]; then
		ocf_log err "\
'mount $fstype_option $mount_options $OCF_RESKEY_host:$OCF_RESKEY_export $mp' failed, error=$ret_val"
		return 1
	fi
	
	return 0
}

do_nfs_rpc_check() {
	# see man nfs TRANSPORT PROTOCOL section for defaults
	local nfsproto=tcp
	local nfsmountproto=udp

	# follow the same logic as mount.nfs option parser.
	# the rightmost option wins over previous ones, so don't break when
	# we find something.

	for o in $(echo ${OCF_RESKEY_options} | sed -e s/,/\ /g); do
		if echo $o | grep -q "^proto=" ; then
			nfsproto="$(echo $o | cut -d "=" -f 2)"
		fi
		if echo $o | grep -q "^mountproto=" ; then
			nfsmountproto="$(echo $o | cut -d "=" -f 2)"
		fi
		case $o in
		tcp)	nfsproto=tcp;;
		udp)	nfsproto=udp;;
		rdma)	nfsproto=rdma;;
		esac
	done

	ocf_log debug "Testing generic rpc access on server ${OCF_RESKEY_host} with protocol $nfsproto"
	if ! rpcinfo -T $nfsproto ${OCF_RESKEY_host} > /dev/null 2>&1; then
		ocf_log alert "RPC server on ${OCF_RESKEY_host} with $nfsproto is not responding"
		return 1
	fi

	ocf_log debug "Testing nfs rcp access on server ${OCF_RESKEY_host} with protocol $nfsproto"
	if ! rpcinfo -T $nfsproto ${OCF_RESKEY_host} nfs > /dev/null 2>&1; then
		ocf_log alert "NFS server on ${OCF_RESKEY_host} with $nfsproto is not responding"
		return 1 
	fi

	if [ "$OCF_RESKEY_fstype" = nfs ]; then
		ocf_log debug "Testing mountd rpc access on server ${OCF_RESKEY_host} with protocol $nfsmountproto"
		if ! rpcinfo -T $nfsmountproto ${OCF_RESKEY_host} mountd; then
			ocf_log alert "MOUNTD server on ${OCF_RESKEY_host} with $nfsmountproto is not responding"
			return 1
		fi
	fi

	return 0
}

do_pre_unmount() {
	case $OCF_RESKEY_fstype in
	nfs|nfs4)
		if [ "$self_fence" != $YES ]; then
			ocf_log debug "Skipping pre unmount checks: self_fence is disabled"
			return 0
		fi

		is_mounted "$dev" "$mp"
		case $? in
		$NO)
			ocf_log debug "Skipping pre unmount checks: device is not mounted"
			return 0
			;;
		esac

		ocf_log info "pre unmount: checking if nfs server ${OCF_RESKEY_host} is alive"
		if ! do_nfs_rpc_check; then
			ocf_log alert "NFS server not responding - REBOOTING"
			sleep 2
			reboot -fn
		fi
		;;
	esac

	return 0
}

do_force_unmount() {
        case $OCF_RESKEY_fstype in
	nfs|nfs4)
		ocf_log warning "Calling 'umount -f $mp'"
		umount -f "$OCF_RESKEY_mountpoint"
		return $?
		;;
	*)
		;;
	esac

	return 1	# Returning 1 lets stop_filesystem do add'l checks
}


populate_defaults()
{
        case $OCF_RESKEY_fstype in
	nfs|nfs4)
		export OCF_RESKEY_device="$OCF_RESKEY_host:$OCF_RESKEY_export"
		if [ -z "$OCF_RESKEY_options" ]; then
			export OCF_RESKEY_options=sync,soft,noac
		fi
		;;
	cifs)
		export OCF_RESKEY_device="//$OCF_RESKEY_host/$OCF_RESKEY_export"
		if [ -z "$OCF_RESKEY_options" ]; then
			export OCF_RESKEY_options=guest
		fi
		;;
	esac
}


#
# Main...
#
populate_defaults
main $*
