#!/bin/bash

#
# File system (normal) mount/umount/fsck/etc. agent
#

LC_ALL=C
LANG=C
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export LC_ALL LANG PATH

#
# XXX todo - search and replace on these
#
SUCCESS=0
FAIL=2
YES=0
NO=1
YES_STR="yes"

# Grab nfs lock tricks if available
export NFS_TRICKS=1
if [ -f "$(dirname $0)/svclib_nfslock" ]; then
	. $(dirname $0)/svclib_nfslock
	NFS_TRICKS=0
else
	unset OCF_RESKEY_nfslock
fi


. $(dirname $0)/ocf-shellfuncs


meta_data()
{
	cat <<EOT
<?xml version="1.0" ?>
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

        <parameter name="options">
            <longdesc lang="en">
                If set, the file system will be checked (even if
                it is a journalled file system).  This option is
                ignored for non-journalled file systems such as
                ext2.
            </longdesc>
            <shortdesc lang="en">
                Mount Options
            </shortdesc>
	    <content type="string"/>
        </parameter>

	<parameter name="self_fence">
	    <longdesc lang="en">
	        If set and unmounting the file system fails, the node will
		immediately reboot.  Generally, this is used in conjunction
		with force-unmount support, but it is not required.
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


verify_name()
{
	[ -n "$OCF_RESKEY_name" ] || exit $OCF_ERR_ARGS
}


verify_mountpoint()
{
	if [ -z "$OCF_RESKEY_mountpoint" ]; then
		ocf_log err "No mount point specified."
		return $OCF_ERR_ARGS
	fi

	if ! [ -e "$OCF_RESKEY_mountpoint" ]; then
		ocf_log info "Mount point $OCF_RESKEY_mountpoint will be "
			     "created at mount time."
		return 0
	fi

	[ -d "$OCF_RESKEY_mountpoint" ] && return 0

	ocf_log err "$OCF_RESKEY_mountpoint is not a directory"
	return $OCF_ERR_ARGS
}


real_device()
{
	declare dev=$1
	declare realdev

	[ -z "$dev" ] && return 1

	if [ -h "$dev" ]; then 
		realdev=$(readlink -f $dev)
		if [ $? -ne 0 ]; then
			return 1
		fi
		echo $realdev
		return 0
	fi

	if [ -b "$dev" ]; then
		echo $dev
	       	return 0
	fi
		
	realdev=$(findfs $dev 2> /dev/null) 
	if [ -n "$realdev" ] && [ -b "$realdev" ]; then
		echo $realdev
		return 0
	fi

	return 1
}


verify_device()
{
	declare realdev

	if [ -z "$OCF_RESKEY_device" ]; then
	       ocf_log err "No device or label specified."
	       return $OCF_ERR_ARGS
	fi

	realdev=$(real_device $OCF_RESKEY_device)
	if [ -n "$realdev" ]; then
		if [ "$realdev" != "$OCF_RESKEY_device" ]; then
			echo "Specified $OCF_RESKEY_device maps to $realdev"
		fi
		return $OCF_SUCCESS
	fi

	ocf_log "Device or label \"$OCF_RESKEY_device\" not valid"

	return $OCF_ERR_ARGS
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


verify_all()
{
	verify_name || return $OCF_ERR_ARGS
	verify_fstype || return $OCF_ERR_ARGS
	verify_device || return $OCF_ERR_ARGS
	verify_mountpoint || return $OCF_ERR_ARGS
	verify_options || return $OCF_ERR_ARGS
}


#
# mountInUse device mount_point
#
# Check to see if either the device or mount point are in use anywhere on
# the system.  It is not required that the device be mounted on the named
# moint point, just if either are in use.
#
mountInUse () {
	typeset mp tmp_mp
	typeset dev tmp_dev
	typeset junk

	if [ $# -ne 2 ]; then
		ocf_log err "Usage: mountInUse device mount_point".
		return $FAIL
	fi

	dev=$1
	mp=$2

	while read tmp_dev tmp_mp junk; do
		if [ -n "$tmp_dev" -a "$tmp_dev" = "$dev" ]; then
			return $YES
		fi
		
		if [ -n "$tmp_mp" -a "$tmp_mp" = "$mp" ]; then
			return $YES
		fi
	done < <(mount | awk '{print $1,$3}')

	return $NO
}


#
# isMounted device mount_point
#
# Check to see if the device is mounted.  Print a warning if its not
# mounted on the directory we expect it to be mounted on.
#
isMounted () {

	typeset mp tmp_mp
	typeset dev tmp_dev

	if [ $# -ne 2 ]; then
		ocf_log err "Usage: isMounted device mount_point"
		return $FAIL
	fi

	dev=$(real_device $1)
	if [ -z "$dev" ]; then
		ocf_log err "isMounted: Could not match $1 with a real device"
		return $FAIL
	fi
	mp=$(readlink -f $2)
	
	while read tmp_dev tmp_mp
	do
		#echo "spec=$1 dev=$dev  tmp_dev=$tmp_dev"
		tmp_dev=$(real_device $tmp_dev)

		if [ -n "$tmp_dev" -a "$tmp_dev" = "$dev" ]; then
			#
			# Check to see if its mounted in the right
			# place
			#
			if [ -n "$tmp_mp"  -a "$tmp_mp"  != "$mp" ]; then
				ocf_log warn "\
Device $dev is mounted on $tmp_mp instead of $mp"
			fi
			return $YES
		fi
	done < <(mount | awk '{print $1,$3}')

	return $NO
}


# 
# isAlive mount_point
# 
# Check to see if mount_point is alive (testing read/write)
# 
isAlive()
{
	declare errcode
	declare mount_point
	declare file=".writable_test.$(hostname)"
	declare rw
	
	if [ $# -ne 1 ]; then
	        ocf_log err "Usage: isAlive mount_point"
		return $FAIL
	fi
	mount_point=$1
	
	test -d $mount_point
	if [ $? -ne 0 ]; then
		ocf_log err "$mount_point is not a directory"
		return $FAIL
	fi
	
	[ $OCF_CHECK_LEVEL -lt 10 ] && return $YES
	
	# depth 10 test (read test)
	ls $mount_point > /dev/null 2> /dev/null
	errcode=$?
	if [ $errcode -ne 0 ]; then
		ocf_log err "clusterfs:${OCF_RESKEY_name}: isAlive failed read test on [$mount_point]. Return code: $errcode"
		return $NO
	fi
	
	[ $OCF_CHECK_LEVEL -lt 20 ] && return $YES
	
	# depth 20 check (write test)
	rw=$YES
	for o in `echo $OCF_RESKEY_options | sed -e s/,/\ /g`; do
                if [ "$o" = "ro" ]; then
		        rw=$NO
                fi
	done
	if [ $rw -eq $YES ]; then
	        file=$mount_point/$file
		while true; do
			if [ -e "$file" ]; then
				file=${file}_tmp
				continue
			else
			        break
			fi
		done
		touch $file > /dev/null 2> /dev/null
		errcode=$?
		if [ $errcode -ne 0 ]; then
			ocf_log err "clusterfs:${OCF_RESKEY_name}: isAlive failed write test on [$mount_point]. Return code: $errcode"
			return $NO
		fi
		rm -f $file > /dev/null 2> /dev/null
	fi
	
	return $YES
}


#
# killMountProcesses device mount_point
#
# Using lsof or fuser try to unmount the mount by killing of the processes
# that might be keeping it busy.
#
killMountProcesses()
{
	typeset -i ret=$SUCCESS
	typeset have_lsof=""
	typeset have_fuser=""
	typeset try

	if [ $# -ne 1 ]; then
		ocf_log err \
			"Usage: killMountProcesses mount_point"
		return $FAIL
	fi

	typeset mp=$1

	ocf_log notice "Forcefully unmounting $mp"

	#
	# Not all distributions have lsof.  If not use fuser.  If it
	# does, try both.
  	#
	file=$(which lsof 2>/dev/null)
	if [ -f "$file" ]; then
		have_lsof=$YES
	fi

	file=$(which fuser 2>/dev/null)
	if [ -f "$file" ]; then
		have_fuser=$YES
	fi             

	if [ -z "$have_lsof" -a -z "$have_fuser" ]; then
		ocf_log warn \
	"Cannot forcefully unmount $mp; cannot find lsof or fuser commands"
		return $FAIL
	fi

	for try in 1 2 3; do
		if [ -n "$have_lsof" ]; then
			#
			# Use lsof to free up mount point
			#
	    		while read command pid user
			do
				if [ -z "$pid" ]; then
					continue
				fi

				if [ $try -eq 1 ]; then
					ocf_log warn \
			 	  "killing process $pid ($user $command $mp)"
				elif [ $try -eq 3 ]; then
					ocf_log crit \
		    		  "Could not clean up mountpoint $mp"
				ret=$FAIL
				fi

				if [ $try -gt 1 ]; then
					kill -9 $pid
				else
					kill -TERM $pid
				fi
			done < <(lsof -b 2>/dev/null | \
			    grep -E "$mp(/.*|)\$" | \
			    awk '{print $1,$2,$3}' | \
			    sort -u -k 1,3)
		elif [ -n "$have_fuser" ]; then
			#
			# Use fuser to free up mount point
			#
			while read command pid user
			do
				if [ -z "$pid" ]; then
					continue
				fi

				if [ $try -eq 1 ]; then
					ocf_log warn \
			 	  "killing process $pid ($user $command $mp)"
				elif [ $try -eq 3 ]; then
					ocf_log crit \
				    "Could not clean up mount point $mp"
					ret=$FAIL
				fi

				if [ $try -gt 1 ]; then
					kill -9 $pid
				else
					kill -TERM $pid
				fi
			done < <(fuser -vm $mp 2>&1 | \
			    grep -v PID | \
			    sed 's;^'$mp:';;' | \
			    awk '{print $4,$2,$1}' | \
			    sort -u -k 1,3)
		fi
	done

	return $ret
}

#
# startFilesystem
#
startFilesystem() {
	typeset -i ret_val=$SUCCESS
	typeset mp=""			# mount point
	typeset dev=""			# device
	typeset fstype=""
	typeset opts=""
	typeset device_in_use=""
	typeset mount_options=""

	#
	# Get the mount point, if it exists.  If not, no need to continue.
	#
	mp=${OCF_RESKEY_mountpoint}
	case "$mp" in 
      	""|"[ 	]*")		# nothing to mount
    		return $SUCCESS
    		;;
	/*)			# found it
	  	;;
	*)	 		# invalid format
			ocf_log err \
"startFilesystem: Invalid mount point format (must begin with a '/'): \'$mp\'"
	    	return $FAIL
	    	;;
	esac
	
	#
	# Get the device
	#
	dev=$(real_device $OCF_RESKEY_device)
	if [ -z "$dev" ]; then
			ocf_log err "\
startFilesystem: Could not match $OCF_RESKEY_device with a real device"
			return $FAIL
	fi

	#
	# Ensure we've got a valid directory
	#
	if [ -e "$mp" ]; then
		if ! [ -d "$mp" ]; then
			ocf_log err "\
startFilesystem: Mount point $mp exists but is not a directory"
			return $FAIL
		fi
	else
		ocf_log info "\
startFilesystem: Creating mount point $mp for device $dev"
		mkdir -p $mp
	fi

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
	# See if the device is already mounted.
	# 
	isMounted $dev $mp
	case $? in
	$YES)		# already mounted
		ocf_log debug "$dev already mounted"
		return $SUCCESS
		;;
	$NO)		# not mounted, continue
		;;
	$FAIL)
		return $FAIL
		;;
	esac


	#
	# Make sure that neither the device nor the mount point are mounted
	# (i.e. they may be mounted in a different location).  The'mountInUse'
	# function checks to see if either the device or mount point are in
	# use somewhere else on the system.
	#
	mountInUse $dev $mp
	case $? in
	$YES)		# uh oh, someone is using the device or mount point
		ocf_log err "\
Cannot mount $dev on $mp, the device or mount point is already in use!"
		return $FAIL
		;;
	$NO)		# good, no one else is using it
		;;
	$FAIL)
		return $FAIL
		;;
	*)
		ocf_log err "Unknown return from mountInUse"
		return $FAIL
		;;
	esac

	#
	# Make sure the mount point exists.
	#
	if [ ! -d $mp ]; then
		rm -f $mp			# rm in case its a plain file
		mkdir -p $mp			# create the mount point
		ret_val=$?
		if [ $ret_val -ne 0 ]; then
			ocf_log err \
				"'mkdir -p $mp' failed, error=$ret_val"
			return $FAIL
		fi
	fi

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

	#
	# Mount the device
	#
	ocf_log debug "mount $fstype_option $mount_options $dev $mp"
	mount $fstype_option $mount_options $dev $mp
	ret_val=$?
	if [ $ret_val -ne 0 ]; then
		ocf_log err "\
'mount $fstype_option $mount_options $dev $mp' failed, error=$ret_val"
		return $FAIL
	fi
	
	return $SUCCESS
}


#
# stopFilesystem serviceID deviceID
#
# Run the stop actions
#
stopFilesystem() {
	typeset -i ret_val=0
	typeset -i try=1
	typeset -i max_tries=3		# how many times to try umount
	typeset -i sleep_time=2		# time between each umount failure
	typeset -i refs=0
	typeset done=""
	typeset umount_failed=""
	typeset force_umount=""
	typeset self_fence=""
	typeset fstype=""


	#
	# Get the mount point, if it exists.  If not, no need to continue.
	#
	mp=${OCF_RESKEY_mountpoint}
	case "$mp" in 
      	""|"[ 	]*")		# nothing to mount
    		return $SUCCESS
    		;;
	/*)			# found it
	  	;;
	*)	 		# invalid format
			ocf_log err \
"stopFilesystem: Invalid mount point format (must begin with a '/'): \'$mp\'"
	    	return $FAIL
	    	;;
	esac
	
	#
	# Get the device
	#
	dev=$(real_device $OCF_RESKEY_device)
	if [ -z "$dev" ]; then
			ocf_log err "\
stop: Could not match $OCF_RESKEY_device with a real device"
			return $FAIL
	fi


	#
	# Get the force unmount setting if there is a mount point.
	#
	if [ -n "$mp" ]; then
		case ${OCF_RESKEY_force_unmount} in
	        $YES_STR)	force_umount=$YES ;;
		1)		force_umount=$YES ;;
	        *)		force_umount="" ;;
		esac
	fi

	if [ -n "$mp" ]; then
		case ${OCF_RESKEY_self_fence} in
	        $YES_STR)	self_fence=$YES ;;
		1)		self_fence=$YES ;;
	        *)		self_fence="" ;;
		esac
	fi

	#
	# Check the rgmanager-supplied reference count if one exists.
	# If the reference count is <= 1, we can safely proceed
	#
	if [ -n "$OCF_RESKEY_RGMANAGER_meta_refcnt" ]; then
		refs=$OCF_RESKEY_RGMANAGER_meta_refcnt
		if [ $refs -gt 0 ]; then
			ocf_log debug "Not unmounting $OCF_RESOURCE_INSTANCE - still in use by $refs other service(s)"
			return $OCF_SUCCESS
		fi
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

	if [ -z "$force_umount" ]; then
		ocf_log debug "Not umounting $dev (clustered file system)"
		return $SUCCESS
	fi

	#
	# Unmount the device.  
	#
	while [ ! "$done" ]; do
		isMounted $dev $mp
		case $? in
		$NO)
			ocf_log info "$dev is not mounted"
			umount_failed=
			done=$YES
			;;
		$FAIL)
			return $FAIL
			;;
		$YES)
			sync; sync; sync
			ocf_log info "unmounting $dev ($mp)"

			umount $mp
			if  [ $? -eq 0 ]; then
				umount_failed=
				done=$YES
				continue
			fi

			umount_failed=yes

			if [ "$force_umount" ]; then
				killMountProcesses $mp
			fi

			if [ $try -ge $max_tries ]; then
				done=$YES
			else
				sleep $sleep_time
				let try=try+1
			fi
			;;
		*)
			return $FAIL
			;;
		esac

		if [ $try -ge $max_tries ]; then
			done=$YES
		else
			sleep $sleep_time
			let try=try+1
		fi
	done # while 

	if [ -n "$umount_failed" ]; then
		ocf_log err "'umount $mp' failed, error=$ret_val"

		if [ "$self_fence" ]; then
			ocf_log alert "umount failed - REBOOTING"
			sync
			reboot -fn
		fi
		return $FAIL
	fi

	return $SUCCESS
}


case $1 in
start)
	declare tries=0
	declare rv

	while [ $tries -lt 3 ]; do
		startFilesystem
		rv=$?
		if [ $rv -eq 0 ]; then
			exit 0
		fi

		((tries++))
		sleep 3
	done
	exit $rv
	;;
stop)
	stopFilesystem
	exit $?
	;;
status|monitor)
  	isMounted ${OCF_RESKEY_device} ${OCF_RESKEY_mountpoint}
 	[ $? -ne $YES ] && exit $OCF_ERR_GENERIC

 	isAlive ${OCF_RESKEY_mountpoint}
 	[ $? -ne $YES ] && exit $OCF_ERR_GENERIC
 	
	exit 0
	;;
restart)
	stopFilesystem
	if [ $? -ne 0 ]; then
		exit $OCF_ERR_GENERIC
	fi

	startFilesystem
	if [ $? -ne 0 ]; then
		exit $OCF_ERR_GENERIC
	fi

	exit 0
	;;
meta-data)
	meta_data
	exit 0
	;;
validate-all)
	verify_all
	;;
*)
	echo "usage: $0 {start|stop|status|monitor|restart|meta-data|validate-all}"
	exit $OCF_ERR_UNIMPLEMENTED
	;;
esac

exit 0
