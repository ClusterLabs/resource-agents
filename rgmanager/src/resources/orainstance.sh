#!/bin/bash
#
# Copyright 2003-2004, 2006-2013 Red Hat, Inc.
#
# Author(s):
#     Hardy Merrill <hmerrill at redhat.com>
#     Lon Hohberger <lhh at redhat.com>
#     Michael Moon <Michael dot Moon at oracle.com>
#     Ryan McCabe <rmccabe at redhat.com>
#
# This program is Open Source software.  You may modify and/or redistribute
# it persuant to the terms of the Open Software License version 2.1, which
# is available from the following URL and is included herein by reference:
#
# 	http://opensource.org/licenses/osl-2.1.php
#
# NOTES:
#
# (1) You can comment out the LOCKFILE declaration below.  This will prevent
# the need for this script to access anything outside of the ORACLE_HOME 
# path.
#
# (2) You MUST customize ORACLE_USER, ORACLE_HOME, ORACLE_SID, and
# ORACLE_HOSTNAME to match your installation if not running from within
# rgmanager.
#
# (3) Do NOT place this script in shared storage; place it in ORACLE_USER's
# home directory in non-clustered environments and /usr/share/cluster
# in rgmanager/Red Hat cluster environments.
#
# Oracle is a registered trademark of Oracle Corporation.
# Oracle9i is a trademark of Oracle Corporation.
# Oracle10g is a trademark of Oracle Corporation.
# Oracle11g is a trademark of Oracle Corporation.
# All other trademarks are property of their respective owners.
#
#
# $Id: orainstance.sh 127 2009-08-21 09:17:52Z hevirtan $
#
# Original version is distributed with RHCS. The modifications include
# the following minor changes:
# - Meta-data moved to a dedicated file
# - Support for multiple listeners
# - Disabled EM
# - SysV init support removed. Only usable with rgmanager
#

# Grab the global RHCS helper functions
. $(dirname $0)/ocf-shellfuncs
. $(dirname $0)/utils/config-utils.sh
. $(dirname $0)/utils/messages.sh
. $(dirname $0)/utils/ra-skelet.sh

. /etc/init.d/functions

declare SCRIPT="`basename $0`"
declare SCRIPTDIR="`dirname $0`"

# Required parameters from rgmanager
ORACLE_USER=$OCF_RESKEY_user
ORACLE_HOME=$OCF_RESKEY_home
ORACLE_SID=$OCF_RESKEY_name

# Optional parameters with default values
LISTENERS=$OCF_RESKEY_listeners
LOCKFILE="$ORACLE_HOME/.orainstance-${ORACLE_SID}.lock"
[ -n "$OCF_RESKEY_lockfile" ] && LOCKFILE=$OCF_RESKEY_lockfile

export LISTENERS ORACLE_USER ORACLE_HOME ORACLE_SID LOCKFILE
export LD_LIBRARY_PATH=$ORACLE_HOME/lib
export PATH=$ORACLE_HOME/bin:/bin:/sbin:/usr/bin:/usr/sbin

declare -i	RESTART_RETRIES=3
declare -r	DB_PROCNAMES="pmon"
declare -r	LSNR_PROCNAME="tnslsnr"

# clulog will not log messages when run by the oracle user.
# This is a hack to work around that.
if [ "`id -u`" = "`id -u $ORACLE_USER`" ]; then
	ocf_log() {
		prio=$1
		shift
		logger -i -p daemon."$prio" -- "$*"
	}
fi

#
# Start Oracle (database portion)
#
start_db() {
	declare -i rv
	declare startup_cmd
	declare startup_stdout

	ocf_log info "Starting Oracle DB $ORACLE_SID"

	# Set up our sqlplus script.  Basically, we're trying to 
	# capture output in the hopes that it's useful in the case
	# that something doesn't work properly.
	startup_cmd="set heading off;\nstartup;\nquit;\n"
	startup_stdout=$(echo -e "$startup_cmd" | sqlplus -S "/ as sysdba")
	rv=$?

	# Dump output to syslog for debugging
	ocf_log debug "[$ORACLE_SID] [$rv] sent $startup_cmd"
	ocf_log debug "[$ORACLE_SID] [$rv] got $startup_stdout"
	
	if [ $rv -ne 0 ]; then
		ocf_log error "Starting Oracle DB $ORACLE_SID failed, sqlplus returned $rv"
		return 1
	fi

	# If we see:
	# ORA-.....: failure, we failed
	# Troubleshooting:
	#   ORA-00845 - Try rm -f /dev/shm/ora_*
	#   ORA-01081 - Try echo -e 'shutdown abort;\nquit;'|sqlplus "/ as sysdba"
	if [[ "$startup_stdout" =~ "ORA-" ]] || [[ "$startup_stdout" =~ "failure" ]]; then
		ocf_log error "Starting Oracle DB $ORACLE_SID failed, found errors in stdout"
		return 1
	fi

	ocf_log info "Started Oracle DB $ORACLE_SID successfully"
	return 0
}


#
# Stop Oracle (database portion)
#
stop_db() {
	declare stop_cmd
	declare stop_stdout
	declare -i rv
	declare how_shutdown="$1"

	if [ -z "$1" ]; then
		how_shutdown="immediate"
	fi

	ocf_log info "Stopping Oracle DB $ORACLE_SID $how_shutdown"

	ora_procname="ora_${DB_PROCNAMES}_${ORACLE_SID}"
	status $ora_procname
	if [ $? -ne 0 ]; then
		ocf_log debug "no pmon process -- DB $ORACLE_SID already stopped"
		# No pmon process found, db already down
		return 0
	fi

	# Setup for Stop ...
	stop_cmd="set heading off;\nshutdown $how_shutdown;\nquit;\n"
	stop_stdout=$(echo -e "$stop_cmd" | sqlplus -S "/ as sysdba")
	rv=$?

	# Log stdout of the stop command
	ocf_log debug "[$ORACLE_SID] sent stop command $stop_cmd"
	ocf_log debug "[$ORACLE_SID] got $stop_stdout"
	
	# sqlplus returned failure. We'll return failed to rhcs
	if [ $rv -ne 0 ]; then
		ocf_log error "Stopping Oracle DB $ORACLE_SID failed, sqlplus returned $rv"
		return 1
	fi

	# If we see 'ORA-' or 'failure' in stdout, we're done.
	if [[ "$startup_stdout" =~ "ORA-" ]] || [[ "$startup_stdout" =~ "failure" ]]; then
		ocf_log error "Stopping Oracle DB $ORACLE_SID failed, errors in stdout"
		return 1
	fi

	ocf_log info "Stopped Oracle DB $ORACLE_SID successfully"
	return 0
}


#
# Destroy any remaining processes with refs to $ORACLE_SID
#
force_cleanup() {
	declare pids
	declare pid

	ocf_log error "Not all Oracle processes for $ORACLE_SID exited cleanly, killing"
	
	pids=`ps ax | grep "ora_.*_${ORACLE_SID}" | grep -v grep | awk '{print $1}'`

	for pid in $pids; do
		kill -9 $pid
		rv=$?
		if [ $rv -eq 0 ]; then
			ocf_log info "Cleanup $ORACLE_SID Killed PID $pid"
		else
			ocf_log error "Cleanup $ORACLE_SID Kill PID $pid failed: $rv"
		fi
	done

	return 0
}


#
# Wait for oracle processes to exit.  Time out after 60 seconds
#
exit_idle() {
	declare -i n=0
	
	ocf_log debug "Waiting for Oracle processes for $ORACLE_SID to terminate..."
	while ps ax | grep $ORACLE_SID | grep -q -v $LSNR_PROCNAME | grep -q -v grep; do
		if [ $n -ge 90 ]; then
			ocf_log debug "Timed out while waiting for Oracle processes for $ORACLE_SID to terminate"
			force_cleanup
			return 0
		fi
		sleep 1
		((n++))
	done

	ocf_log debug "All Oracle processes for $ORACLE_SID have terminated"
	return 0
}


#
# Get database background process status.  Restart it if it failed and
# we have seen the lock file.
#
get_db_status() {
	declare -i subsys_lock=$1
	declare -i i=0
	declare -i rv=0
	declare ora_procname

	ocf_log debug "Checking status of DB $ORACLE_SID"

	for procname in $DB_PROCNAMES ; do
		ora_procname="ora_${procname}_${ORACLE_SID}"
		
		status $ora_procname
		if [ $? -eq 0 ] ; then
			# This one's okay; go to the next one.
			continue
		fi

		# We're not supposed to be running, and we are,
		# in fact, not running...
		if [ $subsys_lock -ne 0 ]; then
			ocf_log debug "DB $ORACLE_SID is already stopped"
			return 3
		fi

		for (( i=$RESTART_RETRIES ; i; i-- )) ; do
			# this db process is down - stop and
			# (re)start all ora_XXXX_$ORACLE_SID processes
			ocf_log info "Restarting Oracle Database $ORACLE_SID"
			stop_db

			start_db
			if [ $? -eq 0 ] ; then
				# ora_XXXX_$ORACLE_SID processes started
				# successfully, so break out of the
				# stop/start # 'for' loop
				ocf_log info "Restarted Oracle DB $ORACLE_SID successfully"
				break
			fi
		done

		if [ $i -eq 0 ]; then
			# stop/start's failed - return 1 (failure)
			ocf_log error "Failed to restart Oracle DB $ORACLE_SID after $RESTART_RETRIES tries"
			return 1
		fi
	done

	ocf_log debug "Checking status of DB $ORACLE_SID success"
	return 0
}


#
# Get the status of the Oracle listener process
#
get_lsnr_status() {
	declare -i subsys_lock=$1
	declare -i rv
	declare -r LISTENER=$3

	ocf_log debug "Checking status for listener $LISTENER"
	lsnrctl status "$LISTENER" >& /dev/null
	rv=$?
	if [ $rv -eq 0 ] ; then
		ocf_log debug "Listener $LISTENER is up"
		return 0 # Listener is running fine
	fi

	# We're not supposed to be running, and we are,
	# in fact, not running.  Return 3
	if [ $subsys_lock -ne 0 ]; then
		ocf_log debug "Listener $LISTENER is stopped as expected"
		return 3
	fi

	# Listener is NOT running (but should be) - try to restart
	for (( i=$RESTART_RETRIES ; i; i-- )) ; do
		ocf_log info "Listener $LISTENER is down, attempting to restart"
		lsnrctl start "$LISTENER" >& /dev/null
		lsnrctl status "$LISTENER" >& /dev/null
		if [ $? -eq 0 ]; then
			ocf_log info "Listener $LISTENER was restarted successfully"
			break # Listener was (re)started and is running fine
		fi
	done

	if [ $i -eq 0 ]; then
		# stop/start's failed - return 1 (failure)
		ocf_log error "Failed to restart listener $LISTENER after $RESTART_RETRIES tries"
		return 1
	fi

	lsnrctl_stdout=$(lsnrctl status "$LISTENER")
	rv=$?
	if [ $rv -ne 0 ] ; then
		ocf_log error "Starting listener $LISTENER failed: $rv output $lsnrctl_stdout"
		return 1 # Problem restarting the Listener
	fi

	ocf_log info "Listener $LISTENER started successfully"
	return 0 # Success restarting the Listener
}


#
# Helps us keep a running status so we know what our ultimate return
# code will be.  Returns 1 if the $1 and $2 are not equivalent, otherwise
# returns $1.  The return code is meant to be the next $1 when this is
# called, so, for example:
#
# update_status 0   <-- returns 0
# update_status $? 0 <-- returns 0
# update_status $? 3 <-- returns 1 (values different - error condition)
# update_status $? 1 <-- returns 1 (same, but happen to be error state!)
#
# update_status 3
# update_status $? 3 <-- returns 3
#
# (and so forth...)
#
update_status() {
	declare -i old_status=$1
	declare -i new_status=$2

	if [ -z "$2" ]; then
		return $old_status
	fi

	if [ $old_status -ne $new_status ]; then
		ocf_log error "Error: $old_status vs $new_status for $ORACLE_SID - returning 1"
		return 1
	fi

	return $old_status
}


#
# Print an error message to the user and exit.
#
oops() {
	ocf_log error "$ORACLE_SID: Fatal: $1 failed validation checks"
	exit 1
}


#
# Do some validation on the user-configurable stuff at the beginning of the
# script.
#
validation_checks() {
	ocf_log debug "Validating configuration for $ORACLE_SID"

	# If the oracle user doesn't exist, we're done.
	[ -n "$ORACLE_USER" ] || oops "ORACLE_USER"
	id -u $ORACLE_USER > /dev/null || oops "ORACLE_USER"
	id -g $ORACLE_USER > /dev/null || oops "ORACLE_GROUP"

	# If the oracle home isn't a directory, we're done
	[ -n "$ORACLE_HOME" ] || oops "ORACLE_HOME"

	# If the oracle SID is NULL, we're done
	[ -n "$ORACLE_SID" ] || oops "ORACLE_SID"

	# Super user? Automatically change UID and exec as oracle user.
	# Oracle needs to be run as the Oracle user, not root!
	if [ "`id -u`" = "0" ]; then
		su $ORACLE_USER -c "$0 $*"
		exit $?
	fi

	# If we're not root and not the Oracle user, we're done.
	[ "`id -u`" = "`id -u $ORACLE_USER`" ] || oops "not ORACLE_USER after su"
	[ "`id -g`" = "`id -g $ORACLE_USER`" ] || oops "not ORACLE_GROUP after su"

	# Go home.
	cd "$ORACLE_HOME"

	ocf_log debug "Validation checks for $ORACLE_SID succeeded"
	return 0
}


#
# Start Oracle
#
start_oracle() {
	ocf_log info "Starting service $ORACLE_SID"

	start_db
	rv=$?
	if [ $rv -ne 0 ]; then
		ocf_log error "Starting service $ORACLE_SID failed"
		return 1
	fi

	for LISTENER in ${LISTENERS}; do
		ocf_log info "Starting listener $LISTENER"
		lsnrctl_stdout=$(lsnrctl start "$LISTENER")
		rv=$?
		if [ $rv -ne 0 ]; then
			ocf_log debug "[$ORACLE_SID] Listener $LISTENER start returned $rv output $lsnrctl_stdout"
			ocf_log error "Starting service $ORACLE_SID failed"
			return 1
		fi
	done

	if [ -n "$LOCKFILE" ]; then
		touch "$LOCKFILE"
	fi

	ocf_log info "Starting service $ORACLE_SID completed successfully"
	return 0
}


#
# Stop Oracle
#
stop_oracle() {
	ocf_log info "Stopping service $ORACLE_SID"

	if ! [ -e "$ORACLE_HOME/bin/lsnrctl" ]; then
		ocf_log error "Oracle Listener Control is not available ($ORACLE_HOME not mounted?)"
		# XXX should this return 1?
		return 0
	fi

	stop_db || stop_db abort
	if [ $? -ne 0 ]; then
		ocf_log error "Unable to stop DB for $ORACLE_SID"
		return 1
	fi

	for LISTENER in ${LISTENERS}; do
		ocf_log info "Stopping listener $LISTENER for $ORACLE_SID"
		lsnrctl_stdout=$(lsnrctl stop "$LISTENER")
		rv=$?
		if [ $? -ne 0 ]; then
			ocf_log error "Listener $LISTENER stop failed for $ORACLE_SID: $rv output $lsnrctl_stdout"
			# XXX - failure?
		fi
	done

	exit_idle

	if [ $? -ne 0 ]; then
		ocf_log error "WARNING: Not all Oracle processes exited cleanly for $ORACLE_SID"
		# XXX - failure?
	fi

	if [ -n "$LOCKFILE" ]; then
		rm -f "$LOCKFILE"
	fi

	ocf_log info "Stopping service $ORACLE_SID succeeded"
	return 0
}


#
# Find and display the status of iAS infrastructure.
#
# This has three parts:
# (1) Oracle database itself
# (2) Oracle listener process
# (3) OPMN and OPMN-managed processes
#
# - If all are (cleanly) down, we return 3.  In order for this to happen,
# $LOCKFILE must not exist.  In this case, we try and restart certain parts
# of the service - as this may be running in a clustered environment.
#
# - If some but not all are running (and, if $LOCKFILE exists, we could not
# restart the failed portions), we return 1 (ERROR)
#
# - If all are running, return 0.  In the "all-running" case, we recreate
# $LOCKFILE if it does not exist.
#
status_oracle() {
	declare -i subsys_lock=1
	declare -i last 
	declare -i depth=$1

	ocf_log debug "Checking status for $ORACLE_SID depth $depth"

	# Check for lock file. Crude and rudimentary, but it works
	if [ -z "$LOCKFILE" ] || [ -f "$LOCKFILE" ]; then
		subsys_lock=0 
	fi

	# Check database status
	get_db_status $subsys_lock $depth
	update_status $? # Start
	last=$?

	# Check & report listener status
	for LISTENER in ${LISTENERS}; do
		get_lsnr_status $subsys_lock $depth "$LISTENER"
		update_status $? $last
		last=$?
	done
	
	# No lock file, but everything's running.  Put the lock
	# file back. XXX - this kosher?
	if [ $last -eq 0 ] && [ $subsys_lock -ne 0 ]; then
		touch "$LOCKFILE"
	fi

	ocf_log debug "Status returning $last for $ORACLE_SID"
	return $last
}


########################
# Do some real work... #
########################

case $1 in
	meta-data)
		cat `echo $0 | sed 's/^\(.*\)\.sh$/\1.metadata/'`
		exit 0
		;;
	start)
		validation_checks $*
		start_oracle
		exit $?
		;;
	stop)
		validation_checks $*
		stop_oracle
		exit $?
		;;
	status|monitor)
		validation_checks $*
		status_oracle $OCF_CHECK_LEVEL
		exit $?
		;;
	restart)
		$0 stop || exit $?
		$0 start || exit $?
		exit 0
		;;
	*)
		echo "usage: $SCRIPT {start|stop|restart|status|monitor|meta-data}"
		exit 1
		;;
esac

exit 0
