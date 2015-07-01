#!/bin/sh
#  License:      GNU General Public License (GPL)
#   (c) 2015 T.J. Yang  and Linux-HA contributors
# -----------------------------------------------------------------------------
#      O C F    R E S O U R C E    S C R I P T   S P E C I F I C A T I O N
# -----------------------------------------------------------------------------
#
#
# NAME
#       nagios_ocf : Nagios OCF resource agent script.
#
# REVISION HISTORY
#       04/15/2015    T.J. Yang   Adding     moto bash script specification
#
# USAGE
#   1. put this script in OCF_
# DEBUG    
# OCF_ROOT=/usr/lib/ocf /usr/lib/ocf/resource.d/heartbeat/nagios
# DESCRIPTION
#       
#
# OPTIONS
#
#
# RETURN CODE
#     OCF_SUCCESS (0)
#     OCF_ERR_GENERIC (1)
#     OCF_ERR_ARGS (2)
#     OCF_ERR_UNIMPLEMENTED (3)
#     OCF_ERR_PERM (4)
#     OCF_ERR_INSTALLED (5)
#     OCF_ERR_CONFIGURED (6)
#     OCF_NOT_RUNNING (7)
#     OCF_RUNNING_MASTER (8)
#     OCF_FAILED_MASTER (9)
#
#  REFERENCE:
#   1. http://www.linux-ha.org/doc/dev-guides/ra-dev-guide.html
#   2. link to old nagios script
#
# ---------------------------- CONSTANT DECLARATION ---------------------------
# WHAT: Define PATH.
# WHY:  Mainly for security reasons.
# NOTE: This should only be changed if you know what you're doing.
#

# WHAT: Define exit status flags.
# WHY:  Easier to program with a mnemonic than with the numbers.
# NOTE: THESE DO NOT CHANGE UNLESS NOTIFIED BY HP!  Startup depends on these
#       exit statuses being defined this way.

: ${OCF_FUNCTIONS_DIR=${OCF_ROOT}/lib/heartbeat}
#[root@nagios1 heartbeat]# locate  .ocf-shellfuncs
#/usr/lib/ocf/resource.d/heartbeat/.ocf-shellfuncs
#[root@nagios1 heartbeat]#
#
. ${OCF_FUNCTIONS_DIR}/ocf-shellfuncs
# ocf_is_root() {
# ocf_maybe_random() {
# ocf_is_decimal() {
# ocf_is_true() {
# ocf_is_hex() {
# ocf_is_octal() {
# ocf_log() {
# ocf_exit_reason()
# ocf_deprecated() {
# ocf_run() {
# ocf_pidfile_status() {
# ocf_take_lock() {
# ocf_release_lock_on_exit() {
# ocf_is_probe() {
# ocf_is_clone() {
# ocf_is_ms() {
# ocf_is_ver() {
# ocf_ver2num() {
# ocf_ver_level(){
# ocf_ver_complete_level(){
# ocf_version_cmp() {
# ocf_local_nodename() {
# ocf_check_level()
# ocf_stop_processes() {
# ocf_is_bash4() {
# ocf_trace_redirect_to_file() {
# ocf_trace_redirect_to_fd() {
# ocf_default_trace_dest() {
# ocf_start_trace() {
# ocf_stop_trace() {
# ocf_is_true "$OCF_TRACE_RA" && ocf_start_trace

# Fill in some defaults if no values are specified
# cutomized for CentOS 7.1, Nagios 3.5.1
: ${OCF_RESKEY_installdir="/usr/bin"}
: ${OCF_RESKEY_vardir="/var/nagios"}
: ${OCF_RESKEY_binary="/usr/bin/nagios"}
: ${OCF_RESKEY_cgidir="/usr/lib64/nagios/cgi-bin"}
NAGIOS_BINDIR=`dirname ${OCF_RESKEY_binary}`
: ${OCF_RESKEY_config="/etc/nagios/nagios.cfg"}
: ${OCF_RESKEY_statusfile="/var/nagios/status.dat"}
: ${OCF_RESKEY_statuslog="/var/nagios/livestatus.log"}
: ${OCF_RESKEY_tempfile="/var/nagios/nagios.tmp*"}
: ${OCF_RESKEY_retentionfile="/var/nagios/retention.dat"}
: ${OCF_RESKEY_commandfile="/var/nagios/cmd/nagios.cmd"}
: ${OCF_RESKEY_tempfile="/var/nagios/nagios.tmp"}
: ${OCF_RESKEY_plugindir="/usr/lib64/nagios/plugins"}
: ${OCF_RESKEY_user="nagios"}
: ${OCF_RESKEY_group="nagios"}
: ${OCF_RESKEY_PidFile="/var/nagios/nagios.pid"}

OCF_RESKEY_PidFile="/var/nagios/nagios.pid"

#
# ---------------------------- VARIABLE DECLARATION ---------------------------
#
# WHAT: enable oracle tracing.
#export ORACLE_TRACE='T'
#
SH=/bin/bash
# ---------------------------- FUNCTION DECLARATION ---------------------------
# WHAT: define usage function.
# WHY:  to advise the usage of this script.
# NOTE: 
nagios_usage() {
  cat <<USAGEND
    usage: $0 (start|stop|validate-all|meta-data|help|usage|monitor)
    $0 manages a nagios instance as an OCF HA resource.
    The 'start' operation starts the instance.
    The 'stop' operation stops the instance.
    The 'status' operation reports whether the instance is running
    The 'monitor' operation reports whether the instance seems to be working
    The 'validate-all' operation reports whether the parameters are valid
USAGEND
}

# WHAT: Define meta data to describe  Nagios service for pacemaker.
# WHY:  
nagios_meta_data() {
        cat <<END
<?xml version="1.0"?>
<!DOCTYPE resource-agent SYSTEM "ra-api-1.dtd">
<resource-agent name="nagios">
<version>0.75</version>

<longdesc lang="en">OCF Resource script for nagios 3.x or 4.x. It manages a nagios instance as an HA resource.</longdesc>
<shortdesc lang="en">nagios resource agent</shortdesc>

<parameters>

<parameter name="installdir" unique="1" required="1">
	<longdesc lang="en">Root directory of the nagios installation</longdesc>
	<shortdesc lang="en">nagios install directory</shortdesc>
	<content type="string" default="/usr/local/nagios" />
</parameter>

<parameter name="vardir" unique="1" required="1">
	<longdesc lang="en">var directory of the nagios installation</longdesc>
	<shortdesc lang="en">nagios var directory</shortdesc>
	<content type="string" default="/usr/local/nagios/var" />
</parameter>

<parameter name="cgidir" unique="1" required="1">
	<longdesc lang="en">cgi directory of the nagios installation</longdesc>
	<shortdesc lang="en">nagios cgi directory</shortdesc>
	<content type="string" default="/usr/local/nagios/sbin" />
</parameter>

<parameter name="binary" unique="1" required="1">
	<longdesc lang="en">Location of the nagios binary</longdesc>
	<shortdesc lang="en">nagios binary</shortdesc>
	<content type="string" default="/usr/local/nagios/bin/nagios" />
</parameter>

<parameter name="config" unique="1" required="1">
	<longdesc lang="en">Configuration file</longdesc>
	<shortdesc lang="en">nagios config</shortdesc>
	<content type="string" default="/usr/local/nagios/etc/nagios.cfg" />
</parameter>

<parameter name="user" unique="1" required="1">
	<longdesc lang="en">User running nagios daemon</longdesc>
	<shortdesc lang="en">nagios user</shortdesc>
	<content type="string" default="nagios" />
</parameter>

<parameter name="group" unique="1" required="1">
	<longdesc lang="en">Group running nagios daemon (for logfile and directory permissions)</longdesc>
	<shortdesc lang="en">nagios group</shortdesc>
	<content type="string" default="nagios"/>
</parameter>

<parameter name="statusfile" unique="1" required="0">
	<longdesc lang="en">Location of the nagios status file</longdesc>
	<shortdesc lang="en">nagios statusfile</shortdesc>
	<content type="string" default="/usr/local/nagios/var/status.dat" />
</parameter>

<parameter name="statuslog" unique="1" required="0">
	<longdesc lang="en">Location of the nagios status log</longdesc>
	<shortdesc lang="en">nagios statuslog</shortdesc>
	<content type="string" default="/usr/local/nagios/var/status.log" />
</parameter>

<parameter name="tempfile" unique="1" required="1">
	<longdesc lang="en">Location of the nagios temporary file</longdesc>
	<shortdesc lang="en">nagios tempfile</shortdesc>
	<content type="string" default="/usr/local/nagios/var/nagios.tmp" />
</parameter>

<parameter name="retentionfile" unique="1" required="0">
	<longdesc lang="en">Location of the nagios retention file</longdesc>
	<shortdesc lang="en">nagios retention file</shortdesc>
	<content type="string" default="/usr/local/nagios/var/retention.dat" />
</parameter>

<parameter name="commandfile" unique="1" required="1">
	<longdesc lang="en">Location of the nagios external command file</longdesc>
	<shortdesc lang="en">nagios command file</shortdesc>
	<content type="string" default="/usr/local/nagios/var/rw/nagios.cmd" />
</parameter>

<parameter name="plugindir" unique="1" required="1">
	<longdesc lang="en">Location of the nagios plugins</longdesc>
	<shortdesc lang="en">nagios plugins location</shortdesc>
	<content type="string" default="/usr/lib/nagios/plugins" />
</parameter>

<parameter name="pid" unique="1" required="1">
	<longdesc lang="en">Location of the nagios pid/lock</longdesc>
	<shortdesc lang="en">nagios pid file</shortdesc>
	<content type="string" default="/usr/local/nagios/var/nagios.lock" />
</parameter>

</parameters>

<actions>
<action name="start" timeout="120" />
<action name="stop" timeout="120" />
<action name="status" timeout="60" />
<action name="monitor" depth="0" timeout="30" interval="10" start-delay="10" />
<action name="validate-all" timeout="5" />
<action name="meta-data" timeout="5" />
</actions>
</resource-agent>
END
}


# WHAT: Validate all the config is correct.
# WHY:  
# NOTE: 

#foobar_validate_all() {
#    # Test for configuration errors first
#    if ! ocf_is_decimal $OCF_RESKEY_eggs; then
#       ocf_log err "eggs is not numeric!"
#       exit $OCF_ERR_CONFIGURED
#    fi
#
#    # Test for required binaries
#    check_binary frobnicate
#
#    # Check for data directory (this may be on shared storage, so
#    # disable this test during probes)
#    if ! ocf_is_probe; then
#       if ! [ -d $OCF_RESKEY_datadir ]; then
#          ocf_log err "$OCF_RESKEY_datadir does not exist or is not a directory!"
#          exit $OCF_ERR_INSTALLED
#       fi
#    fi
#
#    return $OCF_SUCCESS
#}

# TODO: above is in better logic
# WHAT: a series of check to ensure current node has all the needed configuration.
nagios_validate_all(){
    ocf_log debug "nagios_validate_all: checking all the config files"
    # Check that nagios binary exists.
    if [ ! -f ${OCF_RESKEY_binary} ]; then
        ocf_log debug "nagios_validate_all: Executable file ${OCF_RESKEY_binary} not found.  Exiting."
        exit ${OCF_ERR_GENERIC}
    fi
    
    # Check that configuration file exists.
    if [ ! -f ${OCF_RESKEY_config} ]; then
         ocf_log debug "nagios_validate_all: Configuration file ${OCF_RESKEY_config} not found.  Exiting."
        exit ${OCF_ERR_GENERIC}
    fi
    
    # check for plugin installation
    if [ ! -d ${OCF_RESKEY_plugindir} ]; then
    	 ocf_log debug "nagios_validate_all: Plugins do not seem to be installed"
    	 ocf_log debug "nagios_validate_all: installation location should be: ${OCF_RESKEY_plugindir}"
    	exit ${OCF_ERR_GENERIC}
    fi
    # run nagios config check
	 ocf_log debug "nagios_validate_all: running ${OCF_RESKEY_binary} -v ${OCF_RESKEY_config}"
    ${OCF_RESKEY_binary} -v ${OCF_RESKEY_config} > /dev/null 2>&1;
	if [ $? != "0" ]; then
   	 ocf_log debug "nagios_validate_all: running ${OCF_RESKEY_binary} -v ${OCF_RESKEY_config}"
    	   return ${OCF_ERR_GENERIC}
	fi
}


nagios_monitor(){
    local rc
    # exit immediately if configuration is not valid
    # nagios_validate_all || exit $?
    # test if nagios is running.
    get_nagios_status
    case "$?" in
        0)
            rc=$OCF_SUCCESS
            ocf_log debug "monitor: Resource is running"
            ;;
        1)
            rc=$OCF_NOT_RUNNING
            ocf_log debug "monitor: Resource is not running"
            ;;
        *)
            ocf_log err "monitor: Resource has failed"
            exit $OCF_ERR_GENERIC
    esac
    return $rc
}




# WHAT: return TRUE if a process with given PID is running
# NOTE: input process id
#       output true or false 
ProcessRunning() {
	ocf_log debug  "start: calling ProcessRunning "
	RunningPID=$1
	# Use /proc if it looks like it's here...
	if [ -d /proc -a -d /proc/1 ]; then
		[ -d /proc/$RunningPID ]
	else
		# This assumes we're running as root...
		kill -s 0 "$RunningPID" >/dev/null 2>&1
	fi
}

silent_status() {
	ocf_log debug  "start: calling silent_status "
	local OCF_RESKEY_PidFile="/var/nagios/nagios.pid"
	if [ -f $OCF_RESKEY_PidFile ]; then
	    local PID=`cat $OCF_RESKEY_PidFile`
	ocf_log debug  "start: calling silent_status in if pid=$PID  "
            ProcessRunning `cat $OCF_RESKEY_PidFile`
	else
	ocf_log debug  "start: calling silent_status in ELSE  pid=$PID  "
		: No pid file
		false
	fi
}


# WHAT: return running PIDs if any or return false
# WHY:  
# NOTE: return 1 or running PIDs numbers
nagios_running_pid(){
        RunningPID=`ps auxww | grep nagios | grep -v grep | grep ${OCF_RESKEY_config} | awk '{print $2}'`
	if [ ${RunningPID}=""  ]; then
	        ocf_log debug "Nagios is not Running."
		return ${OCF_NOT_RUNNING}
	fi
	ocf_log debug "Nagios Running PID=${RunningPID}"
	return ${RunningPID}
}
# WHAT: find nagios running status
# WHY:  
# NOTE: 
nagios_file_pid(){
    local OCF_RESKEY_PidFile=/var/nagios/nagios.pid
	if [ -f ${OCF_RESKEY_PidFile} ]; then
		NagiosPID=`head -n 1 ${OCF_RESKEY_PidFile}`
                ocf_log debug "Nagios PID=${NagiosPID}"
	else
	        ocf_log debug "No Nagios PID lock file found in ${OCF_RESKEY_PidFile}"
		return ${OCF_NOT_RUNNING}
	fi
	return $OCF_SUCCESS
}

get_nagios_pid(){
	 ocf_log debug "get_nagios_pid: OCF_RESKEY_PidFile=${OCF_RESKEY_PidFile}"
	if [ -f ${OCF_RESKEY_PidFile} ]; then
		NagiosPID=`head -n 1 ${OCF_RESKEY_PidFile}`
                ocf_log debug "get_nagios_pid: Nagios PID=${NagiosPID}"
	else
		 ocf_log debug "get_nagios_pid: No Nagios PID lock file found in ${OCF_RESKEY_PidFile}"
		return ${OCF_NOT_RUNNING}
	fi
	return $OCF_SUCCESS
}
get_nagios_status(){
	get_nagios_pid
	# find if PID in ${OCF_RESKEY_PidFile}"
	ps -p ${NagiosPID} > /dev/null 2>&1
	if [ $? = "0" ]; then
		return ${OCF_SUCCESS}
	else
    		return ${OCF_ERR_GENERIC}
	fi
}

kill_nagios_process(){
	if [ nagios_monitor ]; then
		 ocf_log debug "killing Nagios PID: ${NagiosPID}"
		kill ${2} ${NagiosPID}
	else
		 ocf_log debug "Nagios is not running"
		exit 0
	fi
}

#WHAT:
#WHY:
#NOTE: this function need to be able to run sucessfully 
#      on master or slave Nagio server with/without shared drbd storage.
nagios_stop() {
    local rc
    # exit immediately if configuration is not valid
    # nagios_validate_all_master  || exit $?
    nagios_monitor;rc=$?
    case "$rc" in
        "$OCF_SUCCESS")
            # Currently running. Normal, expected behavior.
            ocf_log debug "Resource is currently running"
            ;;
        "$OCF_NOT_RUNNING")
            # Currently not running. Nothing to do.
            ocf_log info "Resource is already stopped"
        # WHAT:make sure temp fiiles are removed
        # WHY:
            ocf_log debug "ls -l ${OCF_RESKEY_vardir}/"
            ocf_log debug "Removing appropriate Nagios temp files"
            rm -f ${OCF_RESKEY_statusfile}
            rm -f ${OCF_RESKEY_tempfile}
            rm -f ${OCF_RESKEY_tempfile}
            rm -f ${OCF_RESKEY_PidFile}
            rm -f ${OCF_RESKEY_commandfile}

            return $OCF_SUCCESS
            ;;
    esac

    # actually shut down the resource here (make sure to immediately
    # exit with an $OCF_ERR_ error code if anything goes seriously
    # wrong)
    kill_nagios_process

    # After the resource has been stopped, check whether it shut down
    # correctly. If the resource stops asynchronously, the agent may
    # spin on the monitor function here -- if the resource does not
    # shut down within the defined timeout, the cluster manager will
    # consider the stop action failed
    sleep 2
    while nagios_monitor; do
        ocf_log debug "Resource has not stopped yet, waiting"
        sleep 2
    done
    
    # only return $OCF_SUCCESS if _everything_ succeeded as expected
    return $OCF_SUCCESS

}


nagios_start() {
	if 
	    silent_status 
 	then
	    ocf_log debug  "$0  already running (pid `cat ${OCF_RESKEY_PidFile}` )"
	    return $OCF_SUCCESS
	fi

    # exit immediately if configuration is not valid
    # only node has access to config files which reside on shared drbd file can start Nagios.
    # will exit with error if nagios_validate !=0.
    ocf_log debug "nagios_start: Called by PCS start,running nagios_validate_all"

#    if  nagios_validate_all 
#    then
#        ocf_log debug "nagios_start:nagios_validate_all: config errors"
#        return $OCF_ERR_CONFIGURED
#    fi


    # if resource is already running,no need to continue code after this.
    if nagios_monitor; then
        ocf_log debug "nagios_start: Called by PCS start, Resource is already running"
        return $OCF_SUCCESS
    fi

    # actually start up the resource here (make sure to immediately
    # exit with an $OCF_ERR_ error code if anything goes seriously
    # wrong)

    #su - ${OCF_RESKEY_user} -c "touch ${OCF_RESKEY_log} ${OCF_RESKEY_retentionfile}"
    #rm -f ${OCF_RESKEY_commandfile}
    #touch ${OCF_RESKEY_PidFile}
    #chown ${OCF_RESKEY_user}:${OCF_RESKEY_group} ${OCF_RESKEY_PidFile}
    ${OCF_RESKEY_binary} -d ${OCF_RESKEY_config}; rc=$?
    if [ $rc  = "0" ]; then
	get_nagios_pid
        ocf_log debug "start: Nagios started with PID=${NagiosPID}"
	return ${OCF_SUCCESS}
    fi

    # $?
    # not needed 
    #MYNAGPID=`ps auxww | grep nagios | grep -v grep | grep ${OCF_RESKEY_config} | awk '{print $2}'`
    #echo ${MYNAGPID}
    #echo ${MYNAGPID} > ${OCF_RESKEY_PidFile}

    # WHAT: After the resource has been started, check whether it started up
    # correctly. If the resource starts asynchronously, the agent may
    # spin on the monitor function here -- if the resource does not
    # start up within the defined timeout, the cluster manager will
    # consider the start action failed
#    while ! nagios_monitor; do
#        ocf_log debug "Resource has not started yet, waiting"
#        sleep 1
#    done

    # only return $OCF_SUCCESS if _everything_ succeeded as expected
    return $OCF_SUCCESS
}

# WHAT: reload nagios configuration 
# WHY:  so we can activate the changes of nagios configuration file.
# NOTE: 
nagios_reload() {
    # exit immediately if configuration is not valid
    ocf_log debug "nagios_reload: Running nagios_validate_all"
    nagios_validate_all || exit $?

    # if resource is already running, bail out early
    ocf_log debug "nagios_reload: calling nagios_monitor."
    if [ nagios_monitor ]; then
	PID=`cat $OCF_RESKEY_PidFile`
        ocf_log debug "kill -HUP ${PID} to reload config files"
	kill -HUP $PID
    fi
    # WHAT: After the resource has been started, check whether it started up
    # correctly. If the resource starts asynchronously, the agent may
    # spin on the monitor function here -- if the resource does not
    # start up within the defined timeout, the cluster manager will
    # consider the start action failed
    while ! nagios_monitor; do
        ocf_log debug "Resource has not started yet, waiting"
        sleep 1
    done

    # only return $OCF_SUCCESS if _everything_ succeeded as expected
    return $OCF_SUCCESS
}


# WHAT: find nagios running status
# WHY:  
# NOTE: 

# WHAT: find nagios running status
# WHY:  
# NOTE: 

# WHAT: find nagios running status
# WHY:  
# NOTE: 

# WHAT: find nagios running status
# WHY:  
# NOTE: 

# WHAT: Define PATH.
# WHY:  Mainly for security reasons.
# NOTE: This should only be changed if you know what you're doing.

# **************************** MAIN SCRIPT ************************************

# This OCF agent script need to be ran as root user.
if [ ! ocf_is_root  ]; then
        echo  "$0 agent script need to be ran as root user."
        ocf_log debug "$0 agent script need to be ran as root user."
        exit $OCF_ERR_GENERIC
fi

# Make sure meta-data and usage always succeed
case $__OCF_ACTION in
meta-data)      nagios_meta_data
                exit $OCF_SUCCESS
                ;;
usage|help)     nagios_usage
                exit $OCF_SUCCESS
                ;;
esac

# Anything other than meta-data and usage must pass validation
# this will failed on Nagios slave when config files are in master:/dev/drbd0
# nagios_validate_all || exit $?

# Translate each action into the appropriate function call
case $__OCF_ACTION in
start)          nagios_start;;
stop)           nagios_stop;;
nagiospid)      get_nagios_pid;;
status|monitor) nagios_monitor;;
#reload)         ocf_log info "Reloading Nagios configiruation file..."
#                nagios_reload
reload)         nagios_reload ;;
validate-all)   ;;
*)            	ocf_log info "$0 usage ..."
	        nagios_usage
                exit $OCF_ERR_UNIMPLEMENTED
                ;;
esac
rc=$?

# The resource agent may optionally log a debug message
ocf_log debug "${OCF_RESOURCE_INSTANCE} $__OCF_ACTION returned $rc"
exit $rc
  
# End of this script
