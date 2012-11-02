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

export LC_ALL=C
export LANG=C
export PATH=/bin:/sbin:/usr/bin:/usr/sbin

. $(dirname $0)/ocf-shellfuncs
. $(dirname $0)/utils/config-utils.sh
. $(dirname $0)/utils/messages.sh
. $(dirname $0)/utils/ra-skelet.sh

declare SAMBA_SMBD=/usr/sbin/smbd
declare SAMBA_NMBD=/usr/sbin/nmbd
declare SAMBA_pid_dir="`generate_name_for_pid_dir`"
declare SAMBA_conf_dir="`generate_name_for_conf_dir`"
declare SAMBA_smbd_pid_file="$SAMBA_pid_dir/smbd-smb.conf.pid"
declare SAMBA_nmbd_pid_file="$SAMBA_pid_dir/nmbd-smb.conf.pid"
declare SAMBA_gen_config_file="$SAMBA_conf_dir/smb.conf"

verify_all()
{
	clog_service_verify $CLOG_INIT

	if [ -z "$OCF_RESKEY_name" ]; then
		clog_service_verify $CLOG_FAILED "Invalid Name Of Service"
		return $OCF_ERR_ARGS
	fi

	if [ -z "$OCF_RESKEY_service_name" ]; then
		clog_service_verify $CLOG_FAILED_NOT_CHILD
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

	clog_service_verify $CLOG_SUCCEED
		
	return 0
}

generate_config_file()
{
	declare original_file="$1"
	declare generated_file="$2"
	declare ip_addresses="$3"

	if [ -f "$generated_file" ]; then
		sha1_verify "$generated_file"
		if [ $? -ne 0 ]; then
			clog_check_sha1 $CLOG_FAILED
			return 0
		fi
	fi	

	clog_generate_config $CLOG_INIT "$original_file" "$generated_file"

	generate_configTemplate "$generated_file" "$1"

	echo "pid directory = \"$SAMBA_pid_dir\"" >> "$generated_file"
	echo "interfaces = $ip_addresses" >> "$generated_file"
	echo "bind interfaces only = Yes" >> "$generated_file"
	echo "netbios name = ${OCF_RESKEY_name/ /_}" >> "$generated_file"
	echo >> "$generated_file"	
	sed 's/^[[:space:]]*pid directory/### pid directory/i;s/^[[:space:]]*interfaces/### interfaces/i;s/^[[:space:]]*bind interfaces only/### bind interfaces only/i;s/^[[:space:]]*netbios name/### netbios name/i' \
	     < "$original_file" >> "$generated_file"
	
        sha1_addToFile "$generated_file"
	clog_generate_config $CLOG_SUCCEED "$original_file" "$generated_file"
               
	return 0;
}

start()
{
	clog_service_start $CLOG_INIT

	create_pid_directory
	mkdir -p "$SAMBA_pid_dir"
	create_conf_directory "$SAMBA_conf_dir"
	check_pid_file "$SAMBA_smbd_pid_file"

	if [ $? -ne 0 ]; then
		clog_check_pid $CLOG_FAILED "$SAMBA_smbd_pid_file"
		clog_service_start $CLOG_FAILED
		return $OCF_ERR_GENERIC
	fi

	check_pid_file "$SAMBA_nmbd_pid_file"

	if [ $? -ne 0 ]; then
		clog_check_pid $CLOG_FAILED "$SAMBA_nmbd_pid_file"
		clog_service_start $CLOG_FAILED
		return $OCF_ERR_GENERIC
	fi

	clog_looking_for $CLOG_INIT "IP Addresses"

        get_service_ip_keys "$OCF_RESKEY_service_name"
        ip_addresses=`build_ip_list`

	if [ -z "$ip_addresses" ]; then
		clog_looking_for $CLOG_FAILED_NOT_FOUND "IP Addresses"
		return $OCF_ERR_GENERIC
	fi
	
	clog_looking_for $CLOG_SUCCEED "IP Addresses"

	generate_config_file "$OCF_RESKEY_config_file" "$SAMBA_gen_config_file" "$ip_addresses"

	$SAMBA_SMBD -D -s "$SAMBA_gen_config_file" $OCF_RESKEY_smbd_options

	if [ $? -ne 0 ]; then
		clog_service_start $CLOG_FAILED
		return $OCF_ERR_GENERIC
	fi

	$SAMBA_NMBD -D -s "$SAMBA_gen_config_file" $OCF_RESKEY_nmbd_options	

	if [ $? -ne 0 ]; then
		clog_service_start $CLOG_FAILED
		return $OCF_ERR_GENERIC
	fi
	 
	clog_service_start $CLOG_SUCCEED

	return 0;
}

stop()
{
	clog_service_stop $CLOG_INIT

	stop_generic "$SAMBA_smbd_pid_file" "$OCF_RESKEY_shutdown_wait"
	
	if [ $? -ne 0 ]; then
		clog_service_stop $CLOG_FAILED
		return $OCF_ERR_GENERIC
	fi

	stop_generic "$SAMBA_nmbd_pid_file"
	
	if [ $? -ne 0 ]; then
		clog_service_stop $CLOG_FAILED
		return $OCF_ERR_GENERIC
	fi

	if [ -e "$SAMBA_smbd_pid_file" ]; then
		rm -f "$SAMBA_smbd_pid_file"
	fi

	if [ -e "$SAMBA_nmbd_pid_file" ]; then
		rm -f "$SAMBA_nmbd_pid_file"
	fi
	
	clog_service_stop $CLOG_SUCCEED
	return 0;
}

status()
{
	clog_service_status $CLOG_INIT

	status_check_pid "$SAMBA_smbd_pid_file"

	if [ $? -ne 0 ]; then
		clog_service_status $CLOG_FAILED "$SAMBA_smbd_pid_file"
		return $OCF_ERR_GENERIC
	fi

	status_check_pid "$SAMBA_nmbd_pid_file"

	if [ $? -ne 0 ]; then
		clog_service_status $CLOG_FAILED "$SAMBA_nmbd_pid_file"
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
	validate-all)
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
		echo "Usage: $0 {start|stop|status|monitor|restart|meta-data|validate-all}"
		exit $OCF_ERR_UNIMPLEMENTED
		;;
esac
