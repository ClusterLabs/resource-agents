#!/bin/bash

#
#  Copyright Red Hat, Inc. 2002-2004
#  Copyright Mission Critical Linux, Inc. 2000
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
INVALIDATEBUFFERS="/bin/true"

logAndPrint()
{
	echo $*
}


meta_data()
{
	cat <<EOT
<?xml version="1.0" ?>
<resource-agent name="fs" version="rgmanager 2.0">
    <version>1.0</version>

    <longdesc lang="en">
        This defines a standard file system mount (= not a clustered
	or otherwise shared file system).
    </longdesc>
    <shortdesc lang="en">
        Defines a file system mount.
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
	        File system type.  If not specified, mount(1) will attempt to
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
                Force unmount support
            </shortdesc>
	    <content type="boolean"/>
        </parameter>

        <parameter name="force_fsck">
            <longdesc lang="en">
                If set, the file system will be checked (even if
                it is a journalled file system).  This option is
                ignored for non-journalled file systems such as
                ext2.
            </longdesc>
            <shortdesc lang="en">
                Force fsck support
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

    </parameters>

    <actions>
        <action name="start" timeout="900"/>
	<action name="stop" timeout="30"/>
        <action name="recover" timeout="930"/>
	<action name="status" timeout="10"/>
	<action name="meta-data" timeout="5"/>
	<action name="verify-all" timeout="5"/>
    </actions>

    <special tag="rgmanager">
        <child type="nfsexport"/>
	<attributes maxinstances="1"/>
    </special>
</resource-agent>
EOT
}


verify_mountpoint()
{
	if [ -z "$OCF_RESKEY_mountpoint" ]; then
		echo No mount point specified.
		return 1
	fi

	[ -d "$OCF_RESKEY_mountpoint" ] && return 0

	echo $OCF_RESKEY_mountpointis not a directory
	
	return 1
}


verify_device()
{
	if [ -z "$OCF_RESKEY_device" ]; then
	       echo "No device or label specified."
	       return 1
	fi

	[ -b "$OCF_RESKEY_device" ] && return 0
	[ -b "`findfs $OCF_RESKEY_device`" ] && return 0

	echo "Device or label \"$OCF_RESKEY_device\" not valid"

	return 1
}


verify_fstype()
{
	# Auto detect?
	[ -z "$OCF_RESKEY_fstype" ] && return 0

	case $OCF_RESKEY_fstype in
	ext2|ext3|jfs|xfs|reiserfs|vfat|tmpfs)
		return 0
		;;
	*)
		echo "File system type $OCF_RESKEY_fstype not supported"
		return 1
		;;
	esac
}


verify_options()
{
	decalre -i ret=0

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
		ext2|ext3)
			case $o in
				bsddf|minixdf|check|check=*|nocheck|debug)
					continue
					;;
				errors=*|grpid|bsdgroups|nogrpid|sysvgroups)
					continue
					;;
				resgid=*|resuid=*|sb=*|grpquota|noquota)
					continue
					;;
				quota|usrquota|nouid32)
					continue
					;;
			esac

			if [ "$OCF_RESKEY_fstype" = "ext3" ]; then
				case $0 in
					noload|data=*)
						continue
						;;
				esac
			fi
			;;
		vfat)
			case $o in
				blocksize=512|blocksize=1024|blocksize=2048)
					continue
					;;
				uid=*|gid=*|umask=*|dmask=*|fmask=*)
					continue
					;;
				check=r*|check=n*|check=s*|codepage=*)
					continue
					;;
				conv=b*|conv=t*|conv=a*|cvf_format=*)
					continue
					;;
				cvf_option=*|debug|fat=12|fat=16|fat=32)
					continue
					;;
				iocharset=*|quiet)
					continue
					;;
			esac

			;;

		jfs)
			case $o in
				conv|hash=rupasov|hash=tea|hash=r5|hash=detect)
					continue
					;;
				hashed_relocation|no_unhashed_relocation)
					continue
					;;
				noborder|nolog|notail|resize=*)
					continue
					;;
			esac
			;;

		xfs)
			case $o in
				biosize=*|dmapi|xdsm|logbufs=*|logbsize=*)
					continue
					;;
				logdev=*|rtdev=*|noalign|noatime)
					continue
					;;
				norecovery|osyncisdsync|quota|userquota)
					continue
					;;
				uqnoenforce|grpquota|gqnoenforce)
					continue
					;;
				sunit=*|swidth=*)
					continue
					;;
			esac
			;;

		tmpfs)
			case $o in
				size=*|nr_blocks=*|mode=*)
					continue
					;;
			esac
			;;
		esac

		echo Option $o not supported for $OCF_RESKEY_fstype
		ret=1
	done

	return $ret
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
		logAndPrint $LOG_ERR "Usage: mountInUse device mount_point".
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
		logAndPrint $LOG_ERR "Usage: isMounted device mount_point"
		return $FAIL
	fi

	dev=$1
	mp=$2
	
	while read tmp_dev tmp_mp
	do
		if [ -n "$tmp_dev" -a "$tmp_dev" = "$dev" ]; then
			#
			# Check to see if its mounted in the right
			# place
			#
			if [ -n "$tmp_mp"  -a "$tmp_mp"  != "$mp" ]; then
				logAndPrint $LOG_WARNING "\
Device $dev is mounted on $tmp_mp instead of $mp"
			fi
			return $YES
		fi
	done < <(mount | awk '{print $1,$3}')

	return $NO
}


#
# killMountProcesses device mount_point
#
# Using lsof or fuser try to unmount the mount by killing of the processes
# that might be keeping it busy.
#
killMountProcesses()
{
	typeset have_lsof=""
	typeset have_fuser=""
	typeset try

	if [ $# -ne 2 ]; then
		logAndPrint $LOG_ERR \
			"Usage: killMountProcesses device mount_point"
		return $FAIL
	fi

	typeset dev=$1
	typeset mp=$2

	logAndPrint $LOG_INFO "Forcefully unmounting $dev ($mp)"

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
			
	for try in 1 2; do
		if [ -n "$have_fuser" ]; then
			#
			# Use fuser to free up mount point
			#
			while read command pid user; do
				if [ -z "$pid" ]; then
					continue
				fi

				if [ $try -eq 1 ]; then
					logAndPrint $LOG_INFO \
				"killing process $pid ($user $command $dev)"
				fi

				if [ $try -gt 1 ]; then
					kill -9 $pid
				else
					kill -TERM $pid
				fi
			done < <(fuser -vm $dev | \
				grep -v PID | \
				sed 's;^'$dev';;' | \
				awk '{print $4,$2,$1}' | \
				sort -u -k 1,3)
		fi

		if [ -n "$have_lsof" ]; then
			#
			# Use lsof to free up mount point
			#
			while read command pid user name; do
				if [ -z "$pid" ]; then
					continue
				fi

				if [ $try -eq 1 ]; then
					logAndPrint $LOG_INFO \
				"killing process $pid ($user $command $dev)"
				fi

				if [ $try -gt 1 ]; then
					kill -9 $pid
				else
					kill -TERM $pid
				fi
			done < <(lsof $dev | \
				grep -v PID | \
				awk '{print $1,$2,$3,$9}' | \
				sort -u -k 1,3)
		fi
	done

	if [ -z "$have_lsof" -a -z "$have_fuser" ]; then
		logAndPrint $LOG_WARNING \
	"Cannot forcefully unmount $dev; cannot find lsof or fuser commands"
		return $FAIL
	fi

	return $SUCCESS
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
			logAndPrint $LOG_ERR \
"startFilesystem: Invalid mount point format (must begin with a '/'): \'$mp\'"
	    	return $FAIL
	    	;;
	esac
	
	#
	# Get the device
	#
	dev=${OCF_RESKEY_device}

	#
	# Ensure we've got a valid directory
	#
	if [ -e "$mp" ]; then
		if ! [ -d "$mp" ]; then
			logAndPrint $LOG_ERR "\
startFilesystem: Mount point $mp exists but is not a directory"
			return $FAIL
		fi
	else
		logAndPrint $LOG_INFO "\
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
		logAndPrint $LOG_DEBUG "$dev already mounted"
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
		logAndPrint $LOG_ERR "\
Cannot mount $dev on $mp, the device or mount point is already in use!"
		return $FAIL
		;;
	$NO)		# good, no one else is using it
		;;
	$FAIL)
		return $FAIL
		;;
	*)
		logAndPrint $LOG_ERR "Unknown return from mountInUse"
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
			logAndPrint $LOG_ERR \
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
	# Check to determine if we need to fsck the filesystem.
	#
	# Note: this code should not indicate in any manner suggested
	# file systems to use in the cluster.  Known filesystems are
	# listed here for correct operation.
	#
        case "$fstype" in
        reiserfs) typeset fsck_needed="" ;;
        ext3)     typeset fsck_needed="" ;;
        jfs)      typeset fsck_needed="" ;;
        xfs)      typeset fsck_needed="" ;;
        ext2)     typeset fsck_needed=yes ;;
        minix)    typeset fsck_needed=yes ;;
        vfat)     typeset fsck_needed=yes ;;
        msdos)    typeset fsck_needed=yes ;;
	"")       typeset fsck_needed=yes ;;		# assume fsck
	*)
		typeset fsck_needed=yes 		# assume fsck
	     	logAndPrint $LOG_WARNING "\
Unknown file system type '$fstype' for device $dev.  Assuming fsck is required."
		;;
	esac


	#
	# Fsck the device, if needed.
	#
	if [ -n "$fsck_needed" ] || [ "${OCF_RESKEY_force_fsck}" = "yes" ] ||\
	   [ "${OCF_RESKEY_force_fsck}" = "1" ]; then
		typeset fsck_log=/tmp/$(basename $dev).fsck.log
		logAndPrint $LOG_DEBUG "Running fsck on $dev"
		fsck -p $dev >> $fsck_log 2>&1
		ret_val=$?
		if [ $ret_val -gt 1 ]; then
			logAndPrint $LOG_ERR "\
'fsck -p $dev' failed, error=$ret_val; check $fsck_log for errors"
			logAndPrint $LOG_DEBUG "Invalidating buffers for $dev"
			$INVALIDATEBUFFERS -f $dev
			return $FAIL
		fi
		rm -f $fsck_log
	fi

	#
	# Mount the device
	#
	logAndPrint $LOG_DEBUG "mount $fstype_option $mount_options $dev $mp"
	mount $fstype_option $mount_options $dev $mp
	ret_val=$?
	if [ $ret_val -ne 0 ]; then
		logAndPrint $LOG_ERR "\
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
	typeset done=""
	typeset umount_failed=""
	typeset force_umount=""
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
			logAndPrint $LOG_ERR \
"startFilesystem: Invalid mount point format (must begin with a '/'): \'$mp\'"
	    	return $FAIL
	    	;;
	esac
	
	#
	# Get the device
	#
	dev=${OCF_RESKEY_device}

	#
	# Get the force unmount setting if there is a mount point.
	#
	if [ -n "$mp" ]; then
		case ${OCF_RESKEY_force_unmount} in
	        $YES_STR)	force_umount=$YES ;;
		0)		force_umount=$YES ;;
	        *)		force_umount="" ;;
		esac
	fi

	#
	# Unmount the device.  
	#

	while [ ! "$done" ]; do
		isMounted $dev $mp
		case $? in
		$NO)
			logAndPrint $LOG_INFO "$dev is not mounted"
			umount_failed=
			done=$YES
			;;
		$FAIL)
			return $FAIL
			;;
		$YES)
			sync; sync; sync
			logAndPrint $LOG_INFO "unmounting $dev ($mp)"

			umount $dev
			if  [ $? -eq 0 ]; then
				umount_failed=
				done=$YES
				continue
			fi

			umount_failed=yes

			if [ "$force_umount" ]; then
				killMountProcesses $dev $mp
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
		logAndPrint $LOG_ERR "'umount $dev' failed ($mp), error=$ret_val"
		return $FAIL
	else
		return $SUCCESS
	fi
}


case $1 in
start)
	startFilesystem
	exit $?
	;;
stop)
	stopFilesystem
	exit $?
	;;
status)
	isMounted ${OCF_RESKEY_device} ${OCF_RESKEY_mountpoint}
	exit $?
	;;
restart|recover)
	stopFilesystem
	if [ $? -ne 0 ]; then
		exit 1
	fi

	startFilesystem
	if [ $? -ne 0 ]; then
		exit 1
	fi

	exit 0
	;;
meta-data)
	meta_data
	exit 0
	;;
verify-all)
	exit 2
	;;
esac

exit 0
