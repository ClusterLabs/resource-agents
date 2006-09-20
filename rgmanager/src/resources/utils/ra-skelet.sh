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

	if [ ! -d /proc/`cat "$pid_file"` ]; then
		return $OCF_ERR_GENERIC
	fi	

	return 0
}

stop_generic()
{
	declare pid_file="$1"

	if [ ! -e "$pid_file" ]; then
		clog_check_file_exist $CLOG_FAILED_NOT_FOUND "$pid_file"
		return $OCF_ERR_GENERIC
	fi

	kill -TERM `cat "$pid_file"`

	if [ $? -ne 0 ]; then
		return $OCF_ERR_GENERIC
	fi
	
	return 0;
}
