#!/bin/bash

#
# NFS/CIFS file system mount/umount/etc. agent
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


. $(dirname $0)/ocf-shellfuncs


meta_data()
{
	cat <<EOT
<?xml version="1.0" ?>
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

        <parameter name="fstype" required="0">
	    <longdesc lang="en">
	    	File System type (nfs, nfs4 or cifs)
	    </longdesc>
            <shortdesc lang="en">
	    	File System Type
            </shortdesc>
	    <content type="string"/>
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
		ocf_log info "Mount point $OCF_RESKEY_mountpoint will be created "\
		     "at mount time."
		return 0
	fi

	[ -d "$OCF_RESKEY_mountpoint" ] && return 0

	ocf_log err "$OCF_RESKEY_mountpoint is not a directory"
	
	return 1
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


verify_all()
{
	verify_name || return $OCF_ERR_ARGS
	verify_fstype|| return $OCF_ERR_ARGS
	verify_host || return $OCF_ERR_ARGS
	verify_mountpoint || return $OCF_ERR_ARGS
	verify_options || return $OCF_ERR_ARGS
}



#
# isMounted fullpath mount_point
#
# Check to see if the full path is mounted where we need it.
#
isMounted () {

	typeset mp tmp_mp
	typeset fullpath tmp_fullpath

	if [ $# -ne 2 ]; then
		ocf_log err "Usage: isMounted host:/export mount_point"
		return $FAIL
	fi

	fullpath=$1
	mp=$(readlink -f $2)

	while read tmp_fullpath tmp_mp
	do
		if [ "$tmp_fullpath" = "$fullpath" -a \
		     "$tmp_mp" = "$mp" ]; then
			return $YES
		fi
	done < <(mount | awk '{print $1,$3}')

	return $NO
}

#
# startNFSFilesystem
#
startNFSFilesystem() {
	typeset -i ret_val=$SUCCESS
	typeset mp=""			# mount point
	typeset host=""
	typeset fullpath=""
	typeset exp=""
	typeset opts=""
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
	host=${OCF_RESKEY_host}
	exp=${OCF_RESKEY_export}

	fullpath=$host:$exp

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
startFilesystem: Creating mount point $mp for $fullpath"
		mkdir -p $mp
	fi

	#
	# See if the mount path is already mounted.
	# 
	isMounted $fullpath $mp
	case $? in
	$YES)		# already mounted
		ocf_log debug "$fullpath already mounted on $mp"
		return $SUCCESS
		;;
	$NO)		# not mounted, continue
		;;
	$FAIL)
		return $FAIL
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

	#
	# Mount the NFS export
	#
	ocf_log debug "mount $fstype_option $mount_options $fullpath $mp"

        case $OCF_RESKEY_fstype in
		nfs|nfs4)
			mount -t $OCF_RESKEY_fstype $mount_options $host:$exp $mp
			;;
		cifs)
			mount -t $OCF_RESKEY_fstype $mount_options //$host/$exp $mp
			;;
	esac

	ret_val=$?
	if [ $ret_val -ne 0 ]; then
		ocf_log err "\
'mount $fstype_option $mount_options $fullpath $mp' failed, error=$ret_val"
		return $FAIL
	fi
	
	return $SUCCESS
}


#
# stopFilesystem serviceID deviceID
#
# Run the stop actions
#
stopNFSFilesystem() {
	typeset -i ret_val=0
	typeset -i try=1
	typeset -i max_tries=3		# how many times to try umount
	typeset -i sleep_time=2		# time between each umount failure
	typeset done=""
	typeset umount_failed=""
	typeset no_umount=""
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
			ocf_log err \
"stopNFSFilesystem: Invalid mount point format (must begin with a '/'): \'$mp\'"
	    	return $FAIL
	    	;;
	esac
	
	#
	# Get the host/path
	#
	fullpath="${OCF_RESKEY_host}:${OCF_RESKEY_export}"

	#
	# Get the force unmount setting if there is a mount point.
	#
	if [ -n "$mp" ]; then
		case ${OCF_RESKEY_force_unmount} in
	        $YES_STR)	force_umount="$YES" ;;
		1)		force_umount="$YES" ;;
	        *)		force_umount="" ;;
		esac
	fi

	#
	# Unmount
	#
        while [ ! "$done" ]; do
	isMounted $fullpath $mp
	case $? in
	$NO)
		ocf_log debug "$fullpath is not mounted"
		umount_failed=
		done=$YES
		;;
	$FAIL)
		return $FAIL
		;;
	$YES)
		case ${OCF_RESKEY_no_unmount} in
                $YES_STR)       no_umount="$YES" ;;
                1)              no_umount="$YES" ;;
                *)              no_umount="" ;;
                esac
		
		if [ "$no_umount" ]; then
				ocf_log info "skipping unmount operation of $mp"
				return $SUCCESS
		fi

		sync; sync; sync
                        ocf_log info "unmounting $mp"

                        umount $mp
		if  [ $? -eq 0 ]; then
                                umount_failed=
                                done=$YES
                                continue
		fi

		umount_failed=yes

		if [ "$force_umount" ]; then
			if [ $try -eq 1 ]; then
				fuser -TERM -kvm "$mp"
			else
				fuser -kvm "$mp"
			fi
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
		ocf_log err "'umount $fullpath' failed ($mp), error=$ret_val"

		return $FAIL
	fi

	return $SUCCESS
}


populate_defaults()
{
	if [ -z "$OCF_RESKEY_fstype" ]; then
		export OCF_RESKEY_fstype=nfs
	fi

	if [ -z "$OCF_RESKEY_options" ]; then
		export OCF_RESKEY_options=sync,soft,noac
	fi
}


#
# Main...
#

populate_defaults

case $1 in
start)
	startNFSFilesystem
	exit $?
	;;
stop)
	stopNFSFilesystem
	exit $?
	;;
status|monitor)
	isMounted ${OCF_RESKEY_host}:${OCF_RESKEY_export} \
		${OCF_RESKEY_mountpoint}
	exit $?
	;;
restart)
	stopNFSFilesystem
	if [ $? -ne 0 ]; then
		exit $OCF_ERR_GENERIC
	fi

	startNFSFilesystem
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
	exit $?
	;;
*)
	echo "usage: $0 {start|stop|status|monitor|restart|meta-data|validate-all}"
	exit $OCF_ERR_UNIMPLEMENTED
	;;
esac

exit 0
