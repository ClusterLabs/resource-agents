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
#  Description:
#	Catalog of log messages for resources agents
#
#  Author(s):
#       Marek Grac (mgrac at redhat.com)
#

declare CLOG_INIT=100
declare CLOG_SUCCEED=200

declare CLOG_FAILED=400
declare CLOG_FAILED_TIMEOUT=401
declare CLOG_FAILED_CCS=402
declare CLOG_FAILED_NOT_FOUND=403
declare CLOG_FAILED_INVALID=404
declare CLOG_FAILED_NOT_READABLE=405

##
## Usage:
##	clog_service_start %operation%
##
clog_service_start()
{
	case $1 in
		$CLOG_INIT)
			ocf_log info "Starting Service $OCF_RESOURCE_INSTANCE"
			;;
		$CLOG_SUCCEED)
			ocf_log debug "Starting Service $OCF_RESOURCE_INSTANCE > Succeed"
			;;
		$CLOG_FAILED)
			ocf_log error "Starting Service $OCF_RESOURCE_INSTANCE > Failed"
			;;
		$CLOG_FAILED_TIMEOUT)
			ocf_log error "Starting Service $OCF_RESOURCE_INSTANCE > Failed - Timeout Error"
			;;
	esac
	return 0
}

##
## Usage:
##	clog_service_stop %operation%
##
clog_service_stop()
{
	case $1 in
		$CLOG_INIT)
			ocf_log info "Stopping Service $OCF_RESOURCE_INSTANCE"
			;;
		$CLOG_SUCCEED)
			ocf_log info "Stopping Service $OCF_RESOURCE_INSTANCE > Succeed"
			;;
		$CLOG_FAILED)
			ocf_log error "Stopping Service $OCF_RESOURCE_INSTANCE > Failed"
			;;
		$CLOG_FAILED_NOT_STOPPED)
			ocf_log error "Stopping Service $OCF_RESOURCE_INSTANCE > Failed - Application Is Still Running"
			;;
	esac
	return 0
}

##
## Usage:
##	clog_service_status %operation%
##
clog_service_status()
{
	case $1 in
		$CLOG_INIT)
			ocf_log debug "Monitoring Service $OCF_RESOURCE_INSTANCE"
			;;
		$CLOG_SUCCEED)
			ocf_log debug "Monitoring Service $OCF_RESOURCE_INSTANCE > Service Is Running"
			;;
		$CLOG_FAILED)
			ocf_log error "Monitoring Service $OCF_RESOURCE_INSTANCE > Service Is Not Running"
			;;
		$CLOG_FAILED_NOT_FOUND)
			ocf_log error "Monitoring Service $OCF_RESOURCE_INSTANCE > Service Is Not Running - PID File Not Found"
			;;
	esac
	return 0
}

##
## Usage:
##	clog_service_verify %operation%
##	clog_service_verify $CLOG_FAILED %reason%
##
clog_service_verify()
{
	case $1 in
		$CLOG_INIT)
			ocf_log debug "Verifying Configuration Of $OCF_RESOURCE_INSTANCE"
			;;
		$CLOG_SUCCEED)
			ocf_log debug "Verifying Configuration Of $OCF_RESOURCE_INSTANCE > Succeed"
			;;
		$CLOG_FAILED_NOT_CHILD)
			ocf_log error "Service $OCF_RESOURCE_INSTANCE Is Not A Child Of A Service"
			;;
		$CLOG_FAILED)
			if [ "x$2" = "x" ]; then
				ocf_log error "Verifying Configuration Of $OCF_RESOURCE_INSTANCE > Failed"
			else
				ocf_log error "Verifying Configuration Of $OCF_RESOURCE_INSTANCE > Failed - $2"
			fi
			;;
	esac
	return 0
}


##
## Usage:
##	clog_check_sha1 %operation% %filename%
##
clog_check_sha1()
{
	case $1 in
		$CLOG_INIT)
			ocf_log debug "Checking SHA1 Checksum Of File $1"
			;;
		$CLOG_SUCCEED)
			ocf_log debug "Checking SHA1 Checksum Of File > Succeed"
			;;
		$CLOG_FAILED)
			ocf_log debug "Checking SHA1 Checksum Of File > Failed - File Changed"
			;;
	esac
	return 0;
} 

##
## Usage:
##	clog_check_file_exist %operation% %filename%
##
clog_check_file_exist()
{
	case $1 in
		$CLOG_INIT)
			ocf_log debug "Checking Existence Of File $2"
			;;
		$CLOG_SUCCEED)
			ocf_log debug "Checking Existence Of File $2 > Succeed"
			;;
		$CLOG_FAILED)
			ocf_log error "Checking Existence Of File $2 [$OCF_RESOURCE_INSTANCE] > Failed"
			;;
		$CLOG_FAILED_INVALID)
			ocf_log error "Checking Existence Of File $2 [$OCF_RESOURCE_INSTANCE] > Failed - Invalid Argument"
			;;
		$CLOG_FAILED_NOT_FOUND)
			ocf_log error "Checking Existence Of File $2 [$OCF_RESOURCE_INSTANCE] > Failed - File Doesn't Exist"
			;;
		$CLOG_FAILED_NOT_READABLE)
			ocf_log error "Checking Existence Of File $2 [$OCF_RESOURCE_INSTANCE] > Failed - File Is Not Readable"
			;;
	esac
	return 0;
} 

##
## Usage:
##	clog_check_pid %operation% %filename%
##
clog_check_pid()
{
	case $1 in
		$CLOG_INIT)
			ocf_log debug "Checking Non-Existence Of PID File $2"
			return 0
			;;
		$CLOG_SUCCEED)
			ocf_log debug "Checking Non-Existence of PID File $2 > Succeed"
			;;
		$CLOG_FAILED)
			ocf_log error "Checking Non-Existence of PID File $2 [$OCF_RESOURCE_INSTANCE] > Failed - PID File Exists For $OCF_RESOURCE_INSTANCE"
			;;
	esac
	return 0;
}

##
## Usage:
##	clog_check_syntax %operation% %filename%
##
clog_check_syntax()
{
	case $1 in
		$CLOG_INIT)
			ocf_log debug "Checking Syntax Of The File $2"
			;;
		$CLOG_SUCCEED)
			ocf_log debug "Checking Syntax Of The File $2 > Succeed"
			;;
		$CLOG_FAILED)
			ocf_log error "Checking Syntax Of The File $2 [$OCF_RESOURCE_INSTANCE] > Failed"
			;;		
	esac
	return 0;
}

##
## Usage:
##	clog_generate_config %operation% %old filename% %new filename%
##
clog_generate_config()
{
	case $1 in
		$CLOG_INIT)
			ocf_log debug "Generating New Config File $3 From $2"
			;;
		$CLOG_SUCCEED)
			ocf_log debug "Generating New Config File $3 From $2 > Succeed"
			;;
		$CLOG_FAILED)
			ocf_log error "Generating New Config File $3 From $2 [$OCF_RESOURCE_INSTANCE] > Failed"
			;;		
	esac
	return 0;
}

##
## Usage:
##	clog_looking_for %operation% %resource%
##	clog_looking_for %operation% "IP Addresses"
##	clog_looking_for %operation% "Filesystems"
##
clog_looking_for()
{
	case $1 in
		$CLOG_INIT)
			ocf_log debug "Looking For $2"
			;;
		$CLOG_SUCCEED)
			ocf_log debug "Looking For $2 > Succeed - $3 $2 Found"
			;;
		$CLOG_FAILED)
			ocf_log error "Looking For $2 [$OCF_RESOURCE_INSTANCE] > Failed"
			;;		
		$CLOG_FAILED_CCS)
			ocf_log error "Looking For $2 [$OCF_RESOURCE_INSTANCE] > Failed - Unable To Connect To \"ccs\""
			;;		
		$CLOG_FAILED_NOT_FOUND)
			ocf_log error "Looking For $2 [$OCF_RESOURCE_INSTANCE] > Failed - No $2 Found"
			;;		
	esac
	return 0;
}
