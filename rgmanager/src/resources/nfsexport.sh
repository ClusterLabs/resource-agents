#!/bin/bash

#
#  Copyright Red Hat Inc., 2002-2004
#  Copyright Mission Critical Linux, 2000
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
# NFS Export Script.  Handles starting/stopping clurmtabd and doing
# the strange NFS stuff to get it to fail over properly.
#

LC_ALL=C
LANG=C
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export LC_ALL LANG PATH

logAndPrint()
{
	echo $*
}

rmtabpid=""
nfsop_arg=""
rv=0

meta_data()
{
	cat <<EOT
<?xml version="1.0" ?>
<resource-agent name="nfsexport" version="rgmanager 2.0">
    <version>1.0</version>

    <longdesc lang="en">
        This defines an NFS export path.  Generally, these are
        defined inline and implicitly; you should not have to 
        configure one of these.  All of the relevant information
        is inherited from the parent.
    </longdesc>

    <shortdesc lang="en">
        This defines an NFS export.
    </shortdesc>

    <parameters>
        <parameter name="name" primary="1">
            <shortdesc lang="en">
                Name
            </shortdesc>
            <longdesc lang="en">
                Descriptive name for this export.  Generally, only
                one export is ever defined, and it's called "generic
                nfs export".
            </longdesc>
	    <content type="string"/>
        </parameter>

        <parameter name="device" inherit="device">
            <shortdesc lang="en">
                If you can see this, your GUI is broken.
            </shortdesc>
	    <content type="string"/>
        </parameter>

        <parameter name="path" inherit="mountpoint">
            <shortdesc lang="en">
                If you can see this, your GUI is broken.
            </shortdesc>
	    <content type="string"/>
        </parameter>
    </parameters>

    <actions>
        <action name="start" timeout="5"/>
	<action name="stop" timeout="5"/>
	<action name="recover" timeout="5"/>

	<!-- NFS Exports really don't do anything except provide a path
	     for nfs clients.  So, status and monitor are no-ops -->
	<action name="status" timeout="5" interval="1h"/>
	<action name="monitor" timeout="5" interval="1h"/>

	<action name="meta-data"/>
	<action name="verify-all"/>
    </actions>

    <special tag="rgmanager">
	<child type="nfsclient"/>
    </special>

</resource-agent>
EOT
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


verify_path()
{
	if [ -z "$OCF_RESKEY_path" ]; then
		echo No export path specified.
		return 1
	fi

	[ -d "$OCF_RESKEY_path" ] && return 0

	echo $OCF_RESKEY_path is not a directory
	
	return 1
}


verify_all()
{
	declare -i ret=0

	verify_device || ret=1
	verify_path || ret=1

	return $ret
}


#
# Check if the NFS daemons are running.
#
nfs_daemons_running()
{
    declare NFS_DAEMONS="nfsd rpc.mountd rpc.statd"

    for daemon in $NFS_DAEMONS; do
        ps -ef | grep "$daemon" | grep -v grep >/dev/null 2>&1
        if [ $? -ne 0 ]; then
	    logAndPrint $LOG_ERR \
            "NFS daemon $daemon is not running."
	    logAndPrint $LOG_ERR \
            "Verify that the NFS service run level script is enabled."
            return 1
        fi
    done

    return 0
}


nfs_check()
{
	declare junk

	if nfs_daemons_running; then
		return 0
	fi

	#
	# Don't restart daemons during status check.
	#
	if [ "$1" = "status" ]; then
		return 1;
	fi
		
  	logAndPrint $LOG_ERR "Restarting NFS daemons"
	# Note restart does less than stop/start
	junk=$(/sbin/service nfs stop)
	junk=$(/sbin/service nfs start)
	sleep 2
	
	if ! nfs_daemons_running; then
		logAndPrint $LOG_ERR "Failed restarting NFS daemons"
    		return 1
	fi
	logAndPrint $LOG_NOTICE "Successfully restarted NFS daemons"
}


case $1 in
start)
	nfs_check start || exit 1
	rm -f ${OCF_RESKEY_path}/.clumanager/pid
	clurmtabd ${OCF_RESKEY_path}
	rv=$?
	nfsop_arg="-s"
	;;

status|monitor)
	nfs_check status || exit 1
	rmtabpid=$(cat ${OCF_RESKEY_path}/.clumanager/pid)
	if [ -n "$rmtabpid" ]; then
		if kill -s 0 $rmtabpid; then
			# TODO: validate pid?
			exit 0
		fi
	fi
	#
	# rmtabd not running or nonexistent pidfile
	#
	exit 1
	;;
		    
stop)
	nfs_check restart || exit 1
	rmtabpid=$(cat ${OCF_RESKEY_path}/.clumanager/pid)
	if [ -n "$rmtabpid" ]; then
		kill $rmtabpid
	fi
	rm -f ${OCF_RESKEY_path}/.clumanager/pid
	rv=0
	nfsop_arg="-e"
	;;

recover|restart)
	$0 stop || exit 1
	$0 start || exit 1
	exit 0
	;;

meta-data)
	meta_data
	exit 0
	;;

verify-all)
	verify_all
	exit $?
	;;
esac

# XXX Don't do this one yet.  Build is broken
#
#clunfsops $nfsop_arg -d ${OCF_RESKEY_device}
#

exit $rv
