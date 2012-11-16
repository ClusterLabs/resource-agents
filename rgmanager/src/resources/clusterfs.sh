#!/bin/bash

#
# Cluster File System mount/umount/fsck/etc. agent
#
# Copyright (C) 2000 Mission Critical Linux
# Copyright (C) 2002-2011 Red Hat, Inc.  All rights reserved.
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
<resource-agent name="clusterfs" version="rgmanager 2.0">
    <version>1.0</version>

    <longdesc lang="en">
        This defines a cluster file system mount (i.e. GFS)
    </longdesc>
    <shortdesc lang="en">
        Defines a cluster file system mount.
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

        <parameter name="device" unique="1" required="1">
	    <longdesc lang="en">
	        Block device, file system label, or UUID of file system.
	    </longdesc>
            <shortdesc lang="en">
                Device or Label
            </shortdesc>
	    <content type="string"/>
        </parameter>

        <parameter name="fstype">
	    <longdesc lang="en">
	        File system type.  If not specified, mount(8) will attempt to
		determine the file system type.
	    </longdesc>
            <shortdesc lang="en">
                File system type
            </shortdesc>
	    <content type="string"/>
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

	<parameter name="fsid">
	    <longdesc lang="en">
	    	File system ID for NFS exports.  This can be overridden
		in individual nfsclient entries.
	    </longdesc>
	    <shortdesc lang="en">
	    	NFS File system ID
	    </shortdesc>
	    <content type="string"/>
	</parameter>

	<parameter name="nfslock" inherit="service%nfslock">
	    <longdesc lang="en">
	        If set, the node will try to kill lockd and issue 
		reclaims across all remaining network interface cards.
		This happens always, regardless of unmounting failed.
	    </longdesc>
	    <shortdesc lang="en">
	        Enable NFS lock workarounds
	    </shortdesc>
	    <content type="boolean"/>
	</parameter>

	<parameter name="nfsrestart">
	    <longdesc lang="en">
		If set and unmounting the file system fails, the node will
		try to restart nfs daemon and nfs lockd to drop all filesystem
		references. Use this option as last resource.
		This option requires force_unmount to be set and it is not
		compatible with nfsserver resource.
	    </longdesc>
	    <shortdesc lang="en">
		Enable NFS daemon and lockd workaround
	    </shortdesc>
	    <content type="boolean"/>
	</parameter>

        <parameter name="options">
            <longdesc lang="en">
                Options used when the file system is mounted.  These
                are often file-system specific.  See mount(8) and/or
                mount.gfs2(8) for supported mount options.
            </longdesc>
            <shortdesc lang="en">
                Mount Options
            </shortdesc>
            <content type="string"/>
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
    	<child type="fs" start="1" stop="3"/>
    	<child type="clusterfs" start="1" stop="3"/>
        <child type="nfsexport" start="3" stop="1"/>
    </special>
</resource-agent>
EOT
}


verify_fstype()
{
	# Auto detect?
	[ -z "$OCF_RESKEY_fstype" ] && return $OCF_SUCCESS

	case $OCF_RESKEY_fstype in
	gfs|gfs2)
		return $OCF_SUCCESS
		;;
	*)
		ocf_log err "File system type $OCF_RESKEY_fstype not supported"
		return $OCF_ERR_ARGS
		;;
	esac
}


verify_options()
{
	declare -i ret=$OCF_SUCCESS

	#
	# From mount(8)
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
		gfs)
			case $o in
				lockproto=*|locktable=*|hostdata=*)
					continue;
					;;
				localcaching|localflocks|ignore_local_fs)
					continue;
					;;
				num_glockd|acl|suiddir)	
					continue
					;;
			esac
			;;
		gfs2)
			# XXX
			continue
			;;
		esac


		ocf_log err "Option $o not supported for $OCF_RESKEY_fstype"
		ret=$OCF_ERR_ARGS
	done

	return $ret
}


do_verify()
{
	verify_name || return $OCF_ERR_ARGS
	verify_fstype || return $OCF_ERR_ARGS
	verify_device || return $OCF_ERR_ARGS
	verify_mountpoint || return $OCF_ERR_ARGS
	verify_options || return $OCF_ERR_ARGS
}


do_pre_unmount() {
	#
	# Check the rgmanager-supplied reference count if one exists.
	# If the reference count is <= 1, we can safely proceed
	#
	if [ -n "$OCF_RESKEY_RGMANAGER_meta_refcnt" ]; then
		refs=$OCF_RESKEY_RGMANAGER_meta_refcnt
		if [ $refs -gt 0 ]; then
			ocf_log debug "Not unmounting $OCF_RESOURCE_INSTANCE - still in use by $refs other service(s)"
			return 2
		fi
	fi

	if [ -z "$force_umount" ]; then
		ocf_log debug "Not umounting $dev (clustered file system)"
		return 2
	fi

	#
	# Always do this hackery on clustered file systems.
	#
	if [ "$OCF_RESKEY_nfslock" = "yes" ] || \
	   [ "$OCF_RESKEY_nfslock" = "1" ]; then
		ocf_log warning "Dropping node-wide NFS locks"
		mkdir -p $mp/.clumanager/statd
		pkill -KILL -x lockd
		# Copy out the notify list; our 
		# IPs are already torn down
		if notify_list_store $mp/.clumanager/statd; then
			notify_list_broadcast $mp/.clumanager/statd
		fi
	fi

	# Always invalidate buffers on clusterfs resources
	clubufflush -f $dev

	return 0
}

do_force_unmount() {
	if [ "$OCF_RESKEY_nfsrestart" = "yes" ] || \
	   [ "$OCF_RESKEY_nfsrestart" = "1" ]; then
		ocf_log warning "Restarting nfsd/nfslock"
		nfsexports=$(cat /var/lib/nfs/etab)
		service nfslock stop
		service nfs stop
		service nfs start
		service nfslock start
		echo "$nfsexports" | { while read line; do
			nfsexp=$(echo $line | awk '{print $1}')
			nfsopts=$(echo $line | sed -e 's#.*(##g' -e 's#).*##g')
			nfsacl=$(echo $line | awk '{print $2}' | sed -e 's#(.*##g')
			if [ -n "$nfsopts" ]; then
				exportfs -i -o "$nfsopts" "$nfsacl":$nfsexp
			else
				exportfs -i "$nfsacl":$nfsexp
			fi
		done; }
	fi
	return 1
}

main $*
