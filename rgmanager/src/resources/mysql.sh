#!/bin/bash

#
#  Copyright Red Hat, Inc. 2006
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
#  Author(s):
#	Marek Grac (mgrac at redhat.com)
#

export LC_ALL=C
export LANG=C
export PATH=/bin:/sbin:/usr/bin:/usr/sbin

. $(dirname $0)/ocf-shellfuncs
. $(dirname $0)/utils/config-utils.sh
. $(dirname $0)/utils/messages.sh
. $(dirname $0)/utils/ra-skelet.sh

declare MYSQL_MYSQLD=/usr/bin/mysqld_safe
declare MYSQL_ipAddress
declare MYSQL_pid_file="`generate_name_for_pid_file`"
declare MYSQL_timeout=30

verify_all()
{
	clog_service_verify $CLOG_INIT

	if [ -z "$OCF_RESKEY_name" ]; then
		clog_service_verify $CLOG_FAILED "Invalid Name Of Service"
		return $OCF_ERR_ARGS
	fi

	if [ -z "$OCF_RESKEY_config_file" ]; then
		clog_check_file_exist $CLOG_FAILED_INVALID "$OCF_RESKEY_config_file"
		clog_service_verify $CLOG_FAILED
		return $OCF_ERR_ARGS
	fi

	if [ ! -r "$OCF_RESKEY_config_file" ]; then
		clog_check_file_exist $CLOG_FAILED_NOT_READABLE $OCF_RESKEY_config_file
		clog_service_verify $CLOG_FAILED
		return $OCF_ERR_ARGS
	fi

	if [ -z "$MYSQL_pid_file" ]; then
		clog_service_verify $CLOG_FAILED "Invalid name of PID file"
		return $OCF_ERR_ARGS
	fi

	clog_service_verify $CLOG_SUCCEED
	return 0
}

start()
{
	declare ccs_fd;
	
	clog_service_start $CLOG_INIT

	create_pid_directory

	if [ -e "$MYSQL_pid_file" ]; then
		clog_check_pid $CLOG_FAILED "$MYSQL_pid_file"
		clog_service_start $CLOG_FAILED
		return $OCF_ERR_GENERIC
	fi

	if [ -n "$OCF_RESKEY_listen_address" ]; then
		MYSQL_ipAddress="$OCF_RESKEY_listen_address"
	else
		clog_looking_for $CLOG_INIT "IP Address"

	        ccs_fd=$(ccs_connect);
	        if [ $? -ne 0 ]; then
			clog_looking_for $CLOG_FAILED_CCS
	                return $OCF_ERR_GENERIC
	        fi

	        get_service_ip_keys "$ccs_fd" "$OCF_RESKEY_service_name"
	        ip_addresses=`build_ip_list "$ccs_fd"`

		if [ -n "$ip_addresses" ]; then
			for i in $ip_addresses; do
				MYSQL_ipAddress="$i"
				break;
			done
		else
			clog_looking_for $CLOG_FAILED_NOT_FOUND "IP Address"
		fi
	fi

	clog_looking_for $CLOG_SUCCEED "IP Address"

	$MYSQL_MYSQLD --defaults-file="$OCF_RESKEY_config_file" \
		--pid-file="$MYSQL_pid_file" \
		--bind-address="$MYSQL_ipAddress" \
		$OCF_RESKEY_mysqld_options > /dev/null 2>&1 &

	if [ $? -ne 0 ]; then
		clog_service_start $CLOG_FAILED
		return $OCF_ERR_GENERIC
	fi

	while [ "$MYSQL_timeout" -gt 0 ]; do
		if [ -f "$MYSQL_pid_file" ]; then
			break;			
		fi
		sleep 1
		let MYSQL_timeout=${MYSQL_timeout}-1
        done

        if [ "$MYSQL_timeout" -eq 0 ]; then
		clog_service_start $CLOG_FAILED_TIMEOUT
		return $OCF_ERR_GENERIC
	fi
	
	clog_service_start $CLOG_SUCCEED

	return 0;
}

stop()
{
	clog_service_stop $CLOG_INIT

	stop_generic "$MYSQL_pid_file"
	
	if [ $? -ne 0 ]; then
		clog_service_stop $CLOG_FAILED
		return $OCF_ERR_GENERIC
	fi
	
	clog_service_stop $CLOG_SUCCEED
	return 0;
}

status()
{
	clog_service_status $CLOG_INIT

	status_check_pid "$MYSQL_pid_file"
	if [ $? -ne 0 ]; then
		clog_service_status $CLOG_FAILED "$MYSQL_pid_file"
		return $OCF_ERR_GENERIC
	fi

	clog_service_status $CLOG_SUCCEED
	return 0
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
	status|monitor)
		verify_all
		status
		exit $?
		;;
	restart)
		verify_all
		stop
		start
		exit $?
		;;
	*)
		echo "Usage: $0 {start|stop|status|monitor|restart|meta-data|verify-all}"
		exit $OCF_ERR_GENERIC
		;;
esac
