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

declare RA_COMMON_pid_dir=/var/run/cluster
declare RA_COMMON_conf_dir=/etc/cluster

declare -i FAIL=255
declare -a ip_keys

generate_configTemplate()
{
	cat > "$1" << EOT
#
# "$1" was created from the "$2"
#
# This template configuration was automatically generated, and will be
# automatically regenerated if removed. Once this file has been altered,
# automatic re-generation will stop. Remember to copy this file to all 
# other cluster members after making changes, or your service will not 
# operate correctly.
#
EOT
}

generate_configTemplateXML()
{
	cat > "$1" << EOT
<!--
  "$1" was created from the "$2"

  This template configuration was automatically generated, and will be
  automatically regenerated if removed. Once this file has been altered,
  automatic re-generation will stop. Remember to copy this file to all 
  other cluster members after making changes, or your service will not 
  operate correctly.
-->
EOT
}

sha1_addToFile()
{
        declare sha1line="# rgmanager-sha1 $(sha1sum "$1")"
        echo $sha1line >> "$1"
}

sha1_addToFileXML()
{
        declare sha1line="<!--# rgmanager-sha1 $(sha1sum "$1")-->"
        echo $sha1line >> "$1"
}

sha1_verify()
{
	declare sha1_new sha1_old
	declare oldFile=$1

	ocf_log debug "Checking: SHA1 checksum of config file $oldFile"

	sha1_new=`cat "$oldFile" | grep -v "# rgmanager-sha1" | sha1sum | sed 's/^\([a-z0-9]\+\) .*$/\1/'`
 	sha1_old=`tail -n 1 "$oldFile" | sed 's/^\(<!--\)\?# rgmanager-sha1 \(.*\)$/\2/' | sed 's/^\([a-z0-9]\+\) .*$/\1/'`

	if [ "$sha1_new" = "$sha1_old" ]; then
	        ocf_log debug "Checking: SHA1 checksum > succeed"
		return 0;
	else
		ocf_log debug "Checking: SHA1 checksum > failed - file changed"
		return 1;
	fi
}

#
# Usage: ccs_get key
#
ccs_get()
{
	declare outp
	declare key

	[ -n "$1" ] || return $FAIL

	key="$*"

	outp=$(ccs_tool query "$key" 2>&1)
	if [ $? -ne 0 ]; then
		if [[ "$outp" =~ "Query failed: Invalid argument" ]]; then
			# This usually means that element does not exist
			# e.g. when checking for IP address 
			return 0;
		fi

		if [ "$outp" = "${outp/No data available/}" ] || [ "$outp" = "${outp/Operation not permitted/}" ]; then
			ocf_log err "$outp ($key)"
			return $FAIL
		fi

		# no real error, just no data available
		return 0
	fi

	echo $outp

	return 0
}

#
# Build a list of service IP keys; traverse refs if necessary
# Usage: get_service_ip_keys desc serviceName
#
get_service_ip_keys()
{
	declare svc=$1
	declare -i x y=0
	declare outp
	declare key

	#
	# Find service-local IP keys
	#
	x=1
	while : ; do
		key="/cluster/rm/service[@name=\"$svc\"]/ip[$x]"

		#
		# Try direct method
		#
		outp=$(ccs_get "$key/@address")
		if [ $? -ne 0 ]; then
			return 1
		fi

		#
		# Try by reference
		#
		if [ -z "$outp" ]; then
			outp=$(ccs_get "$key/@ref")
			if [ $? -ne 0 ]; then
				return 1
			fi
			key="/cluster/rm/resources/ip[@address=\"$outp\"]"
		fi

		if [ -z "$outp" ]; then
			break
		fi

		#ocf_log debug "IP $outp found @ $key"

		ip_keys[$y]="$key"

		((y++))
		((x++))
	done

	ocf_log debug "$y IP addresses found for $svc/$OCF_RESKEY_name"

	return 0
}

build_ip_list()
{
        declare ipaddrs ipaddr
        declare -i x=0
                        
        while [ -n "${ip_keys[$x]}" ]; do
              ipaddr=$(ccs_get "${ip_keys[$x]}/@address")
              if [ -z "$ipaddr" ]; then
                                   break
              fi

	      # remove netmask
	      iponly=`echo $ipaddr | sed 's/\/.*//'`
              ipaddrs="$ipaddrs $iponly"
             ((x++))
        done

        echo $ipaddrs
}

generate_name_for_pid_file()
{
	declare filename=$(basename $0)
	
	echo "$RA_COMMON_pid_dir/$(basename $0 | sed 's/^\(.*\)\..*/\1/')/$OCF_RESOURCE_INSTANCE.pid"
	
	return 0;
}

generate_name_for_pid_dir()
{
	declare filename=$(basename $0)
	
	echo "$RA_COMMON_pid_dir/$(basename $0 | sed 's/^\(.*\)\..*/\1/')/$OCF_RESOURCE_INSTANCE"
	
	return 0;
}

generate_name_for_conf_dir()
{
	declare filename=$(basename $0)

	echo "$RA_COMMON_conf_dir/$(basename $0 | sed 's/^\(.*\)\..*/\1/')/$OCF_RESOURCE_INSTANCE"
	
	return 0;
}

create_pid_directory()
{
	declare program_name="$(basename $0 | sed 's/^\(.*\)\..*/\1/')"
	declare dirname="$RA_COMMON_pid_dir/$program_name"

	if [ -d "$dirname" ]; then
		return 0;
	fi
	
	chmod 711 "$RA_COMMON_pid_dir"
	mkdir -p "$dirname"
	
	if [ "$program_name" = "mysql" ]; then
		chown mysql.root "$dirname"
	elif [ "$program_name" = "tomcat-5" -o "$program_name" = "tomcat-6" ]; then
		chown tomcat.root "$dirname"
	fi

	return 0;
}

create_conf_directory()
{
	declare dirname="$1"

	if [ -d "$dirname" ]; then
		return 0;
	fi
	
	mkdir -p "$dirname"
	
	return 0;
}

check_pid_file() {
	declare pid_file="$1"

	if [ -z "$pid_file" ]; then
		return 1;
	fi

	if [ ! -e "$pid_file" ]; then
		return 0;
	fi

	## if PID file is empty then it should be safe to remove it
	read pid < "$pid_file"
	if [ -z "$pid" ]; then
		rm $pid_file
		ocf_log debug "PID File \"$pid_file\" Was Removed - Zero length";
		return 0;
	fi

	if [ ! -d /proc/`cat "$pid_file"` ]; then	
		rm "$pid_file"
		ocf_log debug "PID File \"$pid_file\" Was Removed - PID Does Not Exist";
		return 0;
	fi

	return 1;
}
