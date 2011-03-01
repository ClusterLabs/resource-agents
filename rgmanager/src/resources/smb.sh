#!/bin/bash

#
# Script to manage a Samba file-sharing service component.
# Unline NFS, this should be placed at the top level of a service
# because it will try to gather information necessary to run the
# smbd/nmbd daemons at run-time from the service structure.
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
# Author(s):
#  Lon Hohberger (lhh at redhat.com)
#  Tim Burke (tburke at redhat.com)
#

LC_ALL=C
LANG=C
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export LC_ALL LANG PATH

#
# Definitions!
#
declare SAMBA_CONFIG_DIR=/etc/samba
declare SMBD_COMMAND=/usr/sbin/smbd
declare NMBD_COMMAND=/usr/sbin/nmbd
declare KILLALL_COMMAND=/usr/bin/killall
declare SAMBA_PID_DIR=/var/run/samba
declare SAMBA_LOCK_DIR=/var/cache/samba

#
# gross globals
#
declare -a ipkeys
declare -a fskeys

# Don't change please :)
_FAIL=255

. $(dirname $0)/ocf-shellfuncs

meta_data()
{
    cat <<EOT
<?xml version="1.0"?>
<resource-agent version="rgmanager 2.0" name="smb">
    <version>1.0</version>

    <longdesc lang="en">
    	Dynamic smbd/nmbd resource agent
    </longdesc>
    <shortdesc lang="en">
    	Dynamic smbd/nmbd resource agent
    </shortdesc>

    <parameters>
        <parameter name="name" unique="1" primary="1">
            <longdesc lang="en">
                Samba Symbolic Name.  This name will
		correspond to /etc/samba/smb.conf.NAME
            </longdesc>
            <shortdesc lang="en">
                Samba Name
            </shortdesc>
	    <content type="string"/>
        </parameter>

        <parameter name="workgroup">
            <longdesc lang="en">
	    	Workgroup name
            </longdesc>
            <shortdesc lang="en">
	    	Workgroup name
            </shortdesc>
	    <content type="string" default="LINUXCLUSTER"/>
        </parameter>

        <parameter name="service_name" inherit="service%name">
            <longdesc lang="en">
	    	Inherit the service name.  We need to know
		the service name in order to determine file
		systems and IPs for this smb service.
            </longdesc>
            <shortdesc lang="en">
	    	Inherit the service name.
            </shortdesc>
	    <content type="string"/>
        </parameter>
    </parameters>

    <actions>
        <action name="start" timeout="0"/>
        <action name="stop" timeout="0"/>

	<!-- This is just a wrapper for LSB init scripts, so monitor
	     and status can't have a timeout, nor do they do any extra
	     work regardless of the depth -->
        <action name="status" interval="30s" timeout="0"/>
        <action name="monitor" interval="30s" timeout="0"/>

        <action name="meta-data" timeout="0"/>
        <action name="validate-all" timeout="0"/>
    </actions>
</resource-agent>
EOT
}


#
# Usage: ccs_get key
#
ccs_get()
{
	declare outp
	declare key

	[ -n "$1" ] || return $_FAIL

	key="$*"

	outp=$(ccs_tool query "$key" 2>&1)
	if [ $? -ne 0 ]; then
		if [ "$outp" = "${outp/No data available/}" ] || [ "$outp" = "${outp/Operation not permitted/}" ]; then
			ocf_log err "$outp ($key)"
			return $_FAIL
		fi

		# no real error, just no data available
		return 0
	fi

	echo $outp

	return 0
}


#
# Build a list of service IP keys; traverse refs if necessary
#
get_service_ip_keys()
{
	declare svc=$1
	declare -i x y=0
	declare outp
	declare key

	#
	# Find service-local IP keys
	#
	x=1
	while : ; do
		key="/cluster/rm/service[@name=\"$svc\"]/ip[$x]"

		#
		# Try direct method
		#
		outp=$(ccs_get "$key/@address")
		if [ $? -ne 0 ]; then
			return 1
		fi

		#
		# Try by reference
		#
		if [ -z "$outp" ]; then
			outp=$(ccs_get "$key/@ref")
			if [ $? -ne 0 ]; then
				return 1
			fi
			key="/cluster/rm/resources/ip[@address=\"$outp\"]"
		fi

		if [ -z "$outp" ]; then
			break
		fi

		#ocf_log debug "IP $outp found @ $key"

		ipkeys[$y]="$key"

		((y++))
		((x++))
	done

	ocf_log debug "$y IP addresses found for $svc/$OCF_RESKEY_name"

	return 0
}


#
# Build a list of service fs keys, traverse refs if necessary
#
get_service_fs_keys()
{
	declare svc=$1
	declare -i x y=0
	declare outp
	declare key

	#
	# Find service-local IP keys
	#
	x=1
	while : ; do
		key="/cluster/rm/service[@name=\"$svc\"]/fs[$x]"

		#
		# Try direct method
		#
		outp=$(ccs_get "$key/@name")
		if [ $? -ne 0 ]; then
			return 1
		fi

		#
		# Try by reference
		#
		if [ -z "$outp" ]; then
			outp=$(ccs_get "$key/@ref")
			if [ $? -ne 0 ]; then
				return 1
			fi
			key="/cluster/rm/resources/fs[@name=\"$outp\"]"
		fi

		if [ -z "$outp" ]; then
			break
		fi

		#ocf_log debug "filesystem $outp found @ $key"

		fskeys[$y]="$key"

		((y++))
		((x++))
	done

	ocf_log debug "$y filesystems found for $svc/$OCF_RESKEY_name"

	return 0
}


build_ip_list()
{
	declare ipaddrs ipaddr
	declare -i x=0

	while [ -n "${ipkeys[$x]}" ]; do
		ipaddr=$(ccs_get "${ipkeys[$x]}/@address")
		if [ -z "$ipaddr" ]; then
			break
		fi

		ipaddrs="$ipaddrs $ipaddr"

		((x++))
	done

	echo $ipaddrs
}


add_sha1()
{
	declare sha1line="# rgmanager-sha1 $(sha1sum "$1")"
	echo $sha1line >> "$1"
}


verify_sha1()
{
	declare tmpfile="$(mktemp /tmp/smb-$OCF_RESKEY_name.tmp.XXXXXX)"
	declare current exp

	exp=$(grep "^# rgmanager-sha1.*$1" "$1" | head -1)
	if [ -z "$exp" ]; then
		# No sha1 line.  We're done.
		ocf_log debug "No SHA1 info in $1"
		return 1
	fi

	#
	# Find expected sha1 and expected file name
	#
	exp=${exp/*sha1 /}
	exp=${exp/ */}

	grep -v "^# rgmanager-sha1" "$1" > "$tmpfile"
	current=$(sha1sum "$tmpfile")
	current=${current/ */}

	rm -f "$tmpfile"

	if [ "$current" = "$exp" ]; then
		ocf_log debug "SHA1 sum matches for $1"
		return 0
	fi
	ocf_log debug "SHA1 sum does not match for $1"
	return 1
}


add_fs_entries()
{
	declare conf="$1"
	declare sharename
	declare sharepath key

	declare -i x=0

	while [ -n "${fskeys[$x]}" ]; do
		key="${fskeys[$x]}/@name"

		sharename=$(ccs_get "$key")
		if [ -z "$sharename" ]; then
			break
		fi

		key="${fskeys[$x]}/@mountpoint"
		sharepath=$(ccs_get "$key")
		if [ -z "$sharepath" ]; then
			break
		fi

		cat >> "$conf" <<EODEV
[$sharename]
	comment = Auto-generated $sharename share
	# Hide the secret cluster files
	veto files = /.clumanager/.rgmanager/
	browsable = yes
	writable = no
	public = yes
	path = $sharepath

EODEV

		((x++))
	done
}


#
# Generate the samba configuration if neede for this service.
#
gen_smb_conf()
{
	declare conf="$1"
	declare lvl="debug"

	if [ -f "$conf" ]; then
		verify_sha1 "$conf"
		if [ $? -ne 0 ]; then
			ocf_log debug "Config file changed; skipping"
			return 0
		fi
	else
		lvl="info"
	fi

	ocf_log $lvl "Creating $conf"

	get_service_ip_keys "$OCF_RESKEY_service_name"
	get_service_fs_keys "$OCF_RESKEY_service_name"

	cat > "$conf" <<EOT
#
# "$conf"
#
# This template configuration wass automatically generated, and will
# be automatically regenerated if removed.  Please modify this file to
# speficy subdirectories and/or client access permissions.
#
# Once this file has been altered, automatic re-generation will stop.
# Remember to copy this file to all other cluster members after making
# changes, or your SMB service will not operate correctly.
#
# From a cluster perspective, the key fields are:
#     lock directory - must be unique per samba service.
#     bind interfaces only - must be present set to yes.
#     interfaces - must be set to service floating IP address.
#     path - must be the service mountpoint or subdirectory thereof.
#

[global]
        workgroup = $OCF_RESKEY_workgroup
        pid directory = /var/run/samba/$OCF_RESKEY_name
        lock directory = /var/cache/samba/$OCF_RESKEY_name
	log file = /var/log/samba/%m.log
	#private dir = /var/
	encrypt passwords = yes
	bind interfaces only = yes
	netbios name = ${OCF_RESKEY_name/ /_}

	#
	# Interfaces are based on ip resources at the top level of
	# "$OCF_RESKEY_service_name"; IPv6 addresses may or may not
	# work correctly.
	#
	interfaces = $(build_ip_list)

#
# Shares based on fs resources at the top level of "$OCF_RESKEY_service_name"
#
EOT
	add_fs_entries "$conf"
	add_sha1 "$conf"

	return 0
}


#
# Kill off the specified PID
# (from clumanager 1.0.x/1.2.x)
#
# Killing off the samba daemons was miserable to implement, merely
# because killall doesn't distinguish by program commandline.
# Consequently I had to implement these routines to selectively pick 'em off.
#
# Kills of either the {smbd|nmbd} which is running and was started with
# the specified argument.  Can't use `killall` to do this because it
# doesn't allow you to distinguish which process to kill based on any
# of the program arguments.
#
# This routine is also called on "status" checks.  In this case it doesn't
# actually kill anything.
#
# Parameters:
#       daemonName - daemon name, can be either smbd or nmbd
#       command - [stop|start|status]
#       arg     - argument passed to daemon.  In this case its not the
#                 full set of program args, rather its really just the
#                 samba config file.
#
# Returns: 0 - success (or the daemon isn't currently running)
#          1 - failure
#
kill_daemon_by_arg()
{
    	declare daemonName=$1
	declare action=$2
	declare arg=$3
	# Create a unique temporary file to stash off intermediate results
	declare tmpfile_str=/tmp/sambapids.XXXXXX
	declare tmpfile
	declare ret

	tmpfile=$(mktemp $tmpfile_str); ret_val=$?

	if [ -z "$tmpfile" ]; then
		ocf_log err "kill_daemon_by_arg: Can't create tmp file"
		return $_FAIL
	fi

	# Mumble, need to strip off the /etc/samba portion, otherwise the
	# grep pattern matching will fail.
	declare confFile="$(basename $arg)"

	# First generate a list of candidate pids.
	pidof $daemonName > $tmpfile
	if [ $? -ne 0 ]; then
		ocf_log debug "kill_daemon_by_arg: no pids for $daemonName"
		rm -f $tmpfile
		case "$action" in
		'stop')
			return 0
		;;
		'status')
			return $_FAIL
		;;
		esac
		return 0
	fi

	# If you don't find any matching daemons for a "stop" operation, thats
	# considered success; whereas for "status" inquiries its a failure.
	case "$action" in
	'stop')
		ret=0
	;;
	'status')
		ret=$_FAIL
	;;
	esac
	#
	# At this point tmpfile contains a set of pids for the corresponding
	# {smbd|nmbd}.  Now look though this candidate set of pids and compare
	# the program arguments (samba config file name).  This distinguishes
	# which ones should be killed off.
	#
	declare daemonPid=""
	for daemonPid in $(cat $tmpfile); do
		declare commandLine=$(cat /proc/$daemonPid/cmdline)
		declare confBase="$(basename $commandLine)"
		if [ "$confBase" = "$confFile" ]; then
			case "$action" in
			'status')
				rm -f $tmpfile
				return 0
			;;
			esac
			kill_daemon_pid $daemonPid
			if [ $? -ne 0 ]; then
				ret=$_FAIL
				ocf_log err \
				"kill_daemon_by_arg: kill_daemon_pid $daemonPid failed"
			else
				ocf_log debug \
				 "kill_daemon_by_arg: kill_daemon_pid $daemonPid success"
			fi
		fi
	done
	rm -f $tmpfile
	return $ret
}


#
# Kill off the specified PID
# (from clumanager 1.0.x/1.2.x)
#
kill_daemon_pid()
{
	declare pid=$1
	declare retval=0


	kill -TERM $pid
	if [ $? -eq 0 ]; then
		ocf_log debug "Samba: successfully killed $pid"
	else
		ocf_log debug "Samba: failed to kill $pid"
		retval=$_FAIL
	fi
	return $retval
}


share_start_stop()
{
	declare command=$1
	declare conf="$SAMBA_CONFIG_DIR/smb.conf.$OCF_RESKEY_name"
	declare smbd_command
	declare nmbd_command
	declare netbios_name
	
	#
	# Specify daemon options
	# -D = spawn off as separate daemon
	# -s = the following arg specifies the config file
	#
	declare smbd_options="-D -s"
	declare nmbd_options="-D -s"

	if [ "$command" = "start" ]; then
		gen_smb_conf "$conf"
	else 
		if ! [ -f "$conf" ]; then
			ocf_log warn "\"$conf\" missing during $command"
		fi
	fi

	#
	# On clusters with multiple samba shares, we need to ensure (as much
	# as possible) that each service is advertised as a separate netbios
	# name.
	#
	# Generally, the admin sets this in smb.conf.NAME - but since
	# it is not required, we need another option.  Consequently, we use
	# smb instance name (which must be unique)
	#
	if [ -f "$conf" ]; then
		grep -qe "^\([[:space:]]\+n\|n\)etbios[[:space:]]\+name[[:space:]]*=[[:space:]]*[[:alnum:]]\+" "$conf"
		if [ $? -ne 0 ]; then

			netbios_name=$OCF_RESKEY_name

			ocf_log notice "Using $netbios_name as NetBIOS name (service $OCF_RESKEY_service_name)"
			nmbd_options=" -n $netbios_name $nmbd_options"
		fi
	fi

	case $command in
	start)
		ocf_log info "Starting Samba instance \"$OCF_RESKEY_name\""
		mkdir -p "$SAMBA_PID_DIR/$OCF_RESKEY_name"
		mkdir -p "$SAMBA_LOCK_DIR/$OCF_RESKEY_name"

		[ -f "$SMBD_COMMAND" ] || exit $OCF_ERR_INSTALLED
		[ -f "$NMBD_COMMAND" ] || exit $OCF_ERR_INSTALLED

		# Kick off the per-service smbd
		$SMBD_COMMAND $smbd_options "$conf"
		ret_val=$?
		if [ $ret_val -ne 0 ]; then
			ocf_log err "Samba service failed: $SMBD_COMMAND $smbd_options \"$conf\""
			return $_FAIL
		fi
		ocf_log debug "Samba service succeeded: $SMBD_COMMAND $smbd_options \"$conf\""

		# Kick off the per-service nmbd
		$NMBD_COMMAND $nmbd_options "$conf"
		ret_val=$?
		if [ $ret_val -ne 0 ]; then
			ocf_log err "Samba service failed: $NMBD_COMMAND $nmbd_options \"$conf\""
			return $_FAIL
		fi
		ocf_log debug "Samba service succeeded: $NMBD_COMMAND $nmbd_options \"$conf\""
	;;
	stop)
		ocf_log info "Stopping Samba instance \"$OCF_RESKEY_name\""

       		kill_daemon_by_arg "nmbd" $command "$conf"
       		kill_daemon_by_arg "smbd" $command "$conf"
       		if [ "$SAMBA_PID_DIR/$OCF_RESKEY_name" != "/" ]; then
       			pushd "$SAMBA_PID_DIR" &> /dev/null
       			rm -rf "$OCF_RESKEY_name"
	       		popd &> /dev/null
		fi
		if [ "$SAMBA_LOCK_DIR/$OCF_RESKEY_name" != "/" ]; then
     			pushd "$SAMBA_LOCK_DIR" &> /dev/null
			rm -rf "$OCF_RESKEY_name"
			popd &> /dev/null
		fi
	;;
	status)
		ocf_log debug "Checking Samba instance \"$OCF_RESKEY_name\""
		kill_daemon_by_arg "nmbd" $command "$conf"
		if [ $? -ne 0 ]; then
			ocf_log err \
			"share_start_stop: nmbd for service $svc_name died!"
			return $_FAIL
		fi
		kill_daemon_by_arg "smbd" $command "$conf"
		if [ $? -ne 0 ]; then
			ocf_log err \
		"share_start_stop: nmbd for service $svc_name died!"
			return $_FAIL
		fi
	;;
	esac
}


verify_all()
{
	[ -z "$OCF_RESKEY_workgroup" ] && export OCF_RESKEY_workgroup="LINUXCLUSTER"
	[ -n "${OCF_RESKEY_name}" ] || exit $OCF_ERR_ARGS      # Invalid Argument
	if [ -z "${OCF_RESKEY_service_name}" ]; then
		ocf_log ERR "Samba service ${OCF_RESKEY_name} is not the child of a service"
		exit $OCF_ERR_ARGS
	fi
}

case $1 in
	meta-data)
		meta_data
		exit 0
	;;
	start|stop)
		verify_all
		share_start_stop $1
		exit $?
	;;
	status|monitor)
		verify_all
		share_start_stop status
		exit $?
	;;
	validate-all)
		verify_all
		echo "Yer radio's workin', driver!"
		exit 0
	;;
	*)
		echo "usage: $0 {start|stop|status|monitor|meta-data|validate-all}"
		exit $OCF_ERR_UNIMPLEMENTED
	;;
esac

