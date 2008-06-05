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

declare PSQL_POSTMASTER="/usr/bin/postmaster"
declare PSQL_pid_file="`generate_name_for_pid_file`"
declare PSQL_conf_dir="`generate_name_for_conf_dir`"
declare PSQL_gen_config_file="$PSQL_conf_dir/postgresql.conf"

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

	if [ -z "$OCF_RESKEY_postmaster_user" ]; then
		clog_servicer_verify $CLOG_FAILED "Invalid User"
		return $OCF_ERR_ARGS
	fi

	clog_service_verify $CLOG_SUCCEED
		
	return 0
}

generate_config_file()
{
	declare original_file="$1"
	declare generated_file="$2"
	declare ip_addressess="$3"
	
	declare ip_comma="";

	if [ -f "$generated_file" ]; then
		sha1_verify "$generated_file"
		if [ $? -ne 0 ]; then
			clog_check_sha1 $CLOG_FAILED
			return 0
		fi
	fi	

	clog_generate_config $CLOG_INIT "$original_file" "$generated_file"

	declare x=1
	for i in $ip_addressess; do
		if [ $x -eq 1 ]; then
			x=0
			ip_comma=$i
		else
			ip_comma=$ip_comma,$i
		fi 
	done

	generate_configTemplate "$generated_file" "$1"
	echo "external_pid_file = '$PSQL_pid_file'" >> "$generated_file"
	echo "listen_address = '$ip_comma'" >> "$generated_file"

	echo >> "$generated_file"	
	sed 's/^[[:space:]]*external_pid_file/### external_pid_file/i;s/^[[:space:]]*listen_address/### listen_address/i' < "$original_file" >> "$generated_file"
	
        sha1_addToFile "$generated_file"
	clog_generate_config $CLOG_SUCCEED "$original_file" "$generated_file"
               
	return 0;
}

start()
{
	declare ccs_fd;
	
	clog_service_start $CLOG_INIT

	create_pid_directory
	create_conf_directory "$PSQL_conf_dir"
	check_pid_file "$PSQL_pid_file"

	if [ $? -ne 0 ]; then
		clog_check_pid $CLOG_FAILED "$PSQL_pid_file"
		clog_service_start $CLOG_FAILED
		return $OCF_ERR_GENERIC
	fi

	clog_looking_for $CLOG_INIT "IP Addresses"

        ccs_fd=$(ccs_connect);
        if [ $? -ne 0 ]; then
		clog_looking_for $CLOG_FAILED_CCS
                return $OCF_ERR_GENERIC
        fi

        get_service_ip_keys "$ccs_fd" "$OCF_RESKEY_service_name"
        ip_addresses=`build_ip_list "$ccs_fd"`

	if [ -z "$ip_addresses" ]; then
		clog_looking_for $CLOG_FAILED_NOT_FOUND "IP Addresses"
		return $OCF_ERR_GENERIC
	fi
	
	clog_looking_for $CLOG_SUCCEED "IP Addresses"

	generate_config_file "$OCF_RESKEY_config_file" "$PSQL_gen_config_file" "$ip_addresses"

	sudo -u "$OCF_RESKEY_postmaster_user" $PSQL_POSTMASTER -c config_file="$PSQL_gen_config_file" \
		$OCF_RESKEY_postmaster_options 

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

	stop_generic "$PSQL_pid_file" "$OCF_RESKEY_shutdown_wait"
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

	status_check_pid "$PSQL_pid_file"
	if [ $? -ne 0 ]; then
		clog_service_status $CLOG_FAILED "$PSQL_pid_file"
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
