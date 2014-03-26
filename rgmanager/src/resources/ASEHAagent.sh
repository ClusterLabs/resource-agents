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

ase_heartbeat_wrapper()
{
	# default heartbeat agent ocf root.
	export OCF_ROOT=/usr/lib/ocf
	heartbeat_ase="${OCF_ROOT}/resource.d/heartbeat/sybaseASE"

	if ! [ -a $heartbeat_ase ]; then
		echo "heartbeat sybaseASE agent not found at '${heartbeat_ase}'"
		exit $OCF_ERR_INSTALLED 
	fi
	$heartbeat_ase $1
}


#############################
# Do some real work here... #
#############################
case $1 in
	start | stop | status | monitor | kill | validate-all)
		ase_heartbeat_wrapper "$1"
		exit $?
		;;
	meta-data)
		meta_data
		exit $?
		;;
	*)
		echo "Usage: $SCRIPT {start|stop|monitor|status|validate-all|meta-data}"
		exit $OCF_ERR_UNIMPLEMENTED
		;;
esac
exit 0

