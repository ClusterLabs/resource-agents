#!/bin/bash

#
#  Copyright Red Hat Inc., 2004
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
	<action name="status" timeout="5"/>
	<action name="monitor" timeout="5"/>
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


case $1 in
start)
	rm -f ${OCF_RESKEY_path}/.clumanager/pid
	clurmtabd ${OCF_RESKEY_path}
	rv=$?
	nfsop_arg="-s"
	;;

status|monitor)
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
