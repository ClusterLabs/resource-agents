#!/bin/bash

# 
# Sybase Availability Agent for Red Hat Cluster v15.0.2 
# Copyright (C) - 2007
# Sybase, Inc. All rights reserved.
#
# Sybase Availability Agent for Red Hat Cluster v15.0.2 is licensed
# under the GNU General Public License Version 2.
#
# Author(s):
#    Jian-ping Hui <jphui@sybase.com>
#
# Description: Service script for starting/stopping/monitoring \
#              Sybase Adaptive Server on: \
#                            Red Hat Enterprise Linux 5 ES \
#                            Red Hat Enterprise Linux 5 AS
#
# NOTES:
#
# (1) Before running this script, we assume that user has installed
#     Sybase ASE 15.0.2 or higher version on the machine. Please
#     customize your configuration in /etc/cluster/cluster.conf according
#     to your actual environment. We assume the following files exist before
#     you start the service:
#         /$sybase_home/SYBASE.sh
#         /$sybase_home/$sybase_ase/install/RUN_$server_name
#
# (2) You can customize the interval value in the meta-data section if needed:
#                <action name="start" timeout="300" />
#                <action name="stop" timeout="300" />
#                
#                <!-- Checks to see if it''s mounted in the right place -->
#                <action name="status"  interval="30" timeout="100" />
#                <action name="monitor" interval="30" timeout="100" />
#                
#                <!--Checks to see if we can read from the mountpoint -->
#                <action name="status" depth="10" timeout="100" interval="120" />
#                <action name="monitor" depth="10" timeout="100" interval="120" />
#                
#                <action name="meta-data" timeout="5" />
#                <action name="validate-all" timeout="5" />
#     The timeout value is not supported by Redhat in RHCS5.0. 
# 
# (3) This script should be put under /usr/share/cluster. Its owner should be "root" with 
#     execution permission.
#

. /etc/init.d/functions
. $(dirname $0)/ocf-shellfuncs

PROG=${0}

export LD_POINTER_GUARD=0

#######################################################################################
# Declare some variables we will use in the script. Please don't change their values. #
#######################################################################################
declare login_string=""
declare RUNSERVER_SCRIPT=$OCF_RESKEY_sybase_home/$OCF_RESKEY_sybase_ase/install/RUN_$OCF_RESKEY_server_name
declare CONSOLE_LOG=$OCF_RESKEY_sybase_home/$OCF_RESKEY_sybase_ase/install/$OCF_RESKEY_server_name.log

##################################################################################################
# This function will be called by rgmanager to get the meta data of resource agent "ASEHAagent". #
# NEVER CHANGE ANYTHING IN THIS FUNCTION.
##################################################################################################
meta_data()
{
	cat <<EOT
<?xml version="1.0" ?>
<resource-agent name="ASEHAagent" version="rgmanager 2.0">
	<version>1.0</version>

	<longdesc lang="en">
		Sybase ASE Failover Instance
	</longdesc>
	<shortdesc lang="en">
		Sybase ASE Failover Instance
	</shortdesc>

	<parameters>
		<parameter name="name" unique="1" primary="1">
			<longdesc lang="en">
				Instance name of resource agent "ASEHAagent"
			</longdesc>
			<shortdesc lang="en">
				name
			</shortdesc>
			<content type="string" />
		</parameter>

		<parameter name="sybase_home" required="1">
			<longdesc lang="en">
				The home directory of sybase products
			</longdesc>
			<shortdesc lang="en">
				SYBASE home directory
			</shortdesc>
			<content type="string" />
		</parameter>

		<parameter name="sybase_ase" required="1">
			<longdesc lang="en">
				The directory name under sybase_home where ASE products are installed
			</longdesc>
			<shortdesc lang="en">
				SYBASE_ASE directory name
			</shortdesc>
			<content type="string" default="ASE-15_0" />
		</parameter>

		<parameter name="sybase_ocs" required="1">
			<longdesc lang="en">
				The directory name under sybase_home where OCS products are installed, i.e. ASE-15_0
			</longdesc>
			<shortdesc lang="en">
				SYBASE_OCS directory name
			</shortdesc>
			<content type="string" default="OCS-15_0" />
		</parameter>

		<parameter name="server_name" required="1">
			<longdesc lang="en">
				The ASE server name which is configured for the HA service
			</longdesc>
			<shortdesc lang="en">
				ASE server name
			</shortdesc>
			<content type="string" />
		</parameter>

		<parameter name="login_file" required="1">
			<longdesc lang="en">
				The full path of login file which contains the login/password pair
			</longdesc>
			<shortdesc lang="en">
				Login file
			</shortdesc>
			<content type="string" />
		</parameter>

		<parameter name="interfaces_file" required="1">
			<longdesc lang="en">
				The full path of interfaces file which is used to start/access the ASE server
			</longdesc>
			<shortdesc lang="en">
				Interfaces file
			</shortdesc>
			<content type="string" />
		</parameter>

		<parameter name="sybase_user" required="1">
			<longdesc lang="en">
				The user who can run ASE server
			</longdesc>
			<shortdesc lang="en">
				Sybase user
			</shortdesc>
			<content type="string" default="sybase" />
		</parameter>

		<parameter name="shutdown_timeout" required="1">
			<longdesc lang="en">
				The maximum seconds to wait for the ASE server to shutdown before killing the process directly
			</longdesc>
			<shortdesc>
				Shutdown timeout value
			</shortdesc>
			<content type="integer" default="0" />
		</parameter>

		<parameter name="start_timeout" required="1">
			<longdesc lang="en">
				The maximum seconds to wait for an ASE server to complete before determining that the server had failed to start
			</longdesc>
			<shortdesc lang="en">
				Start timeout value
			</shortdesc>
			<content type="integer" default="0" />
		</parameter>

		<parameter name="deep_probe_timeout" required="1">
			<longdesc lang="en">
				The maximum seconds to wait for the response of ASE server before determining that the server had no response while running deep probe
			</longdesc>
			<shortdesc lang="en">
				Deep probe timeout value
			</shortdesc>
			<content type="integer" default="0" />
		</parameter>
	</parameters>
	<actions>
		<action name="start" timeout="300" />
		<action name="stop" timeout="300" />
		
		<!-- Checks to see if it''s mounted in the right place -->
		<action name="status"  interval="30" timeout="100" />
		<action name="monitor" interval="30" timeout="100" />
		
		<!--Checks to see if we can read from the mountpoint -->
		<action name="status" depth="10" timeout="100" interval="120" />
		<action name="monitor" depth="10" timeout="100" interval="120" />
		
		<action name="meta-data" timeout="5" />
		<action name="validate-all" timeout="5" />
	</actions>

	<special tag="rgmanager">
	</special>
</resource-agent>
EOT
}

##################################################################################################
# Function Name: validate_all                                                                    #
# Parameter: None                                                                                #
# Return value:                                                                                  #
#             0               SUCCESS                                                            #
#             OCF_ERR_ARGS    Parameters are invalid                                             #
# Description: Do some validation on the user-configurable stuff at the beginning of the script. #
##################################################################################################
verify_all() 
{
	ocf_log debug "ASEHAagent: Start 'verify_all'"

	# Check if the parameter 'sybase_home' is set.	
	if [[ -z "$OCF_RESKEY_sybase_home" ]]
	then
		ocf_log err "ASEHAagent: The parameter 'sybase_home' is not set."
		return $OCF_ERR_ARGS
	fi

	# Check if the parameter 'sybase_home' is a valid path.
	if [[ ! -d $OCF_RESKEY_sybase_home ]]
	then
		ocf_log err "ASEHAagent: The sybase_home '$OCF_RESKEY_sybase_home' doesn't exist."
		return $OCF_ERR_ARGS
	fi

	# Check if the script file SYBASE.sh exists
	if [[ ! -f $OCF_RESKEY_sybase_home/SYBASE.sh ]]
	then
		ocf_log err "ASEHAagent: The file $OCF_RESKEY_sybase_home/SYBASE.sh is required to run this script. Failed to run the script."
		return $OCF_ERR_ARGS
	fi

	# Check if the parameter 'sybase_ase' is set.
	if [[ -z "$OCF_RESKEY_sybase_ase" ]] 
	then
		ocf_log err "ASEHAagent: The parameter 'sybase_ase' is not set."
		return $OCF_ERR_ARGS
	fi

	# Check if the directory /$OCF_RESKEY_sybase_home/$OCF_RESKEY_sybase_ase exists.
	if [[ ! -d $OCF_RESKEY_sybase_home/$OCF_RESKEY_sybase_ase ]]
	then
		ocf_log err "ASEHAagent: The directory '$OCF_RESKEY_sybase_home/$OCF_RESKEY_sybase_ase' doesn't exist."
		return $OCF_ERR_ARGS
	fi

	# Check if the parameter 'sybase_ocs' is set.
	if [[ -z "$OCF_RESKEY_sybase_ocs" ]] 
	then
		ocf_log err "ASEHAagent: The parameter 'sybase_ocs' is not set."
		return $OCF_ERR_ARGS
	fi

	# Check if the directory /$OCF_RESKEY_sybase_home/$OCF_RESKEY_sybase_ocs exists.
	if [[ ! -d $OCF_RESKEY_sybase_home/$OCF_RESKEY_sybase_ocs ]]
	then
		ocf_log err "ASEHAagent: The directory '$OCF_RESKEY_sybase_home/$OCF_RESKEY_sybase_ocs' doesn't exist."
		return $OCF_ERR_ARGS
	fi

	# Check if the parameter 'server_name' is set.	
	if [[ -z "$OCF_RESKEY_server_name" ]]
	then
		ocf_log err "ASEHAagent: The parameter 'server_name' is not set."
		return $OCF_ERR_ARGS
	fi

	# Check if the Run_server file exists.
	if [[ ! -f $RUNSERVER_SCRIPT ]]
	then
		ocf_log err "ASEHAagent: There file $RUNSERVER_SCRIPT doesn't exist. The sybase directory may be incorrect."
		return $OCF_ERR_ARGS
	fi

	# Check if the parameter 'login_file' is set.	
	if [[ -z "$OCF_RESKEY_login_file" ]]
	then
		ocf_log err "ASEHAagent: The parameter 'login_file' is not set."
		return $OCF_ERR_ARGS
	fi

	# Check if the login file exist.
	if [[ ! -f $OCF_RESKEY_login_file ]]
	then
		ocf_log err "ASEHAagent: The login file '$OCF_RESKEY_login_file' doesn't exist."
		return $OCF_ERR_ARGS
	fi

	# Check if the parameter 'sybase_user' is set
	if [[ -z "$OCF_RESKEY_sybase_user" ]]
	then
		ocf_log err "ASEHAagent: The parameter 'sybase_user' is not set."
		return $OCF_ERR_ARGS
	fi

	# Check if the user 'sybase_user' exist
	id -u $OCF_RESKEY_sybase_user
	if [[ $? != 0 ]]
	then
		ocf_log err "ASEHAagent: The user '$OCF_RESKEY_sybase_user' doesn't exist in the system."
		return $OCF_ERR_ARGS
	fi

	# Check if the parameter 'interfaces_file' is set
	if [[ -z "$OCF_RESKEY_interfaces_file" ]]
	then
		ocf_log err "ASEHAagent: The parameter 'interfaces_file' is not set."
		return $OCF_ERR_ARGS
	fi

	# Check if the file 'interfaces_file' exists
	if [[ ! -f $OCF_RESKEY_interfaces_file ]]
	then
		ocf_log err "ASEHAagent: The interfaces file '$OCF_RESKEY_interfaces_file' doesn't exist."
		return $OCF_ERR_ARGS
	fi

	# Check if the parameter 'shutdown_timeout' is a valid value
	if [[ $OCF_RESKEY_shutdown_timeout -eq 0 ]]
	then
		ocf_log err "ASEHAagent: The parameter 'shutdown_timeout' is not set. Its value cannot be zero."
		return $OCF_ERR_ARGS
	fi

	# Check if the parameter 'start_timeout' is a valid value
	if [[ $OCF_RESKEY_start_timeout -eq 0 ]]
	then
		ocf_log err "ASEHAagent: The parameter 'start_timeout' is not set. Its value cannot be zero."
		return $OCF_ERR_ARGS
	fi

	# Check if the parameter 'deep_probe_timeout' is a valid value
	if [[ $OCF_RESKEY_deep_probe_timeout -eq 0 ]]
	then
		ocf_log err "ASEHAagent: The parameter 'deep_probe_timeout' is not set. Its value cannot be zero."
		return $OCF_ERR_ARGS
	fi

	ocf_log debug "ASEHAagent: End 'verify_all' successfully."

	return 0
}

################################################################################################################
# Function name: get_login_string                                                                              #
# Parameter: None                                                                                              #
# Return value:                                                                                                #
#              0    SUCCESS                                                                                    #
#              1    FAIL                                                                                       #
# Description: Analyze the login_file to format the login string. This function will set the global variable   #
#              "login_string". If the login/password is clear text, the "login_string" will become to "-Ulogin #
#              -Ppassword" if there is no error. If there are any errors in this function, the string          #
#              "login_string" will be still empty. In current stage, the encrypted string is not supported     #
#              because "haisql" is not available on this platform.                                             #
################################################################################################################
get_login_string()
{
	tmpstring=""
	login_sting=""

	# Read the first column. The valid value will be "normal" or "encrypted". Any other values are invalid.
	login_type=`head -1 $OCF_RESKEY_login_file | awk '{print $1}'`
	if [[ $login_type = "normal" ]]
	then
		# The login/password pair is saved in clear text.
		# Abstract the login/password from the line. 
		tmpstring=`head -1 $OCF_RESKEY_login_file | awk '{print $2}'`

		# Abstract "user" from the string.
		user=`echo $tmpstring | awk -F'/' '{print $1}'`
		# Check if the "user" string is NULL. If it is NULL, it means this is not a valid user.
		if  [[ -z $user ]]
		then
			ocf_log err "ASEHAagent: Login username is not specified in the file '$OCF_RESKEY_login_file'"
			return 1
		fi

		# Abstract "password" from the string.
		passwd=`echo $tmpstring | awk -F'/' '{print $2}'`

		# Format the "login_string".
		login_string="-U$user -P$passwd"
	else
		# The login_type is invalid value.
		ocf_log err "ASEHAagent: Login type specified in the file $OCF_RESKEY_login_file is not 'normal' or 'encrypted' which are only supported values."
		return 1
	fi

	# The "login_file" has been analyzed successfully. Now, the value of "login_string" contains the login/password information.
	return 0
}

##############################################################################################
# Function name: ase_start                                                                   #
# Parameter: None                                                                            #
# Return value:                                                                              #
#             0  SUCCESS                                                                     #
#             1  FAIL                                                                        #
# Description: This function is used to start the ASE server in primary or secondary server. #
##############################################################################################
ase_start()
{
	ocf_log debug "ASEHAagent: Start 'ase_start'"

	# Check if the server is running. If yes, return SUCCESS directly. Otherwise, continue the start work.
	ase_is_running
	if [[ $? = 0 ]]
	then
		# The server is running. 
		ocf_log info "ASEHAagent: Server is running. Start is success."
		return 0
	fi

	# The server is not running. We need to start it.
	# If the log file existed, delete it.
	if [[ -f $CONSOLE_LOG ]]
	then
		rm -f $CONSOLE_LOG
	fi
		
	ocf_log debug "ASEHAagent: Starting '$OCF_RESKEY_server_name'..."

	# Run runserver script to start the server. Since this script will be run by root and ASE server
	# needs to be run by another user, we need to change the user to sybase_user first. Then, run
	# the script to start the server.
	su $OCF_RESKEY_sybase_user -c ksh << EOF
		# set required SYBASE environment by running SYBASE.sh.
		. $OCF_RESKEY_sybase_home/SYBASE.sh
		# Run the RUNSERVER_SCRIPT to start the server.
                . $RUNSERVER_SCRIPT > $CONSOLE_LOG 2>&1 &
EOF

	# Monitor every 1 seconds if the server has
	# recovered, until RECOVERY_TIMEOUT.
	t=0
	while [[ $t -le $OCF_RESKEY_start_timeout ]]
	do
		grep -s "Recovery complete." $CONSOLE_LOG > /dev/null 2>&1
		if [[ $? != 0 ]]
		then
			# The server has not completed the recovery. We need to continue to monitor the recovery
			# process.
			t=`expr $t + 1`
		else
			# The server has completed the recovery.
			ocf_log info "ASEHAagent: ASE server '$OCF_RESKEY_server_name' started successfully."
			break
		fi
		sleep 1
	done

	# If $t is larger than start_timeout, it means the ASE server cannot start in given time. Otherwise, it 
	# means the ASE server has started successfully.
	if [[ $t -gt $OCF_RESKEY_start_timeout ]]
	then
		# The server cannot start in specified time. We think the start is failed.
		ocf_log err "ASEHAagent: Failed to start ASE server '$OCF_RESKEY_server_name'. Please check the server error log $CONSOLE_LOG for possible problems."
		return 1
	fi

	ocf_log debug "ASEHAagent: End 'ase_start' successfully."

	return 0
}

#############################################################################################
# Function name: ase_stop                                                                   #
# Parameter: None                                                                           #
# Return value:                                                                             #
#             0  SUCCESS                                                                    #
#             1  FAIL                                                                       #
# Description: This function is used to stop the ASE server in primary or secondary server. #
#############################################################################################
ase_stop()
{
	ocf_log debug "ASEHAagent: Start 'ase_stop'"

	# Check if the ASE server is still running.
	ase_is_running
	if [[ $? != 0 ]]
	then
		# The ASE server is not running. We need not to shutdown it.
		ocf_log info "ASEHAagent: The dataserver $OCF_RESKEY_server_name is not running."
		return 0
	fi

	# Call get_login_string() to parse the login/password string
	get_login_string
	if [[ $? = 1 ]]
	then
		# The login account cannot be used. So we will kill the process directly.
		ocf_log info "ASEHAagent: Cannot parse the login file $OCF_RESKEY_login_file. Kill the processes of ASE directly."
		# Kill the OS processes immediately.
		kill_ase 0
		return $?
	fi

	# Just in case things are hung, start a process that will wait for the
	# timeout period, then kill any remaining porcesses.  We'll need to
	# monitor this process (set -m), so we can terminate it later if it is
	# not needed.
	set -m
	$PROG kill &
	KILL_PID=$!     # If successful, we will also terminate watchdog process

	# Run "shutdown with nowait" from isql command line to shutdown the server
	su $OCF_RESKEY_sybase_user -c ksh << EOF
		# set required SYBASE environment by running SYBASE.sh.
		. $OCF_RESKEY_sybase_home/SYBASE.sh
		# Run "shutdown with nowait" to shutdown the server immediately.
		(echo "use master" ; echo go ; echo "shutdown with nowait"; echo go) | \
		\$SYBASE/\$SYBASE_OCS/bin/isql $login_string -S$OCF_RESKEY_server_name -I$OCF_RESKEY_interfaces_file  &
EOF

	sleep 5

	# Check if the server has been shutted down successfully
	t=0
	while [[ $t -lt $OCF_RESKEY_shutdown_timeout ]]
	do
		# Search "usshutdown: exiting" in the server log. If found, it means the server has been shutted down. 
		# Otherwise, we need to wait.
		tail $CONSOLE_LOG | grep "ueshutdown: exiting" > /dev/null 2>&1
		if [[ $? != 0 ]]
		then
			# The shutdown is still in processing. Wait...
			sleep 2
			t=`expr $t+2`
		else
			# The shutdown is success.
			ocf_log info "ASEHAagent: ASE server '$OCF_RESKEY_server_name' shutdown with isql successfully."
			break
		fi
	done

	# If $t is larger than shutdown_timeout, it means the ASE server cannot be shutted down in given time. We need
	# to wait for the background kill process to kill the OS processes directly.
	if  [[ $t -ge $OCF_RESKEY_shutdown_timeout ]]
	then
		ocf_log err "ASEHAagent: Shutdown of '$OCF_RESKEY_server_name' from isql failed.  Server is either down or unreachable."
	fi

	# Here, the ASE server has been shutted down by isql command or killed by background process. We need to do
	# further check to make sure all processes have gone away before saying shutdown is complete. This stops the
	# other node from starting up the package before it has been stopped and the file system has been unmounted.
	
	# Get all processes ids from log file
	declare -a ENGINE_ALL=(`sed -n -e '/engine /s/^.*os pid \([0-9]*\).*online$/\1/p' $CONSOLE_LOG`)
	typeset -i num_procs=${#ENGINE_ALL[@]}

	# We cannot find any process id from log file. It may be because the log file is corrupted or be deleted.
	# In this case, we determine the shutdown is failed.
	if [[ "${ENGINE_ALL[@]}" = "" ]]
	then
		ocf_log err "ASEHAagent: Unable to find the process id from $CONSOLE_LOG."
		ocf_log err "ASEHAagent: Stop ASE server failed."
		return 1
	fi

	# Monitor the system processes to make sure all ASE related processes have gone away.
	while true
	do
		# To every engine process, search it in system processes list. If it is not in the
		# list, it means this process has gone away. Otherwise, we need to wait for it is
		# killed by background process.
		for i in ${ENGINE_ALL[@]}
		do
			ps -fu $OCF_RESKEY_sybase_user | awk '{print $2}' | grep $i | grep -v grep
			if [[ $? != 0 ]]
			then
				ocf_log debug "ASEHAagent: $i process has stopped."
				c=0
				while (( c < $num_procs ))
				do
					if [[ ${ENGINE_ALL[$c]} = $i ]]
					then
						unset ENGINE_ALL[$c]
						c=$num_procs
					fi
					(( c = c + 1 ))	
				done
			fi
		done
		
		# To here, all processes should have gone away. 
		if [[ ${ENGINE_ALL[@]} = "" ]]
		then
			#
			# Looks like shutdown was successful, so kill the
			# script to kill any hung processes, which we started earlier.
			# Check to see if the script is still running.  If jobs
			# returns that the script is done, then we don't need to kill
			# it.
			#
			job=$(jobs | grep -v Done)
			if [[ ${job} != "" ]]
			then
				ocf_log debug "ASEHAagent: Killing the kill_ase script."

				kill -15 $KILL_PID > /dev/null 2>&1
			fi
			break
	        fi
		sleep 5
	done

	ocf_log debug "ASEHAagent: End 'ase_stop'."

	return 0
}

####################################################################################
# Function name: ase_is_running                                                    #
# Parameter: None                                                                  #
# Return value:                                                                    #
#             0   ASE server is running                                            #
#             1   ASE server is not running or there are errors                    #
# Description: This function is used to check if the ASE server is still running . #
####################################################################################
ase_is_running()
{
	# If the error log doesn't exist, we can say there is no ASE is running.
	if [[ ! -f $CONSOLE_LOG ]]
	then
		return 1
	fi

	# The error log file exists. Check if the engine 0 is alive.
	ENGINE_0=(`sed -n -e '/engine 0/s/^.*os pid \([0-9]*\).*online$/\1/p' $CONSOLE_LOG`)
	if [[ "$ENGINE_0" = "" ]]
	then
		# The engine 0 is down.
		return 1 
	else
		kill -s 0 $ENGINE_0 > /dev/null 2>&1
		if [[ $? != 0 ]]
		then
			# The engine 0 is not running.
			return 1
		else
			# The engine 0 is running.
			return 0
		fi
        fi

	return 1
}

####################################################################################
# Function name: ase_is_running                                                    #
# Parameter:                                                                       #
#             DELAY  The seconds to wait before killing the ASE processes. 0 means #
#                    kill the ASE processes immediately.                           #
# Return value: None                                                               #
#             1   ASE server is not running or there are errors                    #
# Description: This function is used to check if the ASE server is still running . #
####################################################################################
kill_ase()
{
	ocf_log debug "ASEHAagent: Start 'kill_ase'."

	DELAY=$1

	# Wait for sometime before sending a kill signal.  
	t=0
        while [[ $t -lt $DELAY ]]
        do
     		sleep 1
		t=`expr $t+1`
        done

	# Get the process ids from log file
	declare -a ENGINE_ALL=`sed -n -e '/engine /s/^.*os pid \([0-9]*\).*online$/\1/p' $CONSOLE_LOG`

	# If there is no process id found in the log file, we need not to continue.
	if [[ "${ENGINE_ALL[@]}" = "" ]]
	then
		ocf_log err "ASEHAagent: Unable to find the process id from $CONSOLE_LOG."
		return
	fi

	# Kill the datasever process(es)
	for pid in ${ENGINE_ALL[@]}
	do
		kill -9 $pid > /dev/null 2>&1
		if [[ $? != 0 ]]
		then
			ocf_log info "ASEHAagent: kill_ase function did NOT find process $pid running."
		else
			ocf_log info "ASEHAagent: kill_ase function did find process $pid running.  Sent SIGTERM."
		fi
	done

	ocf_log debug "ASEHAagent: End 'kill_ase'."
}


#######################################################################################
# Function name: terminate                                                            #
# Parameter: None                                                                     #
# Return value: Always be 1                                                           #
# Description: This function is called automatically after this script is terminated. #
#######################################################################################
terminate()
{
	ocf_log debug "ASEHAagent: This monitor script has been signaled to terminate."
	exit 1
}

#####################################################################################
# Function name: ase_status                                                         #
# Parameter:                                                                        #
#             0   Level 0 probe. In this level, we just check if engine 0 is alive  #
#             10  Level 10 probe. In this level, we need to probe if the ASE server #
#                 still has response.                                               #              
# Return value:                                                                     #
#             0   The server is still alive                                         #
#             1   The server is down                                                #
# Description: This function is used to check if the ASE server is still running.   #
#####################################################################################
ase_status()
{
	ocf_log debug "ASEHAagent: Start 'ase_status'."

	# Step 1: Check if the engine 0 is alive
	ase_is_running
	if [[ $? = 1 ]]
	then
		# ASE is down. Return fail to rgmanager to trigger the failover process.
		ocf_log err "ASEHAagent: ASE server is down."
		return 1
	fi

	# ASE process is still alive. 
	# Step2: If this is level 10 probe, We need to check if the ASE server still has response.
	if [[ $1 -gt 0 ]]
	then
		ocf_log debug "ASEHAagent: Need to run deep probe."
		# Run deep probe
		deep_probe
		if [[ $? = 1 ]]
		then
			# Deep probe failed. This means the server has been down.
			ocf_log err "ASEHAagent: Deep probe found the ASE server is down."
			return 1
		fi
	fi

	ocf_log debug "ASEHAagent: End 'ase_status'."

	return 0
}

####################################################################################
# Function name: deep_probe                                                        #
# Parameter: None                                                                  #
# Return value:                                                                    #
#             0   ASE server is alive                                              #
#             1   ASE server is down                                               #
# Description: This function is used to run deep probe to make sure the ASE server #
#              still has response.                                                 #
####################################################################################
deep_probe()
{
	declare -i rv
	
	ocf_log debug "ASEHAagent: Start 'deep_probe'."	

	# Declare two temporary files which will be used in this probe.
	tmpfile1="$(mktemp /tmp/ASEHAagent.1.XXXXXX)"
	tmpfile2="$(mktemp /tmp/ASEHAagent.2.XXXXXX)"
	
	# Get the login_string by analyzing the login_file.
	get_login_string
	if [[ $? = 1 ]]
	then
		# Login string cannot be fetched. Cannot continue the deep probe.
		ocf_log err "ASEHAagent: Cannot run the deep probe because of incorrect login file $OCF_RESKEY_login_file. Deep probe failed."
		return 1
	fi

	rm -f $tmpfile1
	rm -f $tmpfile2

	# The login file is correct. We have gotten the login account and password from it.
	# Run isql command in background.
	su $OCF_RESKEY_sybase_user -c ksh << EOF
		# set required SYBASE environment by running SYBASE.sh.
		. $OCF_RESKEY_sybase_home/SYBASE.sh
		# Run a very simple SQL statement to make sure the server is still ok. The output will be put to
		# tmpfile1.
		(echo "select 1"; echo "go") |
		\$SYBASE/\$SYBASE_OCS/bin/isql $login_string -S$OCF_RESKEY_server_name -I$OCF_RESKEY_interfaces_file -t $OCF_RESKEY_deep_probe_timeout -e -o$tmpfile1 &
		# Record the isql command process id to temporary file. If the isql is hung, we need this process id
                # to kill the hung process.
		echo \$! > $tmpfile2	
EOF
	
	declare -i t=0
		
	# Monitor the output file tmpfile1.
	while [[ $t -lt $OCF_RESKEY_deep_probe_timeout ]]
	do
		# If the SQL statement is executed successfully, we will get the following output:
		# 1> select 1
		# 
		# -----------
		#           1
		# 
		# (1 row affected)
		# So, we determine if the execution is success by searching the keyword "(1 row affected)".
		grep "(1 row affected)" $tmpfile1
		if [[ $? = 0 ]]
		then
			ocf_log debug "ASEHAagent: Deep probe sucess."
			break
		else
			sleep 1
			t=`expr $t+1`
		fi
	done	

	# If $t is larger than deep_probe_timeout, it means the isql command line cannot finish in given time.
	# This means the deep probe failed. We need to kill the isql process manually.
	if [[ $t -ge $OCF_RESKEY_deep_probe_timeout ]]
	then
		ocf_log err "ASEHAagent: Deep probe fail. The dataserver has no response."		

		# Read the process id of isql process from tmpfile2
		pid=`cat $tmpfile2 | awk '{print $1}'`

		rm -f $tmpfile1
		rm -f $tmpfile2

		# Kill the isql process directly.
		kill -9 $pid
		return 1
	fi

	rm -f $tmpfile1
	rm -f $tmpfile2

	ocf_log debug "ASEHAagent: End 'deep_probe'."

	return 0
}

trap terminate SIGTERM

#############################
# Do some real work here... #
#############################
case $1 in
	start)
		verify_all || exit 1
		ase_start
		exit $?
		;;
	stop)
		verify_all || exit 1
		ase_stop
		exit $?
		;;
	status | monitor)
		verify_all || exit 1
		ase_status $OCF_CHECK_LEVEL
		exit $?
		;;
	kill)
		kill_ase $OCF_RESKEY_shutdown_timeout
		;;
	meta-data)
		meta_data
		exit $?
		;;
	validate-all)
		verify_all
		exit $?
		;;
	*)
		echo "Usage: $SCRIPT {start|stop|monitor|status|validate-all|meta-data}"
		exit $OCF_ERR_UNIMPLEMENTED
		;;
esac
exit 0

