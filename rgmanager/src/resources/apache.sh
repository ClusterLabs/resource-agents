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

if [ -x /usr/sbin/httpd ]; then
	declare APACHE_HTTPD=/usr/sbin/httpd
elif [ -x /usr/sbin/apache2 ]; then
	declare APACHE_HTTPD=/usr/sbin/apache2
fi
declare APACHE_serverConfigFile
declare APACHE_pid_file="`generate_name_for_pid_file`"
declare APACHE_conf_dir="`generate_name_for_conf_dir`"
declare APACHE_genConfig="$APACHE_conf_dir/httpd.conf"

declare APACHE_parseConfig=$(dirname $0)/utils/httpd-parse-config.pl

apache_serverConfigFile()
{
	if $(echo $OCF_RESKEY_config_file | grep -q "^/"); then
		APACHE_serverConfigFile="$OCF_RESKEY_config_file"
	else 
		APACHE_serverConfigFile="$OCF_RESKEY_server_root/$OCF_RESKEY_config_file"
	fi

	return;
}

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
                                                	
	if [ -z "$OCF_RESKEY_server_root" ]; then
		clog_service_verify $CLOG_FAILED "Invalid ServerRoot"
		return $OCF_ERR_ARGS
	fi

	if [ ! -d "$OCF_RESKEY_server_root" ]; then
		clog_service_verify $CLOG_FAILED "ServerRoot Directory Is Missing"
		return $OCF_ERR_ARGS
	fi

	if [ -z "$OCF_RESKEY_config_file" ]; then
		clog_check_file_exist $CLOG_FAILED_INVALID "$OCF_RESKEY_config_file"
		return $OCF_ERR_ARGS
	fi

	if [ ! -r "$APACHE_serverConfigFile" ]; then
		clog_check_file_exist $CLOG_FAILED_NOT_READABLE "$APACHE_serverConfigFile"
		return $OCF_ERR_ARGS
	fi

	if [ -z "$APACHE_pid_file" ]; then
		clog_service_verify $CLOG_FAILED "Invalid name of PID file"
		return $OCF_ERR_ARGS
	fi

	clog_check_syntax $CLOG_INIT "$APACHE_serverConfigFile"

	"$APACHE_HTTPD" -t \
		-D"$OCF_RESKEY_name" \
		-d "$OCF_RESKEY_server_root" \
		-f "$APACHE_serverConfigFile" \
		$OCF_RESKEY_httpd_options &> /dev/null
		
	if [ $? -ne 0 ]; then
		clog_check_syntax $CLOG_FAILED "$APACHE_serverConfigFile"
		return $OCF_ERR_GENERIC
	fi

	clog_check_syntax $CLOG_SUCCEED "$APACHE_serverConfigFile"

	return 0
}

generate_configFile()
{
	declare originalConfigFile=$1;
	declare generatedConfigFile=$2;
	declare ip_addresses=$3;

	if [ -f "$generatedConfigFile" ]; then
		sha1_verify "$generatedConfigFile"
		if [ $? -ne 0 ]; then
			clog_check_sha1 $CLOG_FAILED
			return 0
		fi
	fi	

	clog_generate_config $CLOG_INIT "$originalConfigFile" "$generatedConfigFile"

	generate_configTemplate "$generatedConfigFile" "$1"
	cat >> "$generatedConfigFile" << EOT
# From a cluster perspective, the key fields are:
#     Listen - must be set to service floating IP address.
#     ServerRoot - path to the ServerRoot (initial value is set in service conf)
#

EOT

	IFS_old="$IFS"
	IFS=$'\n'
	for i in `"$APACHE_parseConfig" -D"$OCF_RESKEY_name" < "$originalConfigFile" | grep -P '(^Listen)|(^Port)' `; do 
		port=`echo $i | sed 's/^Listen \(.*\)/\1/;s/^Port \(.*\)/\1/'`;
		testcond=`echo $port|grep :`
		if [ $testcond ]; then
			port=`echo $port|awk -F : '{print $2}'`
		fi
		IFS=$' ';
		for z in $ip_addresses; do
			if [ "${z//:/}" != "$z" ]; then
				echo "Listen [$z]:$port" >> "$generatedConfigFile";
			else
				echo "Listen $z:$port" >> "$generatedConfigFile";
			fi
		done
		IFS=$'\n';
	done;
	IFS="$IFS_old"

	echo "PidFile \"$APACHE_pid_file\"" >> "$generatedConfigFile";
	echo >> "$generatedConfigFile"

	cat "$originalConfigFile" | sed 's/^Listen/### Listen/;s/^Port/### Port/;s/^PidFile/### PidFile/' | \
	"$APACHE_parseConfig" -D"$OCF_RESKEY_name" >> "$generatedConfigFile"

	sha1_addToFile "$generatedConfigFile"
	clog_generate_config $CLOG_SUCCEED "$originalConfigFile" "$generatedConfigFile"
}

start()
{
	if status; then
		ocf_log info "Starting Service $OCF_RESOURCE_INSTANCE > Already running"
		return $OCF_SUCCESS
	fi

	declare ip_addresses

	clog_service_start $CLOG_INIT	

	create_pid_directory
	create_conf_directory "$APACHE_conf_dir"
	check_pid_file "$APACHE_pid_file"

	if [ $? -ne 0 ]; then
		clog_check_pid $CLOG_FAILED "$APACHE_pid_file"
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

	generate_configFile "$APACHE_serverConfigFile" "$APACHE_genConfig" "$ip_addresses"

	"$APACHE_HTTPD" \
		"-D$OCF_RESKEY_name" \
		-d "$OCF_RESKEY_server_root" \
		-f "$APACHE_genConfig" \
		$OCF_RESKEY_httpd_options \
		-k start

	if [ $? -ne 0 ]; then
		clog_service_start $CLOG_FAILED		
		return $OCF_ERR_GENERIC
	else
		clog_service_start $CLOG_SUCCEED
	fi

	return 0;
}

stop()
{
	clog_service_stop $CLOG_INIT

	stop_generic "$APACHE_pid_file" "$OCF_RESKEY_shutdown_wait"
	
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

	status_check_pid "$APACHE_pid_file"
	case $? in
		$OCF_NOT_RUNNING)
			clog_service_status $CLOG_FAILED "$APACHE_pid_file"
			return $OCF_NOT_RUNNING
			;;
		0)
			clog_service_status $CLOG_SUCCEED
			exit 0
			;;
		*)
			clog_service_status $CLOG_FAILED "$APACHE_pid_file"
			return $OCF_ERR_GENERIC
			;;
	esac
}

if [ "$1" != "meta-data" ]; then
	apache_serverConfigFile
fi;
		
case $1 in
	meta-data)
		cat `echo $0 | sed 's/^\(.*\)\.sh$/\1.metadata/'`
		exit 0
		;;
	validate-all|verify-all)
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
