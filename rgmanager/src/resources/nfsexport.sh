#!/bin/bash

#
# NFS Export Script.  Handles starting/stopping clurmtabd and doing
# the strange NFS stuff to get it to fail over properly.
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

LC_ALL=C
LANG=C
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export LC_ALL LANG PATH

. $(dirname $0)/ocf-shellfuncs


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
            <longdesc lang="en">
                Descriptive name for this export.  Generally, only
                one export is ever defined, and it's called "generic
                nfs export".
            </longdesc>
            <shortdesc lang="en">
                Name
            </shortdesc>
	    <content type="string"/>
        </parameter>

        <parameter name="device" inherit="device">
            <longdesc lang="en">
                If you can see this, your GUI is broken.
            </longdesc>
            <shortdesc lang="en">
                If you can see this, your GUI is broken.
            </shortdesc>
	    <content type="string"/>
        </parameter>

        <parameter name="path" inherit="mountpoint">
            <longdesc lang="en">
                If you can see this, your GUI is broken.
            </longdesc>
            <shortdesc lang="en">
                If you can see this, your GUI is broken.
            </shortdesc>
	    <content type="string"/>
        </parameter>

        <parameter name="fsid" inherit="fsid">
            <longdesc lang="en">
                If you can see this, your GUI is broken.
            </longdesc>
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

	<action name="meta-data" timeout="5"/>
	<action name="validate-all" timeout="30"/>
    </actions>

    <special tag="rgmanager">
	<child type="nfsexport" forbid="1"/>
	<child type="nfsclient"/>
    </special>

</resource-agent>
EOT
}


verify_device()
{
	if [ -z "$OCF_RESKEY_device" ]; then
	       ocf_log err "No device or label specified."
	       return $OCF_ERR_ARGS
	fi

	[ -b "$OCF_RESKEY_device" ] && return 0
	[ -b "`findfs $OCF_RESKEY_device`" ] && return 0

	ocf_log err "Device or label \"$OCF_RESKEY_device\" not valid"

	return $OCF_ERR_ARGS
}


verify_path()
{
	if [ -z "$OCF_RESKEY_path" ]; then
		ocf_log err "No export path specified."
		return $OCF_ERR_ARGS
	fi

	[ -d "$OCF_RESKEY_path" ] && return 0

	ocf_log err "$OCF_RESKEY_path is not a directory"
	
	return $OCF_ERR_ARGS
}


verify_all()
{
	declare -i ret=0

	verify_device || ret=$OCF_ERR_ARGS
	verify_path || ret=$OCF_ERR_ARGS

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
	    ocf_log err \
            "NFS daemon $daemon is not running."
	    ocf_log err \
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
		
  	ocf_log err "Restarting NFS daemons"
	# Note restart does less than stop/start
	junk=$(/sbin/service nfslock stop)
	junk=$(/sbin/service nfslock start)
	junk=$(/sbin/service nfs stop)
	junk=$(/sbin/service nfs start)
	sleep 2
	
	if ! nfs_daemons_running; then
		ocf_log err "Failed restarting NFS daemons"
    		return 1
	fi
	ocf_log notice "Successfully restarted NFS daemons"
}


case $1 in
start)
	nfs_check start || exit 1
	rv=0
	;;

status|monitor)
	nfs_check status || exit 1
	rv=0
	;;
		    
stop)
	nfs_check restart || exit 1
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
	echo "usage: $0 {start|status|monitor|stop|recover|restart|meta-data|validate-all}"
	exit $OCF_ERR_UNIMPLEMENTED
	;;
esac

exit $rv
