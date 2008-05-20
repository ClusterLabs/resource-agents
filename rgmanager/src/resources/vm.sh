#!/bin/bash
#
#  Copyright Red Hat Inc., 2005-2006
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

PATH=/bin:/sbin:/usr/bin:/usr/sbin

export PATH

. $(dirname $0)/ocf-shellfuncs || exit 1

. $(dirname $0)/ocf-shellfuncs

#
# Virtual Machine start/stop script (requires the xm command)
#

meta_data()
{
    cat <<EOT
<?xml version="1.0"?>
<resource-agent version="rgmanager 2.0" name="vm">
    <version>1.0</version>

    <longdesc lang="en">
	Defines a Virtual Machine
    </longdesc>
    <shortdesc lang="en">
        Defines a Virtual Machine
    </shortdesc>

    <parameters>
        <parameter name="name" primary="1">
            <longdesc lang="en">
                This is the name of the virtual machine.
            </longdesc>
            <shortdesc lang="en">
                Name
            </shortdesc>
            <content type="string"/>
        </parameter>
    
        <parameter name="domain" reconfig="1">
            <longdesc lang="en">
                Fail over domains define lists of cluster members
                to try in the event that the host of the virtual machine
		fails.
            </longdesc>
            <shortdesc lang="en">
                Cluster Fail Over Domain
            </shortdesc>
            <content type="string"/>
        </parameter>

        <parameter name="autostart" reconfig="1">
            <longdesc lang="en">
	    	If set to yes, this resource group will automatically be started
		after the cluster forms a quorum.  If set to no, this virtual
		machine will start in the 'disabled' state after the cluster
		forms a quorum.
            </longdesc>
            <shortdesc lang="en">
	    	Automatic start after quorum formation
            </shortdesc>
            <content type="boolean" default="1"/>
        </parameter>

        <parameter name="hardrecovery" reconfig="1">
            <longdesc lang="en">
	    	If set to yes, the last owner will reboot if this resource
		group fails to stop cleanly, thus allowing the resource
		group to fail over to another node.  Use with caution; a
		badly-behaved resource could cause the entire cluster to
		reboot.  This should never be enabled if the automatic
		start feature is used.
            </longdesc>
            <shortdesc lang="en">
	    	Reboot if stop phase fails
            </shortdesc>
            <content type="boolean" default="0"/>
        </parameter>

        <parameter name="exclusive" reconfig="1">
            <longdesc lang="en">
	    	If set, this resource group will only relocate to
		nodes which have no other resource groups running in the
		event of a failure.  If no empty nodes are available,
		this resource group will not be restarted after a failure.
		Additionally, resource groups will not automatically
		relocate to the node running this resource group.  This
		option can be overridden by manual start and/or relocate
		operations.
            </longdesc>
            <shortdesc lang="en">
	        Exclusive resource group
            </shortdesc>
            <content type="boolean" default="0"/>
        </parameter>

        <parameter name="recovery" reconfig="1">
            <longdesc lang="en">
	        This currently has three possible options: "restart" tries
		to restart this virtual machine locally before
		attempting to relocate (default); "relocate" does not bother
		trying to restart the VM locally; "disable" disables
		the VM if it fails.
            </longdesc>
            <shortdesc lang="en">
	    	Failure recovery policy
            </shortdesc>
            <content type="string"/>
        </parameter>

	<parameter name="memory" reconfig="1">
	    <longdesc lang="en">
		Memory size.  This can be reconfigured on the fly.
	    </longdesc>
	    <shortdesc lang="en">
		Memory Size
	    </shortdesc>
            <content type="integer"/>
        </parameter>

       <parameter name="migration_mapping">
           <longdesc lang="en">
               Mapping of the hostname of a target cluster member to a different hostname
           </longdesc>
           <shortdesc lang="en">
               memeberhost:targethost,memeberhost:targethost ..
           </shortdesc>
            <content type="string"/>
        </parameter>

	<parameter name="bootloader">
	    <longdesc lang="en">
		Boot loader that can start the VM from physical image
	    </longdesc>
	    <shortdesc lang="en">
		Boot loader that can start the VM from physical image
	    </shortdesc>
            <content type="string"/>
        </parameter>

	<parameter name="path">
	    <longdesc lang="en">
	    	Path specification 'xm create' will search for the specified
		VM configuration file
	    </longdesc>
	    <shortdesc lang="en">
	    	Path to virtual machine configuration files
	    </shortdesc>
            <content type="string"/>
        </parameter>


	<parameter name="rootdisk_physical" unique="1">
	    <longdesc lang="en">
		Root disk for the virtual machine.  (physical, on the host)
	    </longdesc>
	    <shortdesc lang="en">
		Root disk (physical)
	    </shortdesc>
            <content type="string"/>
        </parameter>
        
	<parameter name="rootdisk_virtual">
	    <longdesc lang="en">
		Root disk for the virtual machine.  (as presented to the VM)
	    </longdesc>
	    <shortdesc lang="en">
		Root disk (virtual)
	    </shortdesc>
            <content type="string"/>
        </parameter>


	<parameter name="swapdisk_physical" unique="1">
	    <longdesc lang="en">
		Swap disk for the virtual machine.  (physical, on the host)
	    </longdesc>
	    <shortdesc lang="en">
		Swap disk (physical)
	    </shortdesc>
            <content type="string"/>
        </parameter>
        
	<parameter name="swapdisk_virtual">
	    <longdesc lang="en">
		Swap disk for the virtual machine.  (as presented to the VM)
	    </longdesc>
	    <shortdesc lang="en">
		Swap disk (virtual)
	    </shortdesc>
            <content type="string"/>
        </parameter>

	<parameter name="vif">
	    <longdesc lang="en">
		Virtual interface MAC address
	    </longdesc>
	    <shortdesc lang="en">
		Virtual interface MAC address
	    </shortdesc>
            <content type="string"/>
        </parameter>

	<parameter name="migrate">
	    <longdesc lang="en">
	    	Migration type live or pause, default = live.
	    </longdesc>
	    <shortdesc lang="en">
	    	Migration type live or pause, default = live.
	    </shortdesc>
            <content type="string" default="live"/>
        </parameter>

        <parameter name="depend">
            <longdesc lang="en">
		Top-level service this depends on, in "service:name" format.
            </longdesc>
            <shortdesc lang="en">
		Service dependency; will not start without the specified
		service running.
            </shortdesc>
            <content type="string"/>
        </parameter>

        <parameter name="depend_mode">
            <longdesc lang="en">
	    	Dependency mode
            </longdesc>
            <shortdesc lang="en">
		Service dependency mode.
		hard - This service is stopped/started if its dependency
		       is stopped/started
		soft - This service only depends on the other service for
		       initial startip.  If the other service stops, this
		       service is not stopped.
            </shortdesc>
            <content type="string" default="hard"/>
        </parameter>

        <parameter name="max_restarts" reconfig="1">
            <longdesc lang="en">
	    	Maximum restarts for this service.
            </longdesc>
            <shortdesc lang="en">
	    	Maximum restarts for this service.
            </shortdesc>
            <content type="string" default="0"/>
        </parameter>

        <parameter name="restart_expire_time" reconfig="1">
            <longdesc lang="en">
	    	Restart expiration time
            </longdesc>
            <shortdesc lang="en">
	    	Restart expiration time.  A restart is forgotten
		after this time.  When combined with the max_restarts
		option, this lets administrators specify a threshold
		for when to fail over services.  If max_restarts
		is exceeded in this given expiration time, the service
		is relocated instead of restarted again.
            </shortdesc>
            <content type="string" default="0"/>
        </parameter>

    </parameters>

    <actions>
        <action name="start" timeout="20"/>
        <action name="stop" timeout="120"/>
	
        <action name="status" timeout="10" interval="30"/>
        <action name="monitor" timeout="10" interval="30"/>

	<!-- reconfigure - reconfigure with new OCF parameters.
	     NOT OCF COMPATIBLE AT ALL -->
	<action name="reconfig" timeout="10"/>

	<!-- Suspend: if available, suspend this resource instead of
	     doing a full stop. -->
	<!-- <action name="suspend" timeout="10m"/> -->
	<action name="migrate" timeout="10m"/>

        <action name="meta-data" timeout="5"/>
        <action name="validate-all" timeout="5"/>

    </actions>
    
    <special tag="rgmanager">
    	<!-- Destroy_on_delete / init_on_add are currently only
	     supported for migratory resources (no children
	     and the 'migrate' action; see above.  Do not try this
	     with normal services -->
        <attributes maxinstances="1" destroy_on_delete="0" init_on_add="0"/>
    </special>
</resource-agent>
EOT
}


build_xm_cmdline()
{
	#
	# Virtual domains should never restart themselves when 
	# controlled externally; the external monitoring app
	# should.
	#
	declare cmdline="on_shutdown=\"destroy\" on_reboot=\"destroy\" on_crash=\"destroy\""
	declare varp val temp

	#
	# Transliterate the OCF_RESKEY_* to something the xm
	# command can recognize.
	#
	for var in ${!OCF_RESKEY_*}; do
		varp=${var/OCF_RESKEY_/}
		val=`eval "echo \\$$var"`

		case $varp in
		bootloader)
			cmdline="$cmdline bootloader=\"$val\""
			;;
		rootdisk_physical)
			[ -n "$OCF_RESKEY_rootdisk_virtual" ] || exit 2
			cmdline="$cmdline disk=\"phy:$val,$OCF_RESKEY_rootdisk_virtual,w\""
			;;
		swapdisk_physical)
			[ -n "$OCF_RESKEY_swapdisk_virtual" ] || exit 2
			cmdline="$cmdline disk=\"phy:$val,$OCF_RESKEY_swapdisk_virtual,w\""
			;;
		vif)
			cmdline="$cmdline vif=\"mac=$val\""
			;;
		recovery|autostart|domain)
			;;
		memory)
			cmdline="$cmdline $varp=$val"
			;;
		swapdisk_virtual)
			;;
		rootdisk_virtual)
			;;
		name)	# Do nothing with name; add it later
			;;
		path)
			cmdline="$cmdline --path=\"$val\""
			;;
		migrate)
			;;
		*)
			cmdline="$cmdline $varp=\"$val\""
			;;
		esac
	done

	if [ -n "$OCF_RESKEY_name" ]; then
		cmdline="$OCF_RESKEY_name $cmdline"
	fi

	echo $cmdline
}


#
# Start a virtual machine given the parameters from
# the environment.
#
do_start()
{
	# Use /dev/null for the configuration file, if xmdefconfig
	# doesn't exist...
	#
	declare cmdline

	status && return 0

	cmdline="`build_xm_cmdline`"

	echo "# xm command line: $cmdline"

	eval xm create $cmdline
	return $?
}


#
# Stop a VM.  Try to shut it down.  Wait a bit, and if it
# doesn't shut down, destroy it.
#
do_stop()
{
	declare -i timeout=60
	declare -i ret=1
	declare st

	for op in $*; do
		echo xm $op $OCF_RESKEY_name ...
		xm $op $OCF_RESKEY_name

		timeout=60
		while [ $timeout -gt 0 ]; do
			sleep 5
			((timeout -= 5))
			status || return 0
			while read dom state; do
				#
				# State is "stopped".  Kill it.
				#
				if [ "$dom" != "$OCF_RESKEY_name" ]; then
					continue
				fi
				if [ "$state" != "---s-" ]; then
					continue
				fi
				xm destroy $OCF_RESKEY_name
			done < <(xm list | awk '{print $1, $5}')
		done
	done

	return 1
}


#
# Reconfigure a running VM.  Currently, all we support is
# memory ballooning.
#
reconfigure()
{
	if [ -n "$OCF_RESKEY_memory" ]; then
		echo "xm balloon $OCF_RESKEY_name $OCF_RESKEY_memory"
		xm balloon $OCF_RESKEY_name $OCF_RESKEY_memory
		return $?
	fi
	return 0
}


#
# Simple status check: Find the VM in the list of running
# VMs
#
do_status()
{
	xm list $OCF_RESKEY_name &> /dev/null
	if [ $? -eq 0 ]; then
		return 0
	fi
	xm list migrating-$OCF_RESKEY_name &> /dev/null
	return $?
}


verify_all()
{
	declare errors=0

	if [ -n "$OCF_RESKEY_bootloader" ] && \
	   ! [ -x "$OCF_RESKEY_bootloader" ]; then
		echo "$OCF_RESKEY_bootloader is not executable"
		((errors++))
	fi
}


migrate()
{
	declare target=$1
	declare errstr rv migrate_opt

	if [ "$OCF_RESKEY_migrate" = "live" ]; then
		migrate_opt="-l"
	fi

	# Patch from Marcelo Azevedo to migrate over private
	# LANs instead of public LANs
        if [ -n $OCF_RESKEY_migration_mapping ] ; then
                target=${OCF_RESKEY_migration_mapping#*$target:} target=${target%%,*}
        fi

	err=$(xm migrate $OCF_RESKEY_name $target 2>&1 | head -1; exit ${PIPESTATUS[0]})
	rv=$?

	if [ $rv -ne 0 ]; then
		if [ "$err" != "${err/does not exist/}" ]; then
			ocf_log warn "Trying to migrate '$OCF_RESKEY_name' - domain does not exist"
			return $OCF_NOT_RUNNING
		fi
		if [ "$err" != "${err/Connection refused/}" ]; then
			ocf_log warn "Trying to migrate '$OCF_RESKEY_name' - connect refused"
			return $OCF_ERR_CONFIGURED
		fi
	fi

	return $?
}

#
# A Resource group is abstract, but the OCF RA API doesn't allow for abstract
# resources, so here it is.
#

case $1 in
	start)
		do_start
		exit $?
		;;
	stop)
		do_stop shutdown destroy
		exit $?
		;;
	kill)
		stop destroy
		exit $?
		;;
	recover|restart)
		exit 0
		;;
	status|monitor)
		do_status
		exit $?
		;;
	migrate)
		migrate $2 # Send VM to this node
		exit $?
		;;
	reload)
		exit 0
		;;
	reconfig)
		echo "$0 RECONFIGURING $OCF_RESKEY_memory"
		reconfigure
		exit $?
		;;
	meta-data)
		meta_data
		exit 0
		;;
	validate-all)
		verify_all
		exit $?
		;;
	*)
		echo "usage: $0 {start|stop|restart|status|reload|reconfig|meta-data|validate-all}"
		exit 1
		;;
esac
