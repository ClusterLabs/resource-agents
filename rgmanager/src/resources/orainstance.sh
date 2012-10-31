#!/bin/bash
#
# Copyright 2003-2004, 2006-2011 Red Hat, Inc.
#
# Author(s):
#     Hardy Merrill <hmerrill at redhat.com>
#     Lon Hohberger <lhh at redhat.com>
#     Michael Moon <Michael dot Moon at oracle.com>
#
# This program is Open Source software.  You may modify and/or redistribute
# it persuant to the terms of the Open Software License version 2.1, which
# is available from the following URL and is included herein by reference:
#
# 	http://opensource.org/licenses/osl-2.1.php
#
# chkconfig: 345 99 01
# description: Service script for starting/stopping      \
#	       Oracle(R) Database 10g on                 \
#		        Red Hat Enterprise Linux 5
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

. /etc/init.d/functions

declare SCRIPT="`basename $0`"
declare SCRIPTDIR="`dirname $0`"

# Required parameters from rgmanager
ORACLE_USER=$OCF_RESKEY_user
ORACLE_HOME=$OCF_RESKEY_home
ORACLE_SID=$OCF_RESKEY_name

# Optional parameters with default values
LISTENERS=$OCF_RESKEY_listeners
LOCKFILE="/tmp/.oracle10g-${ORACLE_SID}.lock"
[ -n "$OCF_RESKEY_lockfile" ] && LOCKFILE=$OCF_RESKEY_lockfile

export LISTENERS ORACLE_USER ORACLE_HOME ORACLE_SID LOCKFILE
export LD_LIBRARY_PATH=$ORACLE_HOME/lib
export PATH=$ORACLE_HOME/bin:$PATH

declare -i	RESTART_RETRIES=3
declare -r	DB_PROCNAMES="pmon"
declare -r	LSNR_PROCNAME="tnslsnr"


#
# Start Oracle (database portion)
#
start_db() {
	declare tmpfile
	declare logfile
	declare -i rv

	tmpfile=/tmp/$SCRIPT-start.$$
	logfile=/tmp/$SCRIPT-start.log.$$

	# Set up our sqlplus script.  Basically, we're trying to 
	# capture output in the hopes that it's useful in the case
	# that something doesn't work properly.
	echo "startup" > $tmpfile
	echo "quit" >> $tmpfile

	sqlplus "/ as sysdba" < $tmpfile > $logfile
	rv=$?

	rm -f $tmpfile

	# Dump logfile to /var/log/messages
	initlog -q -c "cat $logfile"
	
	if [ $rv -ne 0 ]; then
        rm -f $logfile
        initlog -n $SCRIPT -q -s "sqlplus returned 1, failed"
		return 1
	fi

	# If we see:
	# ORA-.....: failure, we failed
	grep -q "^ORA-" $logfile
	rv=$?

    rm -f $logfile
	if [ $rv -eq 0 ]; then
        initlog -n $SCRIPT -q -s "found failure in stdout, returning 1"
		return 1
	fi

	return 0
}


#
# Stop Oracle (database portion)
#
stop_db() {
	declare tmpfile
	declare logfile
	declare -i rv

	tmpfile=/tmp/$SCRIPT-stop.$$
	logfile=/tmp/$SCRIPT-stop.log.$$

    ora_procname="ora_${DB_PROCNAMES}_${ORACLE_SID}"
    status $ora_procname
    if [ $? -ne 0 ]; then
        # No pmon process found, db already down
        return 0
    fi

	# Setup for Stop ...
	echo "shutdown immediate" > $tmpfile
	echo "quit" >> $tmpfile

	sqlplus "/ as sysdba" < $tmpfile > $logfile
	rv=$?

	rm -f $tmpfile

	# Dump logfile to /var/log/messages
	initlog -q -c "cat $logfile"
	
    # sqlplus returned failure. We'll return failed to rhcs
	if [ $rv -ne 0 ]; then
        rm -f $logfile
        initlog -n $SCRIPT -q -s "sqlplus returned 1, failed"
		return 1
	fi

	grep -q "^ORA-" $logfile
    rv=$?
    rm -f $logfile

	# If we see 'failure' in the log, we're done.
	if [ $rv -eq 0 ]; then
        initlog -n $SCRIPT -q -s "found failure in stdout, returning 1"
		return 1
	fi

	return 0
}


#
# Destroy any remaining processes with refs to $ORACLE_SID
#
force_cleanup() {
	declare pids
	declare pid

	pids=`ps ax | grep $ORACLE_SID | grep -v grep | awk '{print $1}'`

	initlog -n $SCRIPT -s "<err> Not all Oracle processes exited cleanly, killing"
	
	for pid in $pids; do
		kill -9 $pid
		if [ $? -eq 0 ]; then
			initlog -n $SCRIPT -s "Killed $pid"
		fi
	done

	return 0
}


#
# Wait for oracle processes to exit.  Time out after 60 seconds
#
exit_idle() {
	declare -i n=0
	
	while ps ax | grep $ORACLE_SID | grep -q -v $LSNR_PROCNAME | grep -q -v grep; do
		if [ $n -ge 90 ]; then
			force_cleanup
			return 0
		fi
		sleep 1
		((n++))
	done
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
			return 3
		fi

		for (( i=$RESTART_RETRIES ; i; i-- )) ; do
			# this db process is down - stop and
			# (re)start all ora_XXXX_$ORACLE_SID processes
			initlog -q -n $SCRIPT -s "Restarting Oracle Database..."
			stop_db

			start_db
			if [ $? == 0 ] ; then
				# ora_XXXX_$ORACLE_SID processes started
				# successfully, so break out of the
				# stop/start # 'for' loop
				break
			fi
		done

		if [ $i -eq 0 ]; then
			# stop/start's failed - return 1 (failure)
            initlog -q -n $SCRIPT -s "Restart failed, retuning 1"
			return 1
		fi
	done
	return 0
}


#
# Get the status of the Oracle listener process
#
get_lsnr_status() {
	declare -i subsys_lock=$1
	declare -i rv
    declare -r LISTENER=$3

    lsnrctl status $LISTENER >& /dev/null
	rv=$?
	if [ $rv == 0 ] ; then
		return 0 # Listener is running fine
	fi

	# We're not supposed to be running, and we are,
	# in fact, not running.  Return 3
	if [ $subsys_lock -ne 0 ]; then
		return 3
	fi

	# Listener is NOT running (but should be) - try to restart
	for (( i=$RESTART_RETRIES ; i; i-- )) ; do
        initlog -n $SCRIPT -q -s "Restarting Oracle listener ($LISTENER)"
		lsnrctl start $LISTENER
		lsnrctl status $LISTENER >& /dev/null
		if [ $? == 0 ] ; then
			break # Listener was (re)started and is running fine
		fi
	done

	if [ $i -eq 0 ]; then
		# stop/start's failed - return 1 (failure)
        initlog -n $SCRIPT -q -s "Listener restart failed, retuning 1"
		return 1
	fi

    lsnrctl status $LISTENER >& /dev/null
	if [ $? != 0 ] ; then
        initlog -n $SCRIPT -q -s "Listener status failed, retuning 1"
		return 1 # Problem restarting the Listener
	fi
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
        initlog -n $SCRIPT -q -s "$old_status vs $new_status - returning 1"
		return 1
	fi

	return $old_status
}


#
# Print an error message to the user and exit.
#
oops() {
	#echo "Please configure this script ($0) to"
	#echo "match your installation."
	#echo 
	#echo "    $1 failed validation checks."
    initlog -n $SCRIPT -q -s "$1 failed validation checks"
	exit 1
}


#
# Do some validation on the user-configurable stuff at the beginning of the
# script.
#
validation_checks() {
	# If the oracle user doesn't exist, we're done.
	[ -n "$ORACLE_USER" ] || oops "ORACLE_USER"
	id -u $ORACLE_USER > /dev/null || oops "ORACLE_USER"
	id -g $ORACLE_USER > /dev/null || oops "ORACLE_USER"

	# If the oracle home isn't a directory, we're done
	[ -n "$ORACLE_HOME" ] || oops ORACLE_HOME

	# If the oracle SID is NULL, we're done
	[ -n "$ORACLE_SID" ] || oops ORACLE_SID

	# Super user? Automatically change UID and exec as oracle user.
	# Oracle needs to be run as the Oracle user, not root!
	if [ "`id -u`" = "0" ]; then
		su $ORACLE_USER -c "$0 $*"
		exit $?
	fi

	# If we're not root and not the Oracle user, we're done.
	[ "`id -u`" = "`id -u $ORACLE_USER`" ] || exit 1
	[ "`id -g`" = "`id -g $ORACLE_USER`" ] || exit 1

	# Go home.
	cd $ORACLE_HOME

	return 0
}


#
# Start Oracle
#
start_oracle() {
    initlog -n $SCRIPT -q -s "Starting Oracle Database"
	start_db || return 1
	
    for LISTENER in ${LISTENERS}; do
        logfile=/tmp/$SCRIPT-lsn-$$.log
        initlog -n $SCRIPT -q -s "Starting Oracle Listener $LISTENER"
        lsnrctl start $LISTENER > $logfile
        initlog -q -c "cat $logfile"
        rm -f $logfile
    done

	if [ -n "$LOCKFILE" ]; then
		touch $LOCKFILE
	fi
	return 0
}


#
# Stop Oracle
#
stop_oracle() {
	if ! [ -e "$ORACLE_HOME/bin/lsnrctl" ]; then
		initlog -n $SCRIPT -q -s "Oracle Listener Control is not available ($ORACLE_HOME not mounted?)"
		return 0
	fi

    initlog -n $SCRIPT -q -s "Stopping Oracle Database"
    stop_db || return 1

	
    for LISTENER in ${LISTENERS}; do
        initlog -n $SCRIPT -q -s "Stopping Oracle Listener $LISTENER"
        lsnrctl stop $LISTENER
    done

    initlog -n $SCRIPT -q -s "Waiting for all Oracle processes to exit"
    exit_idle

	if [ $? -ne 0 ]; then
		initlog -n $SCRIPT -q -s "WARNING: Not all Oracle processes exited cleanly"
	fi

	if [ -n "$LOCKFILE" ]; then
		rm -f $LOCKFILE
	fi
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

	# Check for lock file. Crude and rudimentary, but it works
	if [ -z "$LOCKFILE" ] || [ -f $LOCKFILE ]; then
		subsys_lock=0 
	fi

	# Check database status
	get_db_status $subsys_lock $depth
	update_status $? # Start
	last=$?

	# Check & report listener status
    for LISTENER in ${LISTENERS}; do
        get_lsnr_status $subsys_lock $depth $LISTENER
        update_status $? $last
        last=$?
    done
	
	# No lock file, but everything's running.  Put the lock
	# file back. XXX - this kosher?
	if [ $last -eq 0 ] && [ $subsys_lock -ne 0 ]; then
		touch $LOCKFILE
	fi

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
