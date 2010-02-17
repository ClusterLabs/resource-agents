#!/bin/bash

status_check_pid()
{
	declare pid_file="$1"

	if [ -z "$pid_file" ]; then
		clog_check_file_exist $CLOG_FAILED_INVALID "$pid_file"
		return $OCF_ERR_GENERIC
	fi

	if [ ! -e "$pid_file" ]; then
		clog_check_file_exist $CLOG_FAILED "$pid_file"
		return $OCF_ERR_GENERIC
	fi

	read pid < "$pid_file"
	
	if [ -z "$pid" ]; then
		return $OCF_ERR_GENERIC
	fi
	
	if [ ! -d /proc/$pid ]; then
		return $OCF_ERR_GENERIC
	fi	

	return 0
}

stop_generic()
{
	declare pid_file="$1"
	declare kill_timeout="$2"
	declare pid;
	declare count=0;

	if [ ! -e "$pid_file" ]; then
		clog_check_file_exist $CLOG_FAILED_NOT_FOUND "$pid_file"
		# In stop-after-stop situation there is no PID file but
		# it will be nice to check for it in stop-after-start
		# look at bug #449394
		return 0
	fi

	if [ -z "$kill_timeout" ]; then
		kill_timeout=20
	fi

	read pid < "$pid_file"

	# @todo: PID file empty -> error?
	if [ -z "$pid" ]; then
		return 0;
	fi

	# @todo: PID is not running -> error?
	if [ ! -d "/proc/$pid" ]; then
		return 0;
	fi

	kill -TERM "$pid"

	if [ $? -ne 0 ]; then
		return $OCF_ERR_GENERIC
	fi

	until [ `ps --pid "$pid" &> /dev/null; echo $?` = '1' ] || [ $count -gt $kill_timeout ]
	do
		sleep 1
		let count=$count+1
	done

	if [ $count -gt $kill_timeout ]; then
		clog_service_stop $CLOG_FAILED_NOT_STOPPED
		return $OCF_ERR_GENERIC
	fi
	
	return 0;
}
