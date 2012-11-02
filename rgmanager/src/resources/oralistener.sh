#!/bin/bash
#
# $Id: oralistener.sh 127 2009-08-21 09:17:52Z hevirtan $
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
PATH=/bin:/sbin:/usr/bin:/usr/sbin:$ORACLE_HOME/bin
export LC_ALL LANG PATH ORACLE_HOME

verify_all() {
    clog_service_verify $CLOG_INIT

    if [ -z "$OCF_RESKEY_name" ]; then
        clog_service_verify $CLOG_FAILED "Invalid name of service (listener name)"
        return $OCF_ERR_ARGS
    fi

    if [ -z "$OCF_RESKEY_home" ]; then
        clog_service_verify $CLOG_FAILED "No Oracle home specified."
        return $OCF_ERR_ARGS
    fi

    if [ -z "$OCF_RESKEY_user" ]; then
        clog_service_verify $CLOG_FAILED "No Oracle username specified."
        return $OCF_ERR_ARGS
    fi
        
    # Make sure the lsnrctl binary is in our $PATH
    if [ ! -x $(which lsnrctl) ]; then
        clog_service_verify $CLOG_FAILED "oralistener:${OCF_RESKEY_home}: Unable to locate lsnrctl command from path! ($PATH)"
        return $OCF_ERR_GENERIC
    fi

    clog_service_verify $CLOG_SUCCEED
    return 0
}

start () {
    clog_service_start $CLOG_INIT
    
    logfile="/tmp/oracle_lsn.$$"
    su -p - $ORACLE_USER -c "lsnrctl start $LISTENER > $logfile"

    initlog -q -c "cat $logfile"
    rm -f $logfile

    clog_service_start $CLOG_SUCCEED
    return 0
}
 
stop () {
    clog_service_stop $CLOG_INIT
    
    logfile="/tmp/oracle_lsn.$$"
    su -p - $ORACLE_USER -c "lsnrctl stop $LISTENER > $logfile"

    initlog -q -c "cat $logfile"
    rm -f $logfile

    clog_service_stop $CLOG_SUCCEED
    return 0
}
 
monitor () {
    clog_service_status $CLOG_INIT
    
    su -p - $ORACLE_USER -c "lsnrctl status $LISTENER"
    rv=$?
    if [ $rv == 0 ]; then
        clog_service_status $CLOG_SUCCEED
	    return 0 # Listener is running fine
    else
        clog_service_status $CLOG_FAILED
        return $OCF_ERR_GENERIC
    fi
}

recover() {
	for (( i=$RESTART_RETRIES ; i; i-- )); do
		start
		if [ $? == 0 ] ; then
		    break
		fi
	done

	if [ $i -eq 0 ]; then
		# stop/start's failed - return 1 (failure)
		return 1
	fi

    status
	if [ $? != 0 ] ; then
		return 1 # Problem restarting the Listener
	fi

	return 0 # Success restarting the Listener
}

case $1 in
    meta-data)
        cat `echo $0 | sed 's/^\(.*\)\.sh$/\1.metadata/'`
        exit 0
        ;;
    verify-all)
        verify_all
        exit $?
        ;;
    start)
        verify_all && start
        exit $?
        ;;
    stop)
        verify_all && stop
        exit $?
        ;;
    recover)
        verify_all && recover
        exit $?
        ;;
    status|monitor)
        verify_all
        monitor
        exit $?
        ;;
    *)
        echo "Usage: $0 {start|stop|recover|monitor|status|meta-data|verify-all}"
        exit $OCF_ERR_GENERIC
        ;;
esac
