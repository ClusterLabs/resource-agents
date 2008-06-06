#!/bin/bash

export LC_ALL=C
export LANG=C
export PATH=/bin:/sbin:/usr/bin:/usr/sbin

. $(dirname $0)/ocf-shellfuncs
. $(dirname $0)/utils/config-utils.sh
. $(dirname $0)/utils/messages.sh
. $(dirname $0)/utils/ra-skelet.sh

declare LDAP_SLAPD=/usr/sbin/slapd
declare LDAP_pid_file="`generate_name_for_pid_file`"
declare LDAP_conf_dir="`generate_name_for_conf_dir`"
declare LDAP_gen_config_file="$LDAP_conf_dir/slapd.conf"
declare LDAP_url_list

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

generate_url_list()
{
	declare ldap_url_source=$1
	declare ip_addresses=$2
	declare url_list
	declare tmp;
	
	for u in $ldap_url_source; do 
		if [[ "$u" =~ ':///' ]]; then
			for z in $ip_addresses; do
				tmp=`echo $u | sed "s,://,://$z,"`
				url_list="$url_list $tmp"
			done
		elif [[ "$u" =~ '://0:' ]]; then
			for z in $ip_addresses; do
				tmp=`echo $u | sed "s,://0:,://$z:,"`
				url_list="$url_list $tmp"
			done
		else
			url_list="$url_list $u"
		fi
	done
	
	echo $url_list
}

generate_config_file()
{
	declare original_file="$1"
	declare generated_file="$2"

	if [ -f "$generated_file" ]; then
		sha1_verify "$generated_file"
		if [ $? -ne 0 ]; then
			clog_check_sha1 $CLOG_FAILED
			return 0
		fi
	fi	

	clog_generate_config $CLOG_INIT "$original_file" "$generated_file"

	generate_configTemplate "$generated_file" "$1"
	echo "pidfile \"$LDAP_pid_file\"" >> $generated_file
	echo >> $generated_file	
	sed 's/^[[:space:]]*pidfile/### pidfile/i' < "$original_file" >> "$generated_file"
	
        sha1_addToFile "$generated_file"
	clog_generate_config $CLOG_SUCCEED "$original_file" "$generated_file"
               
	return 0;
}

start()
{
	declare ccs_fd;
	
	clog_service_start $CLOG_INIT

	create_pid_directory
	create_conf_directory "$LDAP_conf_dir"
	check_pid_file "$LDAP_pid_file"

	if [ $? -ne 0 ]; then
		clog_check_pid $CLOG_FAILED "$LDAP_pid_file"
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

	LDAP_url_list=`generate_url_list "$OCF_RESKEY_url_list" "$ip_addresses"`

	if [ -z "$LDAP_url_list" ]; then
		ocf_log error "Generating URL List for $OCF_RESOURCE_INSTANCE > Failed"
		return $OCF_ERR_GENERIC
	fi

	generate_config_file "$OCF_RESKEY_config_file" "$LDAP_gen_config_file"

	$LDAP_SLAPD -f "$LDAP_gen_config_file" -n "$OCF_RESOURCE_INSTANCE" \
		-h "$LDAP_url_list" $OCF_RESKEY_slapd_options

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

	stop_generic "$LDAP_pid_file" "$OCF_RESKEY_shutdown_wait"
	
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

	status_check_pid "$LDAP_pid_file"
	if [ $? -ne 0 ]; then
		clog_service_status $CLOG_FAILED "$LDAP_pid_file"
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
