#!/bin/bash

#
# NFS Server Script.  Handles starting/stopping Servand doing
# the strange NFS stuff to get it to fail over properly.
#

LC_ALL=C
LANG=C
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export LC_ALL LANG PATH

. $(dirname $0)/ocf-shellfuncs

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
<resource-agent name="nfsserver" version="rgmanager 2.0">
    <version>1.0</version>

    <longdesc lang="en">
        This defines an NFS server. 
    </longdesc>

    <shortdesc lang="en">
        This defines an NFS server.
    </shortdesc>

    <parameters>
        <parameter name="name" primary="1">
            <longdesc lang="en">
                Descriptive name for this server.  Generally, only
                one serveris ever defined.
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
		is used.
            </longdesc>
            <shortdesc lang="en">
	    	This is the path you intend to export.
            </shortdesc>
	    <content type="string"/>
        </parameter>

        <parameter name="nfspath">
            <longdesc lang="en">
	        This is the path containing shared NFS information.  This
		is relative to the export path.
            </longdesc>
            <shortdesc lang="en">
	        This is the path containing shared NFS information.
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
		/etc/init.d/nfs status
		if [ $? -eq 0 ]; then
			ocf_log debug "NFS daemons are running"
			return 0
		fi
		return 1
		;;
	esac
}


create_tree()
{
	declare fp="$OCF_RESKEY_path/$OCF_RESKEY_nfspath"

	[ -d "$fp" ] || mkdir -p $fp
	#
	# is this really needed?
	#
	#[ -d "$fp/rpc_pipefs" ] || mkdir -p $fp/rpc_pipefs

	[ -d "$fp/statd" ] || mkdir -p $fp/statd

	#
	# Create our own private copy which we use for notifies.
	# This way, we can be sure to advertise on possibly multiple
	# IP addresses.
	#
	[ -d "$fp/statd/sm-ha" ] || mkdir -p $fp/statd/sm-ha

	# Create if they don't exist
	[ -f "$fp/etab" ] || touch $fp/etab
	[ -f "$fp/xtab" ] || touch $fp/xtab
	[ -f "$fp/rmtab" ] || touch $fp/rmtab
	[ -f "$fp/state" ] || touch $fp/state
}


is_bound()
{
	mount | grep -q "$1 on $2 type none (.*bind)"
	return $?
}


mount_tree()
{
	declare fp="$OCF_RESKEY_path/$OCF_RESKEY_nfspath"

	# what about /var/lib/nfs/rpc_pipefs ?  sunrpc mount?
	# is that really needed?

	if is_bound $fp /var/lib/nfs; then
		ocf_log debug "$fp is already bound to /var/lib/nfs"
		return 0
	fi

	log_do mount -o bind $fp /var/lib/nfs
}


umount_tree()
{
	declare fp="$OCF_RESKEY_path/$OCF_RESKEY_nfspath"

	if is_bound $fp /var/lib/nfs; then
		log_do umount /var/lib/nfs
		return $?
	fi

	ocf_log debug "$fp is not bound to /var/lib/nfs"
	return 0
}

start_locking()
{
	declare ret
	[ -x /sbin/rpc.statd ] || return 1
	
	#
	# Synchronize these before starting statd
	#
	cp -f /var/lib/nfs/statd/sm-ha/* /var/lib/nfs/statd/sm/* 2> /dev/null
	cp -f /var/lib/nfs/statd/sm/* /var/lib/nfs/statd/sm-ha/* 2> /dev/null

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
	rpc.statd -H $0 -Fd &
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

	# Rip from nfslock
	ocf_log info "Stopping NFS lockd"
	if killkill lockd; then
		ocf_log debug "NFS lockd is stopped"
	else
		ocf_log err "Failed to stop NFS lockd"
	 	return 1
	fi
	
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
	create_tree
	mount_tree

	start_locking
	nfs_daemons start
	if [ $? -eq 0 ]; then
		ocf_log info "Started NFS Server $OCF_RESKEY_name"
		exit 0
	fi

	ocf_log err "Failed to start NFS Server $OCF_RESKEY_name"
	exit $?
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

	stop_locking
	umount_tree
	# todo - error check here?
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
