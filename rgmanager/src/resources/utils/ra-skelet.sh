#!/bin/bash
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
	declare stop_timeout="$2"
	declare pid;
	declare count=0;

	if [ ! -e "$pid_file" ]; then
		clog_check_file_exist $CLOG_FAILED_NOT_FOUND "$pid_file"
		# In stop-after-stop situation there is no PID file but
		# it will be nice to check for it in stop-after-start
		# look at bug #449394
		return 0
	fi

	if [ -z "$stop_timeout" ]; then
		stop_timeout=20
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

	until [ `ps --pid "$pid" &> /dev/null; echo $?` = '1' ] || [ $count -gt $stop_timeout ]
	do
		sleep 1
		let count=$count+1
	done

	if [ $count -gt $stop_timeout ]; then
		clog_service_stop $CLOG_FAILED_NOT_STOPPED
		return $OCF_ERR_GENERIC
	fi
	
	return 0;
}

stop_generic_sigkill() {
	# Use stop_generic (kill -TERM) and if application did not stop
	# correctly then use kill -QUIT and check if it was killed
	declare pid_file="$1"
	declare stop_timeout="$2"
	declare kill_timeout="$3"
	declare pid
	
	## If stop_timeout is equal to zero then we do not want
	## to give -TERM signal at all.
	if [ $stop_timeout -ne 0 ]; then
		stop_generic "$pid_file" "$stop_timeout"
		if [ $? -eq 0 ]; then
			return 0;
		fi
	fi
	
	if [ ! -e "$pid_file" ]; then
		clog_check_file_exist $CLOG_FAILED_NOT_FOUND "$pid_file"
		# In stop-after-stop situation there is no PID file but
		# it will be nice to check for it in stop-after-start
		# look at bug #449394
		return 0
	fi
	read pid < "$pid_file"

	if [ -z "$pid" ]; then
		return 0;
	fi

	if [ ! -d "/proc/$pid" ]; then
		return 0;
	fi

	kill -QUIT "$pid"
	if [ $? -ne 0 ]; then
		return $OCF_GENERIC_ERROR
	fi
	
	sleep "$kill_timeout"
	ps --pid "$pid" &> /dev/null
	if [ $? -eq 0 ]; then
		clog_service_stop $CLOG_FAILED_KILL
		return $OCF_ERR_GENERIC
	fi
	
	clog_service_stop $CLOG_SUCCEED_KILL
	return 0
}
