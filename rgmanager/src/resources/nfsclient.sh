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
# NFS Export Client handler agent script
#

LC_ALL=C
LANG=C
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export LC_ALL LANG PATH


meta_data()
{
	cat <<EOT
<?xml version="1.0" ?>
<resource-agent version="rgmanager 2.0" name="nfsclient">
    <version>1.0</version>

    <longdesc lang="en">
        This defines how a machine or group of machines may access
        an NFS export on the cluster.  IP addresses, IP wildcards,
	hostnames, hostname wildcards, and netgroups are supported.
    </longdesc>
    <shortdesc lang="en">
        Defines an NFS client.
    </shortdesc>

    <parameters>
        <parameter name="name" unique="1" primary="1">
            <longdesc lang="en">
                This is a symbolic name of a client used to reference
                it in the resource tree.  This is NOT the same thing
                as the target option.
            </longdesc>
            <shortdesc lang="en">
                Client Name
            </shortdesc>
            <content type="string"/>
        </parameter>

        <parameter name="target" required="1">
            <longdesc lang="en">
                This is either a hostname, a wildcard (IP address or
                hostname based), or a netgroup to which defining a
                host or hosts to export to.
            </longdesc>
            <shortdesc lang="en">
                Target Hostname, Wildcard, or Netgroup
            </shortdesc>
            <content type="string"/>
        </parameter>

        <parameter name="path" inherit="path">
            <longdesc lang="en">
                This is the path to export to the target.  This
                field is generally left blank, as it inherits the
                path from the parent export.
            </longdesc>
            <shortdesc lang="en">
                Path to Export
            </shortdesc>
            <content type="string"/>
        </parameter>

        <parameter name="options">
            <longdesc lang="en">Defines a list of options for this
                particular client.  See 'man 5 exports' for a list
                of available options.
            </longdesc>
            <shortdesc lang="en">
                Export Options
            </shortdesc>
            <content type="string"/>
        </parameter>
    </parameters>

    <actions>
        <action name="start" timeout="90"/>
        <action name="stop" timeout="5"/>
        <action name="recover" timeout="90"/>

	<!-- Checks to see if the export exists in /var/lib/nfs/etab -->
        <action name="status" timeout="5" interval="1m"/>
        <action name="monitor" timeout="5" interval="1m"/>

        <action name="meta-data" timeout="5"/>
        <action name="verify-all" timeout="30"/>
    </actions>

</resource-agent>
EOT
}


verify_options()
{
	declare o
	declare -i ret=0

	[ -z "$OCF_RESKEY_options" ] && return 0
	
	#
	# From exports(5)
	#
	for o in `echo $OCF_RESKEY_options | sed -e s/,/\ /g`; do
		case $o in
		secure)
			;;
		rw)
			;;
		async)
			;;
		no_wdelay)
			;;
		nohide)
			;;
		no_subtree_check)
			;;
		insecure_locks)
			;;
		no_auth_nlm)
			;;
		mountpoint=*)
			;;
		mp=*)
			;;
		root_squash)
			;;
		no_root_squash)
			;;
		all_squash)
			;;
		anonuid)
			;;
		anongid)
			;;
		*)
			echo Option $o invalid
			ret=1
			;;
		esac
	done

	return $ret
}


verify_target()
{
	# XXX need to add wildcards, hostname, ip, etc.
	return 0
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

	verify_options || ret=1
	verify_target || ret=1
	verify_path || ret=1

	return $ret
}


case $1 in
start)
	if [ -n "${OCF_RESKEY_type}" ] && [ "${OCF_RESKEY_type}" != "nfs" ]; then
		exit 1
	fi

	[ -n "${OCF_RESKEY_target}" ] || exit 1
	[ -n "${OCF_RESKEY_path}" ] || exit 1

	if [ -z "${OCF_RESKEY_options}" ]; then
		exportfs -i "${OCF_RESKEY_target}:${OCF_RESKEY_path}"
		rv=$?
	else
		exportfs -o ${OCF_RESKEY_options} "${OCF_RESKEY_target}:${OCF_RESKEY_path}"
		rv=$?
	fi
	;;

stop)
	[ -n "${OCF_RESKEY_target}" ] || exit 0
	[ -n "${OCF_RESKEY_path}" ] || exit 0
	exportfs -u "${OCF_RESKEY_target}:${OCF_RESKEY_path}"
	rv=$?
	;;

status|monitor)
	if [ "${OCF_RESKEY_target}" = "*" ]; then
		export OCF_RESKEY_target="\<world\>"
	fi

	exportfs | grep -q "^${OCF_RESKEY_path}\ .*${OCF_RESKEY_target}"
	rv=$?
	;;

recover|restart)
	#
	# Recover might better be "exportfs -r" - reexport
	#
	$0 stop || exit 1
	$0 start || exit 1
	;;

meta-data)
	meta_data
	exit 0
	;;

verify-all)
	verify_all
	rv=$?
	;;

	*)
	rv=0
	;;
esac

exit $rv
