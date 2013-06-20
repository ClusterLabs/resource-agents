#!/bin/bash
#
# Copyright (C) 1997-2003 Sistina Software, Inc.  All rights reserved.
# Copyright (C) 2004-2013 Red Hat, Inc.  All rights reserved.
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
# Author(s):
#     Hardy Merrill <hmerrill at redhat.com>
#     Lon Hohberger <lhh at redhat.com>
#     Michael Moon <Michael dot Moon at oracle.com>
#     Ryan McCabe <rmccabe at redhat.com>
#
# NOTES:
#
# (1) You can comment out the LOCKFILE declaration below.  This will prevent
# the need for this script to access anything outside of the ORACLE_HOME 
# path.
#
# (2) You MUST customize ORACLE_USER, ORACLE_HOME, ORACLE_SID, and
# ORACLE_HOSTNAME to match your installation if not running from within
# rgmanager.
#
# (3) Do NOT place this script in shared storage; place it in ORACLE_USER's
# home directory in non-clustered environments and /usr/share/cluster
# in rgmanager/Red Hat cluster environments.
#
# Oracle is a registered trademark of Oracle Corporation.
# Oracle9i is a trademark of Oracle Corporation.
# Oracle10g is a trademark of Oracle Corporation.
# Oracle11g is a trademark of Oracle Corporation.
# All other trademarks are property of their respective owners.
#

. $(dirname $0)/ocf-shellfuncs
. $(dirname $0)/utils/config-utils.sh
. $(dirname $0)/utils/messages.sh
. $(dirname $0)/utils/ra-skelet.sh

. /etc/init.d/functions

declare SCRIPT="`basename $0`"
declare SCRIPTDIR="`dirname $0`"

[ -n "$OCF_RESKEY_user" ] && ORACLE_USER=$OCF_RESKEY_user
[ -n "$OCF_RESKEY_home" ] && ORACLE_HOME=$OCF_RESKEY_home
[ -n "$OCF_RESKEY_name" ] && ORACLE_SID=$OCF_RESKEY_name
[ -n "$OCF_RESKEY_listener_name" ] && ORACLE_LISTENER=$OCF_RESKEY_listener_name
[ -n "$OCF_RESKEY_lockfile" ] && LOCKFILE=$OCF_RESKEY_lockfile
[ -n "$OCF_RESKEY_type" ] && ORACLE_TYPE=$OCF_RESKEY_type
[ -n "$OCF_RESKEY_vhost" ] && ORACLE_HOSTNAME=$OCF_RESKEY_vhost

######################################################
# Customize these to match your Oracle installation. #
######################################################
#
# 1. Oracle user.  Must be the same across all cluster members.  In the event
#    that this script is run by the super-user, it will automatically switch
#    to the Oracle user and restart.  Oracle needs to run as the Oracle
#    user, not as root.
#
#[ -n "$ORACLE_USER" ] || ORACLE_USER=oracle

#
# 2. Oracle home.  This is set up during the installation phase of Oracle.
#    From the perspective of the cluster, this is generally the mount point
#    you intend to use as the mount point for your Oracle Infrastructure
#    service.
#
#[ -n "$ORACLE_HOME" ] || ORACLE_HOME=/mnt/oracle/home

#
# 3. This is your SID.  This is set up during oracle installation as well.
#
#[ -n "$ORACLE_SID" ] || ORACLE_SID=orcl

#
# 4. The oracle user probably doesn't have the permission to write to 
# /var/lock/subsys, so use the user's home directory.
#
#[ -n "$LOCKFILE" ] || LOCKFILE="/home/$ORACLE_USER/.oracle-ias.lock"
[ -n "$LOCKFILE" ] || LOCKFILE="$ORACLE_HOME/.oracle-ias.lock"
#[ -n "$LOCKFILE" ] || LOCKFILE="/var/lock/subsys/oracle-ias" # Watch privileges

#
# 5. Type of Oracle Database.  Currently supported: 10g 10g-iAS(untested!)
#
[ -n "$ORACLE_TYPE" ] || ORACLE_TYPE="base-em"

#
# 6. Oracle virtual hostname.  This is the hostname you gave Oracle during
#    installation.
#
#[ -n "$ORACLE_HOSTNAME" ] || ORACLE_HOSTNAME=svc0.foo.test.com



###########################################################################
ORACLE_TYPE=`echo $ORACLE_TYPE | tr A-Z a-z`
export ORACLE_USER ORACLE_HOME ORACLE_SID LOCKFILE ORACLE_TYPE
export ORACLE_HOSTNAME


##########################
# Set up paths we'll use.  Not all are used by all the different types of
# Oracle installations
#
export LD_LIBRARY_PATH=$ORACLE_HOME/lib:$ORACLE_HOME/opmn/lib
export PATH=$ORACLE_HOME/bin:$ORACLE_HOME/opmn/bin:$ORACLE_HOME/dcm/bin:$PATH

declare -i	RESTART_RETRIES=0
declare -r	DB_PROCNAMES="pmon"
#declare -r	DB_PROCNAMES="pmonXX" # testing
#declare -r	DB_PROCNAMES="pmon smon dbw0 lgwr"

declare -r	LSNR_PROCNAME="tnslsnr"
#declare -r	LSNR_PROCNAME="tnslsnrXX" # testing

# clulog will not log messages when run by the oracle user.
# This is a hack to work around that.
if [ "`id -u`" = "`id -u $ORACLE_USER`" ]; then
	ocf_log() {
		prio=$1
		shift
		logger -i -p daemon."$prio" -- "$*"
	}
fi

##########################################################
# (Hopefully) No user-serviceable parts below this line. #
##########################################################
meta_data()
{
	cat <<EOT
<?xml version="1.0" ?>
<resource-agent name="oracledb" version="rgmanager 2.0">
    <version>1.0</version>

    <longdesc lang="en">
	Oracle 10g/11g Failover Instance
    </longdesc>
    <shortdesc lang="en">
	Oracle 10g/11g Failover Instance
    </shortdesc>

    <parameters>
        <parameter name="name" primary="1">
	    <longdesc lang="en">
		Instance name (SID) of oracle instance
	    </longdesc>
            <shortdesc lang="en">
		Oracle SID
            </shortdesc>
	    <content type="string"/>
        </parameter>

        <parameter name="listener_name" unique="1">
	    <longdesc lang="en">
		Oracle Listener Instance Name.  If you have multiple 
		instances of Oracle running, it may be necessary to 
		have multiple listeners on the same machine with
		different names.
	    </longdesc>
            <shortdesc lang="en">
		Oracle Listener Instance Name
            </shortdesc>
	    <content type="string"/>
        </parameter>

        <parameter name="user" required="1">
	    <longdesc lang="en">
		Oracle user name.  This is the user name of the Oracle
		user which the Oracle AS instance runs as.
	    </longdesc>
            <shortdesc lang="en">
		Oracle User Name
            </shortdesc>
	    <content type="string"/>
        </parameter>

        <parameter name="home" required="1">
	    <longdesc lang="en">
		This is the Oracle (application, not user) home directory.
		This is configured when you install Oracle.
	    </longdesc>
            <shortdesc lang="en">
		Oracle Home Directory
            </shortdesc>
	    <content type="string"/>
        </parameter>

        <parameter name="type" required="0">
	    <longdesc lang="en">
		This is the Oracle installation type:
		base - Database Instance and Listener only
		base-11g - Oracle11g Database Instance and Listener Only
		base-em (or 10g) - Database, Listener, Enterprise Manager,
				   and iSQL*Plus
		base-em-11g - Database, Listener, Enterprise Manager dbconsole
		ias (or 10g-ias) - Internet Application Server (Infrastructure)
	    </longdesc>
            <shortdesc lang="en">
		Oracle Installation Type
            </shortdesc>
	    <content type="string"/>
        </parameter>

        <parameter name="vhost" required="0" unique="1">
	    <longdesc lang="en">
	        Virtual Hostname matching the installation hostname of
		Oracle 10g.  Note that during the start/stop of an oracledb
		resource, your hostname will temporarily be changed to
		this hostname.  As such, it is recommended that oracledb
		resources be instanced as part of an exclusive service only.
	    </longdesc>
            <shortdesc lang="en">
		Virtual Hostname
            </shortdesc>
	    <content type="string"/>
        </parameter>

    </parameters>

    <actions>
        <action name="start" timeout="900"/>
	<action name="stop" timeout="90"/>
        <action name="recover" timeout="990"/>

	<!-- Checks to see if it's mounted in the right place -->
	<action name="status" timeout="10"/>
	<action name="monitor" timeout="10"/>

	<action name="status" depth="10" timeout="30" interval="30"/>
	<action name="monitor" depth="10" timeout="30" interval="30"/>

	<action name="meta-data" timeout="5"/>
	<action name="validate-all" timeout="5"/>
    </actions>

    <special tag="rgmanager">
	<attributes maxinstances="1"/>
    </special>
</resource-agent>
EOT
}

#
# Start Oracle9i/10g/11g (database portion)
#
start_db()
{
	declare -i rv
	declare startup_cmd
	declare startup_stdout

	ocf_log info "Starting Oracle DB $ORACLE_SID"

	# Set up our sqlplus script.  Basically, we're trying to 
	# capture output in the hopes that it's useful in the case
	# that something doesn't work properly.
	startup_cmd="set heading off;\nstartup;\nquit;\n"
	startup_stdout=$(echo -e "$startup_cmd" | sqlplus -S "/ as sysdba")
	rv=$?

	# Dump output to syslog for debugging
	ocf_log debug "[$ORACLE_SID] [$rv] sent $startup_cmd"
	ocf_log debug "[$ORACLE_SID] [$rv] got $startup_stdout"

	if [ $rv -ne 0 ]; then
	    ocf_log error "Starting Oracle DB $ORACLE_SID failed, sqlplus returned $rv"
	    return 1
	fi

	# If we see:
	# ORA-.....: failure, we failed
	# Troubleshooting:
	#   ORA-00845 - Try rm -f /dev/shm/ora_*
	#   ORA-01081 - Try echo -e 'shutdown abort;\nquit;'|sqlplus "/ as sysdba"
	if [[ "$startup_stdout" =~ "ORA-" ]] || [[ "$startup_stdout" =~ "failure" ]]; then
	    ocf_log error "Starting Oracle DB $ORACLE_SID failed, found errors in stdout"
	    return 1
	fi

	ocf_log info "Started Oracle DB $ORACLE_SID successfully"
	return 0
}


#
# Stop Oracle (database portion)
#
stop_db()
{
	declare stop_cmd
	declare stop_stdout
	declare -i rv
	declare how_shutdown="$1"

	if [ -z "$1" ]; then
		how_shutdown="immediate"
	fi

	ocf_log info "Stopping Oracle DB $ORACLE_SID $how_shutdown"

	# Setup for Stop ...
	stop_cmd="set heading off;\nshutdown $how_shutdown;\nquit;\n"
	stop_stdout=$(echo -e "$stop_cmd" | sqlplus -S "/ as sysdba")
	rv=$?

	# Log stdout of the stop command
	ocf_log debug "[$ORACLE_SID] sent stop command $stop_cmd"
	ocf_log debug "[$ORACLE_SID] got $stop_stdout"

	# sqlplus returned failure. We'll return failed to rhcs
	if [ $rv -ne 0 ]; then
	    ocf_log error "Stopping Oracle DB $ORACLE_SID failed, sqlplus returned $rv"
	    return 1
	fi

	# If we see 'ORA-' or 'failure' in stdout, we're done.
	if [[ "$startup_stdout" =~ "ORA-" ]] || [[ "$startup_stdout" =~ "failure" ]]; then
	    ocf_log error "Stopping Oracle DB $ORACLE_SID failed, errors in stdout"
	    return 1
	fi

	ocf_log info "Stopped Oracle DB $ORACLE_SID successfully"
	return 0
}


#
# Destroy any remaining processes with refs to $ORACLE_HOME
#
force_cleanup()
{
	declare pids
	declare pid

	# Patch from Shane Bradley to fix 471266
	pids=`ps ax | grep $ORACLE_HOME | grep "ora_.*_${ORACLE_SID}" | grep -v grep | awk '{print $1}'`

	ocf_log error "Not all Oracle processes for $ORACLE_SID exited cleanly, killing"

	for pid in $pids; do
		kill -9 $pid
		rv=$?
		if [ $rv -eq 0 ]; then
			ocf_log info "Cleanup $ORACLE_SID Killed PID $pid"
		else
			ocf_log error "Cleanup $ORACLE_SID Kill PID $pid failed: $rv"
		fi
	done

	return 0
}



#
# Wait for oracle processes to exit.  Time out after 60 seconds
#
exit_idle()
{
	declare -i n=0

	ocf_log debug "Waiting for Oracle processes for $ORACLE_SID to terminate..."
	while ps ax | grep $ORACLE_HOME | grep -q -v grep; do
		if [ $n -ge 90 ]; then
			ocf_log debug "Timed out while waiting for Oracle processes for $ORACLE_SID to terminate"
			force_cleanup
			return 0
		fi
		sleep 1
		((n++))
	done

	ocf_log debug "All Oracle processes for $ORACLE_SID have terminated"
	return 0
}


#
# Get database background process status.  Restart it if it failed and
# we have seen the lock file.
#
get_db_status()
{
	declare -i subsys_lock=$1
	declare -i i=0
	declare -i rv=0
	declare ora_procname

	for procname in $DB_PROCNAMES ; do

		ora_procname="ora_${procname}_${ORACLE_SID}"
		
		status $ora_procname
		if [ $? -eq 0 ] ; then
			# This one's okay; go to the next one.
			continue
		fi

		#
		# We're not supposed to be running, and we are,
		# in fact, not running...
		# XXX only works when monitoring one db process; consider
		# extending in future.
		#
		if [ $subsys_lock -ne 0 ]; then
			return 3
		fi

		for (( i=$RESTART_RETRIES ; i; i-- )) ; do
			# this db process is down - stop and
			# (re)start all ora_XXXX_$ORACLE_SID processes
			ocf_log info "Restarting Oracle Database $ORACLE_SID"
			stop_db immediate
			if [ $? -ne 0 ] ; then
				# stop failed - return 1
				ocf_log error "Error stopping Oracle Database $ORACLE_SID"
				return 1
			fi

			start_db
			if [ $? -eq 0 ] ; then
				# ora_XXXX_$ORACLE_SID processes started
				# successfully, so break out of the
				# stop/start # 'for' loop
				ocf_log info "Restarted Oracle Database $ORACLE_SID successfully"
				break
			fi
		done

		if [ $i -eq 0 ]; then
			# stop/start's failed - return 1 (failure)
			ocf_log error "Failed to restart Oracle Database $ORACLE_SID after $RESTART_RETRIES tries"
			return 1
		fi
	done
	return 0
}


#
# Get the status of the Oracle listener process
#
get_lsnr_status() 
{
	declare -i subsys_lock=$1
	declare -i rv

	ocf_log debug "Checking status for listener $ORACLE_LISTENER"
	lsnrctl status "$ORACLE_LISTENER" >& /dev/null
	rv=$?
	if [ $rv -eq 0 ] ; then
		ocf_log debug "Listener $ORACLE_LISTENER is up"
		return 0 # Listener is running fine
	fi

	# We're not supposed to be running, and we are,
	# in fact, not running.  Return 3
	if [ $subsys_lock -ne 0 ]; then
		ocf_log debug "Listener $ORACLE_LISTENER is stopped as expected"
		return 3
	fi

	# Listener is NOT running (but should be) - try to restart
	for (( i=$RESTART_RETRIES ; i; i-- )) ; do
		ocf_log info "Listener $ORACLE_LISTENER is down, attempting to restart"
		lsnrctl start "$ORACLE_LISTENER" >& /dev/null
		lsnrctl status "$ORACLE_LISTENER" >& /dev/null
		if [ $? -eq 0 ] ; then
			ocf_log info "Listener $ORACLE_LISTENER was restarted successfully"
			break # Listener was (re)started and is running fine
		fi
	done

	if [ $i -eq 0 ]; then
		# stop/start's failed - return 1 (failure)
		ocf_log error "Failed to restart listener $ORACLE_LISTENER after $RESTART_RETRIES tries"
		return 1
	fi

	lsnrctl_stdout=$(lsnrctl status "$ORACLE_LISTENER")
	rv=$?
	if [ $rv -ne 0 ] ; then
		ocf_log error "Starting listener $ORACLE_LISTENER failed: $rv output $lsnrctl_stdout"
		return 1 # Problem restarting the Listener
	fi

	ocf_log info "Listener $ORACLE_LISTENER started successfully"
	return 0 # Success restarting the Listener
}


#
# usage: get_opmn_proc_status <ias-component> [process-type]
#
# Get the status of a specific OPMN-managed process.  If process-type
# is not specified, assume the process-type is the same as the ias-component.
# If the lock-file exists (or no lock file is specified), try to restart
# the given process-type if it is not running.
#
get_opmn_proc_status()
{
	declare comp=$1
	declare opmntype=$2
	declare type_pretty
	declare _pid _status
	
	[ -n "$comp" ] || return 1
	if [ -z "$opmntype" ]; then
		opmntype=$comp
	else
		type_pretty=" [$opmntype]"
	fi

	for (( i=$RESTART_RETRIES ; i; i-- )) ; do

		_status=`opmnctl status | grep "^$comp " | grep " $opmntype " | cut -d '|' -f3,4 | sed -e 's/ //g' -e 's/|/ /g'`

		_pid=`echo $_status | cut -f1 -d' '`
		_status=`echo $_status | cut -f2 -d' '`
		if [ "${_status}" == "Alive" ] || [ "${_status}" == "Init" ]; then
			if [ $i -lt $RESTART_RETRIES ] ; then
				ocf_log info "$comp$type_pretty restarted"
			fi
			ocf_log info "$comp$type_pretty (pid $_pid) is running..."
			break
		else
			ocf_log info "$comp$type_pretty is stopped"

			#
			# Try to restart it, but don't worry if we fail.  OPMN
			# is supposed to handle restarting these anyway.
			#
			# If it's running and you tell OPMN to "start" it,
			# you will get an error.
			#
			# If it's NOT running and you tell OPMN to "restart"
			# it, you will also get an error.
			#
			opmnctl startproc process-type=$opmntype &> /dev/null
		fi
	done

	if [ $i -eq 0 ]; then
		# restarts failed - return 1 (failure)
		ocf_log error "Failed to restart OPMN process $comp"
		return 1
	fi

	return 0
}


#
# Get the status of the OPMN-managed processes.
#
get_opmn_status()
{
	declare -i subsys_lock=$1
	declare -i ct_errors=0

	opmnctl status &> /dev/null
	if [ $? -eq 2 ]; then
		#
		# OPMN not running??
		#
		ocf_log info "OPMN is stopped"

		if [ $subsys_lock -eq 0 ]; then
			#
			# Don't handle full opmn-restart. XXX
			#
			return 1
		fi

		# That's okay, it's not supposed to be!
		return 3
	fi

	#
	# Print out the PIDs for everyone.
	#
	ocf_log info "OPMN is running..."
	ocf_log info "opmn components:"

	#
	# Check the OPMN-managed processes
	#
	get_opmn_proc_status OID || ((ct_errors++))
	get_opmn_proc_status HTTP_Server || ((ct_errors++))
	get_opmn_proc_status OC4J OC4J_SECURITY || ((ct_errors++))

	#
	# One or more OPMN-managed processes failed and could not be
	# restarted.
	#
	if [ $ct_errors -ne 0 ]; then
		ocf_log error "$ct_errors errors occurred while restarting OPMN-managed processes"
		return 1
	fi
	return 0
}


#
# Helps us keep a running status so we know what our ultimate return
# code will be.  Returns 1 if the $1 and $2 are not equivalent, otherwise
# returns $1.  The return code is meant to be the next $1 when this is
# called, so, for example:
#
# update_status 0   <-- returns 0
# update_status $? 0 <-- returns 0
# update_status $? 3 <-- returns 1 (values different - error condition)
# update_status $? 1 <-- returns 1 (same, but happen to be error state!)
#
# update_status 3
# update_status $? 3 <-- returns 3
#
# (and so forth...)
#
update_status()
{
	declare -i old_status=$1
	declare -i new_status=$2

	if [ -z "$2" ]; then
		return $old_status
	fi

	if [ $old_status -ne $new_status ]; then
		return 1
	fi

	return $old_status
}


#
# Print an error message to the user and exit.
#
oops()
{
	ocf_log error "$ORACLE_SID: Fatal: $1 failed validation checks"
	exit 1
}


#
# Do some validation on the user-configurable stuff at the beginning of the
# script.
#
validation_checks()
{
	ocf_log debug "Validating configuration for $ORACLE_SID"

	#
	# If the oracle user doesn't exist, we're done.
	#
	[ -n "$ORACLE_USER" ] || oops "ORACLE_USER"
	id -u $ORACLE_USER > /dev/null || oops "ORACLE_USER"
	id -g $ORACLE_USER > /dev/null || oops "ORACLE_USER"

	#
	# If the oracle home isn't a directory, we're done
	#
	[ -n "$ORACLE_HOME" ] || oops ORACLE_HOME
	#[ -d "$ORACLE_HOME" ] || oops ORACLE_HOME

	#
	# If the oracle SID is NULL, we're done
	#
	[ -n "$ORACLE_SID" ] || oops ORACLE_SID

	#
	# If we don't know the type, we're done
	#
	if [ "$ORACLE_TYPE" = "base" ]; then
		# Other names for base
		ORACLE_TYPE="base"
	elif [ "$ORACLE_TYPE" = "10g" ] || [ "$ORACLE_TYPE" = "base-em" ]; then
		ORACLE_TYPE="base-em"
	elif [ "$ORACLE_TYPE" = "10g-ias" ] || [ "$ORACLE_TYPE" = "ias" ]; then
		ORACLE_TYPE="ias"
	elif [ "$ORACLE_TYPE" = "11g" ] || [ "$ORACLE_TYPE" = "base-em-11g" ]; then
		ORACLE_TYPE="base-em-11g"
	elif [ "$ORACLE_TYPE" = "base-11g" ]; then
		ORACLE_TYPE="base-11g"
	else
		oops "ORACLE_TYPE $ORACLE_TYPE"
	fi

	#
	# If the hostname is zero-length, fix it
	#
	[ -n "$ORACLE_HOSTNAME" ] || ORACLE_HOSTNAME=`hostname`

	#
	# Super user? Automatically change UID and exec as oracle user.
	# Oracle needs to be run as the Oracle user, not root!
	#
	if [ "`id -u`" = "0" ]; then
		#echo "Restarting $0 as $ORACLE_USER."
		#
		# Breaks on RHEL5 
		# exec sudo -u $ORACLE_USER $0 $*
		#
		su $ORACLE_USER -c "$0 $*"
		exit $?
	fi

	#
	# If we're not root and not the Oracle user, we're done.
	#
	[ "`id -u`" = "`id -u $ORACLE_USER`" ] || oops "not ORACLE_USER after su"
	[ "`id -g`" = "`id -g $ORACLE_USER`" ] || oops "not ORACLE_GROUP after su"

	#
	# Go home.
	#
	cd "$ORACLE_HOME"

	ocf_log debug "Validation checks for $ORACLE_SID succeeded"
	return 0
}


#
# Start Oracle 9i/10g/11g Application Server Infrastructure
#
start_oracle()
{
	ocf_log info "Starting service $ORACLE_SID"

	start_db
	rv=$?
	if [ $rv -ne 0 ]; then
		ocf_log error "Starting service $ORACLE_SID failed"
		return 1
	fi

	ocf_log info "Starting listener $ORACLE_LISTENER"
	lsnrctl_stdout=$(lsnrctl start "$ORACLE_LISTENER")
	rv=$?
	if [ $rv -ne 0 ]; then
		ocf_log debug "[$ORACLE_SID] Listener $ORACLE_LISTENER start returned $rv output $lsnrctl_stdout"
		ocf_log error "Starting service $ORACLE_SID failed"
		return 1
	fi

	if [ "$ORACLE_TYPE" = "base-em" ]; then
		ocf_log info "Starting iSQL*Plus for $ORACLE_SID"
		isqlplusctl start
		if [ $? -ne 0 ]; then
			ocf_log error "iSQL*Plus startup for $ORACLE_SID failed"
			ocf_log error "Starting service $ORACLE_SID failed"
			return 1
		else
			ocf_log info "iSQL*Plus startup for $ORACLE_SID succeeded"
		fi

		ocf_log info "Starting Oracle EM DB Console for $ORACLE_SID"
		emctl start dbconsole
		if [ $? -ne 0 ]; then
			ocf_log error "Oracle EM DB Console startup for $ORACLE_SID failed"
			ocf_log error "Starting service $ORACLE_SID failed"
			return 1
		else
			ocf_log info "Oracle EM DB Console startup for $ORACLE_SID succeeded"
		fi
	elif [ "$ORACLE_TYPE" = "ias" ]; then
		ocf_log info "Starting Oracle EM for $ORACLE_SID"
		emctl start em
		if [ $? -ne 0 ]; then
			ocf_log error "Oracle EM startup for $ORACLE_SID failed"
			ocf_log error "Starting service $ORACLE_SID failed"
			return 1
		else
			ocf_log info "Oracle EM startup for $ORACLE_SID succeeded"
		fi

		ocf_log info "Starting iAS Infrastructure for $ORACLE_SID"
		opmnctl startall
		if [ $? -ne 0 ]; then
			ocf_log error "iAS Infrastructure startup for $ORACLE_SID failed"
			ocf_log error "Starting service $ORACLE_SID failed"
			return 1
		else
			ocf_log info "iAS Infrastructure startup for $ORACLE_SID succeeded"
		fi
	elif [ "$ORACLE_TYPE" = "base-em-11g" ]; then
		ocf_log info "Starting Oracle EM DB Console for $ORACLE_SID"
		emctl start dbconsole
		if [ $? -ne 0 ]; then
			ocf_log error "Oracle EM DB Console startup for $ORACLE_SID failed"
			ocf_log error "Starting service $ORACLE_SID failed"
			return 1
		else
			ocf_log info "Oracle EM DB Console startup for $ORACLE_SID succeeded"
		fi
	fi

	if [ -n "$LOCKFILE" ]; then
		touch "$LOCKFILE"
	fi

	ocf_log info "Starting service $ORACLE_SID completed successfully"
	return 0
}


#
# Stop Oracle 9i/10g/11g Application Server Infrastructure
#
stop_oracle()
{
	ocf_log info "Stopping service $ORACLE_SID"

	if ! [ -e "$ORACLE_HOME/bin/lsnrctl" ]; then
		ocf_log error "Oracle Listener Control is not available ($ORACLE_HOME not mounted?)"
		return 0
	fi

	if [ "$ORACLE_TYPE" = "base-em" ]; then
		ocf_log info "Stopping Oracle EM DB Console for $ORACLE_SID"
		emctl stop dbconsole
		if [ $? -ne 0 ]; then
			ocf_log error "Stopping Oracle EM DB Console for $ORACLE_SID failed"
			ocf_log error "Stopping service $ORACLE_SID failed"
			return 1
		else
			ocf_log info "Stopping Oracle EM DB Console for $ORACLE_SID succeeded"
		fi

		ocf_log info "Stopping iSQL*Plus for $ORACLE_SID"
		isqlplusctl stop
		if [ $? -ne 0 ]; then
			ocf_log error "Stopping iSQL*Plus for $ORACLE_SID failed"
			ocf_log error "Stopping service $ORACLE_SID failed"
			return 1
		else
			ocf_log info "Stopping iSQL*Plus for $ORACLE_SID succeeded"
		fi
	elif [ "$ORACLE_TYPE" = "ias" ]; then
		ocf_log info "Stopping iAS Infrastructure for $ORACLE_SID"
		opmnctl stopall
		if [ $? -ne 0 ]; then
			ocf_log error "Stopping iAS Infrastructure for $ORACLE_SID failed"
			ocf_log error "Stopping service $ORACLE_SID failed"
			return 1
		else
			ocf_log info "Stopping iAS Infrastructure for $ORACLE_SID succeeded"
		fi

		ocf_log info "Stopping Oracle EM for $ORACLE_SID"
		emctl stop em
		if [ $? -ne 0 ]; then
			ocf_log error "Stopping Oracle EM for $ORACLE_SID failed"
			ocf_log error "Stopping service $ORACLE_SID failed"
			return 1
		else
			ocf_log info "Stopping Oracle EM for $ORACLE_SID succeeded"
		fi
	elif [ "$ORACLE_TYPE" = "base-em-11g" ]; then
		ocf_log info "Stopping Oracle EM DB Console for $ORACLE_SID"
		emctl stop dbconsole
		if [ $? -ne 0 ]; then
			ocf_log error "Stopping Oracle EM DB Console for $ORACLE_SID failed"
			ocf_log error "Stopping service $ORACLE_SID failed"
			return 1
		else
			ocf_log info "Stopping Oracle EM DB Console for $ORACLE_SID succeeded"
		fi
	fi

	stop_db immediate || stop_db abort
	if [ $? -ne 0 ]; then
		ocf_log error "Stopping service $ORACLE_SID failed"
		return 1
	fi

	ocf_log info "Stopping listener $ORACLE_LISTENER for $ORACLE_SID"
	lsnrctl_stdout=$(lsnrctl stop "$ORACLE_LISTENER")
	rv=$?
	if [ $? -ne 0 ]; then
		ocf_log error "Listener $ORACLE_LISTENER stop failed for $ORACLE_SID: $rv output $lsnrctl_stdout"
		# XXX - failure?
	fi

	exit_idle 
	if [ $? -ne 0 ]; then
		ocf_log warning "WARNING: Not all Oracle processes exited cleanly for $ORACLE_SID"
	fi

	if [ -n "$LOCKFILE" ]; then
		rm -f "$LOCKFILE"
	fi

	ocf_log info "Stopping service $ORACLE_SID succeeded"
	return 0
}


#
# Find and display the status of iAS infrastructure.
#
# This has three parts:
# (1) Oracle database itself
# (2) Oracle listener process
# (3) OPMN and OPMN-managed processes
#
# - If all are (cleanly) down, we return 3.  In order for this to happen,
# $LOCKFILE must not exist.  In this case, we try and restart certain parts
# of the service - as this may be running in a clustered environment.
#
# - If some but not all are running (and, if $LOCKFILE exists, we could not
# restart the failed portions), we return 1 (ERROR)
#
# - If all are running, return 0.  In the "all-running" case, we recreate
# $LOCKFILE if it does not exist.
#
status_oracle()
{
	declare -i subsys_lock=1
	declare -i last 

	ocf_log debug "Checking status for $ORACLE_SID depth $depth"

	#
	# Check for lock file.  Crude and rudimentary, but it works
	#
	if [ -z "$LOCKFILE" ] || [ -f "$LOCKFILE" ]; then
		subsys_lock=0 
	fi

	# Check database status
	get_db_status $subsys_lock
	update_status $? # Start
	last=$?

	# Check & report listener status
	get_lsnr_status $subsys_lock
	update_status $? $last
	last=$?
	
	if [ "$ORACLE_TYPE" = "base-em" ] || [ "$ORACLE_TYPE" = "base-em-11g" ]; then
		# XXX Add isqlplus status check?!
		emctl status dbconsole >&/dev/null
		update_status $? $last
		last=$?
	elif [ "$ORACLE_TYPE" = "ias" ]; then
		# Check & report opmn / opmn-managed process status
		get_opmn_status $subsys_lock
		update_status $? $last
		last=$?
	fi

	#
	# No lock file, but everything's running.  Put the lock
	# file back. XXX - this kosher?
	#
	if [ $last -eq 0 ] && [ $subsys_lock -ne 0 ]; then
		touch "$LOCKFILE"
	fi

	ocf_log debug "Status returning $last for $ORACLE_SID"
	return $last
}


########################
# Do some real work... #
########################
if [ "$1" = "meta-data" ]; then
	meta_data
	exit 0
fi

validation_checks $*

case $1 in
	start)
		start_oracle
		exit $?
		;;
	stop)
		stop_oracle
		exit $?
		;;
	status|monitor)
		status_oracle
		exit $?
		;;
	restart)
		$0 stop || exit $?
		$0 start || exit $?
		exit 0
		;;
	*)
		echo "usage: $SCRIPT {start|stop|status|restart|meta-data}"
		exit 1
		;;
esac
exit 0
