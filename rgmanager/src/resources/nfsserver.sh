#!/bin/bash

#
# NFS Server Script.  Handles starting/stopping Servand doing
# the strange NFS stuff to get it to fail over properly.
#

LC_ALL=C
LANG=C
PATH=/bin:/sbin:/usr/bin:/usr/sbin

V4RECOVERY="/var/lib/nfs/v4recovery"
PROC_V4RECOVERY="/proc/fs/nfsd/nfsv4recoverydir"

export LC_ALL LANG PATH

. $(dirname $0)/ocf-shellfuncs

# SELinux information
which restorecon &> /dev/null && selinuxenabled
export SELINUX_ENABLED=$?
if [ $SELINUX_ENABLED ]; then
	export SELINUX_LABEL="$(ls -ldZ /var/lib/nfs/statd | cut -f4 -d' ')"
fi


log_do()
{
	ocf_log debug $*
	$* &> /dev/null
	ret=$?
	if [ $ret -ne 0 ]; then
		ocf_log debug "Failed: $*"
	fi
	return $ret
}


meta_data()
{
	cat <<EOT
<?xml version="1.0" ?>
<!DOCTYPE resource-agent SYSTEM "ra-api-1-modified.dtd">
<resource-agent name="nfsserver" version="rgmanager 2.0">
    <version>1.0</version>

    <longdesc lang="en">
        This defines an NFS server resource.  The NFS server
	resource is useful for exporting NFSv4 file systems
	to clients.  Because of the way NFSv4 works, only
	one NFSv4 resource may exist on a server at a
	time.  Additionally, it is not possible to use
	the nfsserver resource when also using local instances
	of NFS on each cluster node.
    </longdesc>

    <shortdesc lang="en">
        This defines an NFS server resource.
    </shortdesc>

    <parameters>
        <parameter name="name" primary="1">
            <longdesc lang="en">
                Descriptive name for this server.  Generally, only
                one server is ever defined per service.
            </longdesc>
            <shortdesc lang="en">
                Name
            </shortdesc>
	    <content type="string"/>
        </parameter>

        <parameter name="path" inherit="mountpoint">
            <longdesc lang="en">
	        This is the path you intend to export.  Usually, this is
		left blank, and the mountpoint of the parent file system
		is used.  This path is passed to nfsclient resources as
		the export path when exportfs is called.
            </longdesc>
            <shortdesc lang="en">
	    	This is the path you intend to export.
            </shortdesc>
	    <content type="string"/>
        </parameter>

        <parameter name="nfspath">
            <longdesc lang="en">
	        This is the path containing shared NFS information which
		is used for NFS recovery after a failover.  This
		is relative to the export path, and defaults to
		".clumanager/nfs".
            </longdesc>
            <shortdesc lang="en">
	        This is the path containing shared NFS recovery
		information, relative to the path parameter.
            </shortdesc>
	    <content type="string" default=".clumanager/nfs"/>
        </parameter>

    </parameters>

    <actions>
        <action name="start" timeout="5"/>
	<action name="stop" timeout="5"/>
	<action name="recover" timeout="5"/>

	<action name="status" timeout="5" interval="30"/>
	<action name="monitor" timeout="5" interval="30"/>

	<action name="meta-data" timeout="5"/>
	<action name="validate-all" timeout="30"/>
    </actions>

    <special tag="rgmanager">
    	<attributes maxinstances="1"/>
	<child type="nfsexport" forbid="1"/>
	<child type="nfsserver" forbid="1"/>
	<child type="nfsclient" start="1" stop="2"/>
	<child type="ip" start="2" stop="1"/>
    </special>

</resource-agent>
EOT
}


verify_path()
{
	if [ -z "$OCF_RESKEY_path" ]; then
		ocf_log err "No server path specified."
		return $OCF_ERR_ARGS
	fi

	[ -d "$OCF_RESKEY_path" ] && return 0

	ocf_log err "$OCF_RESKEY_path is not a directory"
	
	return $OCF_ERR_ARGS
}


verify_nfspath()
{
	if [ -z "$OCF_RESKEY_nfspath" ]; then
		echo No NFS data path specified.
		return 1
	fi

	[ -d "$OCF_RESKEY_path" ] && return 0
	
	# xxx do nothing for now.
	return 0
}


verify_all()
{
	verify_path || return 1
	verify_nfspath || return 1

	return 0
}


nfs_daemons()
{
	declare oper
	declare val

	case $1 in
	start)
		ocf_log info "Starting NFS daemons"
		/etc/init.d/nfs start
		if [ $? -ne 0 ]; then
			ocf_log err "Failed to start NFS daemons"
			return 1
		fi

		ocf_log debug "NFS daemons are running"
		return 0
		;;
	stop)
		ocf_log info "Stopping NFS daemons"
		if ! /etc/init.d/nfs stop; then
			ocf_log err "Failed to stop NFS daemons"
			return 1
		fi

		ocf_log debug "NFS daemons are stopped"

		return 0
		;;
	status|monitor)
		declare recoverydir="$OCF_RESKEY_path/$OCF_RESKEY_nfspath/v4recovery"
		val=$(cat $PROC_V4RECOVERY)

		[ "$val" = "$recoverydir" ] || ocf_log warning \
			"NFSv4 recovery directory is $val instead of $recoverydir"
		/etc/init.d/nfs status
		if [ $? -eq 0 ]; then
			ocf_log debug "NFS daemons are running"
			return 0
		fi
		return $OCF_NOT_RUNNING
		;;
	esac
}


create_tree()
{
	declare fp="$OCF_RESKEY_path/$OCF_RESKEY_nfspath"

	[ -d "$fp" ] || mkdir -p "$fp"

	[ -d "$fp/statd" ] || mkdir -p "$fp/statd"
	[ -d "$fp/v4recovery" ] || mkdir -p "$fp/v4recovery"

	#
	# Create our own private copy which we use for notifies.
	# This way, we can be sure to advertise on possibly multiple
	# IP addresses.
	#
	[ -d "$fp/statd/sm" ] || mkdir -p "$fp/statd/sm"
	[ -d "$fp/statd/sm.bak" ] || mkdir -p "$fp/statd/sm.bak"
	[ -d "$fp/statd/sm-ha" ] || mkdir -p "$fp/statd/sm-ha"
	[ -n "`id -u rpcuser`" -a "`id -g rpcuser`" ] && chown -R rpcuser.rpcuser "$fp/statd"

	# Create if they don't exist
	[ -f "$fp/etab" ] || touch "$fp/etab"
	[ -f "$fp/xtab" ] || touch "$fp/xtab"
	[ -f "$fp/rmtab" ] || touch "$fp/rmtab"

	[ $SELINUX_ENABLED ] && chcon -R "$SELINUX_LABEL" "$fp"

        #
        # Generate a random state file.  If this ends up being what a client
        # already has in its list, that's bad, but the chances of this
        # are small - and relocations should be rare.
        #
        dd if=/dev/urandom of=$fp/state bs=1 count=4 &> /dev/null
	[ -n "`id -u rpcuser`" -a "`id -g rpcuser`" ] && chown rpcuser.rpcuser "$fp/state"
}

setup_v4recovery()
{
	declare recoverydir="$OCF_RESKEY_path/$OCF_RESKEY_nfspath/v4recovery"

	# mounts /proc/fs/nfsd for us
	lsmod | grep -q nfsd
	if [ $? -ne 0 ]; then
		modprobe nfsd
	fi

	val=$(cat "$PROC_V4RECOVERY")

	# Ensure start-after-start works
	if [ "$val" = "$recoverydir" ]; then
		return 0
	fi

	#
	# If the value is not default, there may be another
	# cluster service here already which has replaced
	# the v4 recovery directory.  In that case,
	# we must refuse to go any further.
	#
	if [ "$val" != "$V4RECOVERY" ]; then
		ocf_log err "NFSv4 recovery directory has an unexpected value: $val"
		return 1
	fi

	#
	# Redirect nfs v4 recovery dir to shared storage
	#
	echo "$recoverydir" > "$PROC_V4RECOVERY"
	if [ $? -ne 0 ]; then
		echo "Uh oh... echo failed!?"
	fi

	val="$(cat $PROC_V4RECOVERY)"
	if [ "$val" != "$recoverydir" ]; then
		ocf_log err "Failed to change NFSv4 recovery path"
		ocf_log err "Wanted: $recoverydir; got $val"
		return 1
	fi

	return 0
}


cleanup_v4recovery()
{
	#
	# Restore nfsv4 recovery directory to default
	#
	echo "$V4RECOVERY" > "$PROC_V4RECOVERY"
	return $?
}


is_bound()
{
	mount | grep -q "$1 on $2 type none (.*bind.*)"
	return $?
}


setup_tree()
{
	declare fp="$OCF_RESKEY_path/$OCF_RESKEY_nfspath"

	if is_bound $fp/statd /var/lib/nfs/statd; then
		ocf_log debug "$fp is already bound to /var/lib/nfs/statd"
		return 0
	fi

	mount -o bind "$fp/statd" /var/lib/nfs/statd
	cp -a "$fp"/*tab /var/lib/nfs
	[ $SELINUX_ENABLED ] && restorecon /var/lib/nfs
}


cleanup_tree()
{
	declare fp="$OCF_RESKEY_path/$OCF_RESKEY_nfspath"

	if is_bound "$fp/statd" /var/lib/nfs/statd; then
		log_do umount /var/lib/nfs/statd || return 1
	else
		ocf_log debug "$fp is not bound to /var/lib/nfs/statd"
	fi

	cp -a /var/lib/nfs/*tab "$fp"

	return 0
}

start_locking()
{
	declare ret
	[ -x /sbin/rpc.statd ] || return 1
	
	#
	# Synchronize these before starting statd
	#
	cp -f /var/lib/nfs/statd/sm-ha/* /var/lib/nfs/statd/sm 2> /dev/null
	cp -f /var/lib/nfs/statd/sm/* /var/lib/nfs/statd/sm-ha 2> /dev/null

	if pidof rpc.statd &> /dev/null; then
		ocf_log debug "rpc.statd is already running"
		return 0
	fi

	#
	# Set this resrouce script as the callout program.  We are evil.
	# In cases where we want to preserve lock information, this is needed
	# because we can't do the "copy" that we do on the down-state...
	#
	ocf_log info "Starting rpc.statd"
	rm -f /var/run/sm-notify.pid
	rpc.statd -H $0 -d
	ret=$?
	if [ $ret -ne 0 ]; then
		ocf_log err "Failed to start rpc.statd"
		return $ret
	fi
	touch /var/lock/subsys/nfslock
	return $ret
}


terminate()
{
	declare pids
	declare i=0

	while : ; do
		pids=$(pidof $1)
		[ -z "$pids" ] && return 0
	 	kill $pids
		sleep 1
		((i++))
		[ $i -gt 3 ] && return 1
	done
}


killkill()
{
	declare pids
	declare i=0

	while : ; do
		pids=$(pidof $1)
		[ -z "$pids" ] && return 0
	 	kill -9 $pids
		sleep 1
		((i++))
		[ $i -gt 3 ] && return 1
	done
}


stop_locking()
{
	declare ret 

	ocf_log info "Stopping rpc.statd"
	if terminate rpc.statd; then
		ocf_log debug "rpc.statd is stopped"
	else
		if killkill rpc.statd; then
			ocf_log debug "rpc.statd is stopped"
		else
			ocf_log debug "Failed to stop rpc.statd"
			return 1
		fi
	fi
}


case $1 in
start)
	# Check for and source configuration file
	ocf_log info "Starting NFS Server $OCF_RESKEY_name"
	create_tree || exit 1
	setup_tree || exit 1
	setup_v4recovery || exit 1

	start_locking
	nfs_daemons start
	rv=$?
	if [ $rv -eq 0 ]; then
		ocf_log info "Started NFS Server $OCF_RESKEY_name"
		exit 0
	fi

	ocf_log err "Failed to start NFS Server $OCF_RESKEY_name"
	exit $rv
	;;

status|monitor)
	nfs_daemons status 
	exit $?
	;;
		    
stop)
	if ! nfs_daemons stop; then
		ocf_log err "Failed to stop NFS Server $OCF_RESKEY_name"
		exit $OCF_ERR_GENERIC
	fi

	# Copy the current notify list into our private area
	ocf_log debug "Copying sm files for future notification..."
	rm -f /var/lib/nfs/statd/sm-ha/* &> /dev/null
	cp -f /var/lib/nfs/statd/sm/* /var/lib/nfs/statd/sm-ha &> /dev/null

	stop_locking || exit 1
	cleanup_v4recovery
	cleanup_tree || exit 1
	exit 0
	;;

add-client)
	ocf_log debug "$0 $1 $2 $3"
	touch /var/lib/nfs/statd/sm/$2
	touch /var/lib/nfs/statd/sm-ha/$2
	exit 0
	;;

del-client)
	ocf_log debug "$0 $1 $2 $3"
	touch /var/lib/nfs/statd/sm/$2
	rm -f /var/lib/nfs/statd/sm-ha/$2
	exit 0
	;;

recover|restart)
	$0 stop || exit $OCF_ERR_GENERIC
	$0 start || exit $OCF_ERR_GENERIC
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
	echo "usage: $0 {start|stop|status|monitor|restart|recover|add-client|del-client|meta-data|validate-all}"
	exit $OCF_ERR_UNIMPLEMENTED
	;;
esac

exit 0
