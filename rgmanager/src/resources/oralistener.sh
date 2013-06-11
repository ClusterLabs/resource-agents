#!/bin/bash
#
# Red Hat Cluster Suite resource agent for controlling Oracle 10g
# listener instances. This script will start, stop and monitor running
# listeners.
#
# start:    Will start given listener instance
#
# stop:     Will stop given listener instance
#
# monitor:  Will check that the listener is OK by calling lsnrctl status
#
#
# Copyright (C) 1997-2003 Sistina Software, Inc.  All rights reserved.
# Copyright (C) 2004-2013 Red Hat, Inc.  All rights reserved.
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

# Grab the global RHCS helper functions
. $(dirname $0)/ocf-shellfuncs
. $(dirname $0)/utils/config-utils.sh
. $(dirname $0)/utils/messages.sh
. $(dirname $0)/utils/ra-skelet.sh

declare -i	RESTART_RETRIES=3

ORACLE_USER=$OCF_RESKEY_user
ORACLE_HOME=$OCF_RESKEY_home
LISTENER=$OCF_RESKEY_name

LC_ALL=C
LANG=C
PATH=$ORACLE_HOME/bin:/bin:/sbin:/usr/bin:/usr/sbin
export LC_ALL LANG PATH ORACLE_USER ORACLE_HOME

# clulog will not log messages when run by the oracle user.
# This is a hack to work around that.
if [ "`id -u`" = "`id -u $ORACLE_USER`" ]; then
	ocf_log() {
		prio=$1
		shift
		logger -i -p daemon."$prio" -- "$*"
	}
fi

verify_all() {
	ocf_log debug "Validating configuration for $LISTENER"

	if [ -z "$OCF_RESKEY_name" ]; then
		ocf_log error "Validation for $LISTENER failed: Invalid name of service (listener name)"
		return $OCF_ERR_ARGS
	fi

	if [ -z "$OCF_RESKEY_home" ]; then
		ocf_log error "Validation for $LISTENER failed: No Oracle home specified."
		return $OCF_ERR_ARGS
	fi

	if [ -z "$OCF_RESKEY_user" ]; then
		ocf_log error "Validation for $LISTENER failed: No Oracle username specified."
		return $OCF_ERR_ARGS
	fi
 
	# Super user? Automatically change UID and exec as oracle user.
	# Oracle needs to be run as the Oracle user, not root!
	if [ "`id -u`" = "0" ]; then
		su $OCF_RESKEY_user -c "$0 $*"
		exit $?
	fi

	# Make sure the lsnrctl binary is in our $PATH
	if [ ! -x $(which lsnrctl) ]; then
		ocf_log error "Validation for $LISTENER failed: Unable to locate lsnrctl command from path! ($PATH)"
		return $OCF_ERR_GENERIC
	fi

	ocf_log debug "Validation checks for $LISTENER succeeded"
	return 0
}

start() {
	ocf_log info "Starting listener $LISTENER"
	lsnrctl_stdout=$(lsnrctl start "$LISTENER")
	if [ $? -ne 0 ]; then
		ocf_log error "start listener $LISTENER failed $lsnrctl_stdout"
		return $OCF_ERR_GENERIC
	fi

	ocf_log info "Listener $LISTENER started successfully"
	return 0
}
 
stop() {
	ocf_log info "Stopping listener $LISTENER"

	lsnrctl_stdout=$(lsnrctl stop "$LISTENER")
	if [ $? -ne 0 ]; then
		ocf_log debug "stop listener $LISTENER failed $lsnrctl_stdout"
		return $OCF_ERR_GENERIC
	fi

	ocf_log info "Listener $LISTENER stopped successfully"
	return 0
}
 
monitor() {
	declare -i depth=$1

	ocf_log debug "Checking status for listener $LISTENER depth $depth"
	lsnrctl status "$LISTENER" >& /dev/null
	if [ $? -ne 0 ]; then
		ocf_log error "Listener $LISTENER not running"
		return $OCF_ERR_GENERIC
	fi

	ocf_log debug "Listener $LISTENER is up"
	return 0 # Listener is running fine
}

recover() {
	ocf_log debug "Recovering listener $LISTENER"

	for (( i=$RESTART_RETRIES ; i; i-- )); do
		start
		if [ $? -eq 0 ] ; then
			ocf_log debug "Restarted listener $LISTENER successfully"
			break
		fi
	done

	if [ $i -eq 0 ]; then
		# stop/start's failed - return 1 (failure)
		ocf_log debug "Failed to restart listener $LISTENER after $RESTART_RETRIES tries"
		return 1
	fi

	status
	if [ $? -ne 0 ] ; then
		ocf_log debug "Failed to restart listener $LISTENER"
		return 1 # Problem restarting the Listener
	fi

	ocf_log debug "Restarted listener $LISTENER successfully"
	return 0 # Success restarting the Listener
}

case $1 in
	meta-data)
		cat `echo $0 | sed 's/^\(.*\)\.sh$/\1.metadata/'`
		exit 0
		;;
	verify-all)
		verify_all $*
		exit $?
		;;
	start)
		verify_all $* && start
		exit $?
		;;
	stop)
		verify_all $* && stop
		exit $?
		;;
	recover)
		verify_all $* && recover
		exit $?
		;;
	status|monitor)
		verify_all $*
		monitor $OCF_CHECK_LEVEL
		exit $?
		;;
	*)
		echo "Usage: $0 {start|stop|recover|monitor|status|meta-data|verify-all}"
		exit $OCF_ERR_GENERIC
		;;
esac
