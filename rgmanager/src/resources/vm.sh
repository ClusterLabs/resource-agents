#!/bin/bash

PATH=/bin:/sbin:/usr/bin:/usr/sbin

export PATH

. $(dirname $0)/ocf-shellfuncs || exit 1

#
# Virtual Machine start/stop script (requires the virsh command)
#

# Indeterminate state: xend/libvirtd is down.
export OCF_APP_ERR_INDETERMINATE=150

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

       <parameter name="migration_mapping" reconfig="1">
           <longdesc lang="en">
               Mapping of the hostname of a target cluster member to a different hostname
           </longdesc>
           <shortdesc lang="en">
               memberhost:targethost,memberhost:targethost ..
           </shortdesc>
            <content type="string"/>
        </parameter>

	<parameter name="use_virsh">
	    <longdesc lang="en">
	    	Use virsh instead of XM
	    </longdesc>
	    <shortdesc lang="en">
	    	If set to 1, vm.sh will use the virsh command to manage
		virtual machines instead of xm.  This is required when
		using non-Xen virtual machines (e.g. qemu / KVM).
	    </shortdesc>
            <content type="integer" default="1"/>
        </parameter>

	<parameter name="xmlfile">
	    <longdesc lang="en">
	    	Use xmlfile to create the vm (libvirt)
	    </longdesc>
	    <shortdesc lang="en">
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

	<parameter name="snapshot">
	    <longdesc lang="en">
	    	Path to the snapshot directory where the virtual machine
		image will be stored.
	    </longdesc>
	    <shortdesc lang="en">
	    	Path to the snapshot directory where the virtual machine
		image will be stored.
	    </shortdesc>
            <content type="string" default=""/>
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
            <content type="string" default="0"/>
        </parameter>

	<parameter name="hypervisor">
            <shortdesc lang="en">
		Hypervisor
            </shortdesc >
            <longdesc lang="en">
		Specify hypervisor tricks to use.  Default = auto.
		Other supported options are xen and qemu.
            </longdesc>
	    <content type="string" default="auto" />
	</parameter>

	<parameter name="hypervisor_uri">
            <shortdesc lang="en">
		Hypervisor URI
            </shortdesc >
            <longdesc lang="en">
		Hypervisor URI.  Generally, this is keyed off of the
		hypervisor and does not need to be set.
            </longdesc>
	    <content type="string" default="auto" />
	</parameter>

	<parameter name="migration_uri">
            <shortdesc lang="en">
		Migration URI
            </shortdesc >
            <longdesc lang="en">
		Migration URI.  Generally, this is keyed off of the
		hypervisor and does not need to be set.
            </longdesc>
	    <content type="string" default="auto" />
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


build_virsh_cmdline()
{
	declare cmdline=""
	declare operation=$1

	if [ -n "$OCF_RESKEY_hypervisor_uri" ]; then
		cmdline="$cmdline -c $OCF_RESKEY_hypervisor_uri"
	fi

	cmdline="$cmdline $operation $OCF_RESKEY_name"
		
	echo $cmdline
}


# this is only used on startup
build_xm_cmdline()
{
	declare operation=$1
	#
	# Virtual domains should never restart themselves when 
	# controlled externally; the external monitoring app
	# should.
	#
	declare cmdline="on_shutdown=\"destroy\" on_reboot=\"destroy\" on_crash=\"destroy\""

	if [ -n "$OCF_RESKEY_path" ]; then
		operation="$operation --path=\"$OCF_RESKEY_path\""
	fi

	if [ -n "$OCF_RESKEY_name" ]; then
		cmdline="$operation $OCF_RESKEY_name $cmdline"
	fi

	echo $cmdline
}


do_xm_start()
{
	# Use /dev/null for the configuration file, if xmdefconfig
	# doesn't exist...
	#
	declare cmdline

	echo -n "Virtual machine $OCF_RESKEY_name is "
	do_status && return 0

	cmdline="`build_xm_cmdline create`"

	ocf_log debug "xm $cmdline"

	eval xm $cmdline
	return $?
}


#
# Start a virtual machine given the parameters from
# the environment.
#
do_virsh_start()
{
	declare cmdline
	declare snapshotimage

	echo -n "Virtual machine $OCF_RESKEY_name is "
	do_status && return 0

	snapshotimage="$OCF_RESKEY_snapshot/$OCF_RESKEY_name"

        if [ -n "$OCF_RESKEY_snapshot" -a -f "$snapshotimage" ]; then
		eval virsh restore $snapshotimage
		if [ $? -eq 0 ]; then
			rm -f $snapshotimage
			return 0
		fi
		return 1
	fi

	if [ -n "$OCF_RESKEY_xmlfile" -a -f "$OCF_RESKEY_xmlfile" ]; then
		# TODO: try to use build_virsh_cmdline for the hypervisor_uri
		cmdline="virsh create $OCF_RESKEY_xmlfile"
	else
		cmdline="virsh $(build_virsh_cmdline start)"
	fi

	ocf_log debug "$cmdline"

	$cmdline
	return $?
}


do_xm_stop()
{
	declare -i timeout=60
	declare -i ret=1
	declare st

	for op in $*; do
		echo "CMD: xm $op $OCF_RESKEY_name"
		xm $op $OCF_RESKEY_name

		timeout=60
		while [ $timeout -gt 0 ]; do
			sleep 5
			((timeout -= 5))
			do_status&>/dev/null || return 0
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
# Stop a VM.  Try to shut it down.  Wait a bit, and if it
# doesn't shut down, destroy it.
#
do_virsh_stop()
{
	declare -i timeout=60
	declare -i ret=1
	declare state

	state=$(do_status)
	[ $? -eq 0 ] || return 0

	if [ -n "$OCF_RESKEY_snapshot" ]; then
		virsh save $OCF_RESKEY_name "$OCF_RESKEY_snapshot/$OCF_RESKEY_name"
	fi

	for op in $*; do
		echo virsh $op $OCF_RESKEY_name ...
		virsh $op $OCF_RESKEY_name

		timeout=60
		while [ $timeout -gt 0 ]; do
			sleep 5
			((timeout -= 5))
			state=$(do_status)
			[ $? -eq 0 ] || return 0

			if [ "$state" = "paused" ]; then
				virsh destroy $OCF_RESKEY_name
			fi
		done
	done

	return 1
}


do_start()
{
	if [ "$OCF_RESKEY_use_virsh" = "1" ]; then
		do_virsh_start $*
		return $?
	fi

	do_xm_start $*
	return $?
}


do_stop()
{
	declare domstate rv

	domstate=$(do_status)
	rv=$?
	ocf_log debug "Virtual machine $OCF_RESKEY_name is $domstate"
	if [ $rv -eq $OCF_APP_ERR_INDETERMINATE ]; then
		ocf_log crit "xend/libvirtd is dead; cannot stop $OCF_RESKEY_name"
		return 1
	fi

	if [ "$OCF_RESKEY_use_virsh" = "1" ]; then
		do_virsh_stop $*
		return $?
	fi

	do_xm_stop $*
	return $?
}


#
# Reconfigure a running VM.
#
reconfigure()
{
	return 0
}


xm_status()
{
	service xend status &> /dev/null
	if [ $? -ne 0 ]; then 
		# if xend died
		echo indeterminate
		return $OCF_APP_ERR_INDETERMINATE
	fi

	xm list $OCF_RESKEY_name &> /dev/null
	if [ $? -eq 0 ]; then
		echo "running"
		return 0
	fi
	xm list migrating-$OCF_RESKEY_name &> /dev/null
	if [ $? -eq 0 ]; then
		echo "running"
		return 0
	fi
	echo "not running"
	return 1
}


virsh_status()
{
	declare state pid

	if [ "$OCF_RESKEY_hypervisor" = "xen" ]; then
		service xend status &> /dev/null
		if [ $? -ne 0 ]; then 
			echo indeterminate
			return $OCF_APP_ERR_INDETERMINATE
		fi
	fi

	#
	# libvirtd is required when using virsh even though
	# not specifically when also using Xen.  This is because
	# libvirtd is required for migration.
	#
	pid=$(pidof libvirtd)
	if [ -z "$pid" ]; then 
		echo indeterminate
		return $OCF_APP_ERR_INDETERMINATE
	fi

	state=$(virsh domstate $OCF_RESKEY_name)

	echo $state

	if [ "$state" = "running" ] || [ "$state" = "paused" ] || [ "$state" = "no state" ] || 
	   [ "$state" = "idle" ]; then
		return 0
	fi

	return 1
}


#
# Simple status check: Find the VM in the list of running
# VMs
#
do_status()
{
	if [ "$OCF_RESKEY_use_virsh" = "1" ]; then
		virsh_status
		return $?
	fi

	xm_status
	return $?
}


validate_all()
{
	[ "$(id -u)" = "0" ] || return 1

	#
	# If someone selects a hypervisor, honor it.
	# Otherwise, ask virsh what the hypervisor is.
	#
	if [ -z "$OCF_RESKEY_hypervisor" ] ||
	   [ "$OCF_RESKEY_hypervisor" = "auto" ]; then
		export OCF_RESKEY_hypervisor="`virsh version | grep \"Running hypervisor:\" | awk '{print $3}' | tr A-Z a-z`"
		if [ -z "$OCF_RESKEY_hypervisor" ]; then
			ocf_log err "Could not determine Hypervisor"
			return $OCF_ERR_ARGS
		fi
		echo Hypervisor: $OCF_RESKEY_hypervisor 
	fi

	#
	# Xen hypervisor only for when use_virsh = 0.
	#
	if [ "$OCF_RESKEY_use_virsh" = "0" ]; then
		if [ "$OCF_RESKEY_hypervisor" != "xen" ]; then
			ocf_log err "Cannot use $OCF_RESKEY_hypervisor hypervisor without using virsh"
			return $OCF_ERR_ARGS
		fi
	else
	
		#
		# If no path is set, use virsh.  Otherwise, use xm.
		# xm only works with Xen.
		#
		if [ -z "$OCF_RESKEY_path" ] ||
		   [ "$OCF_RESKEY_path" = "/etc/xen" ]; then
			echo "Management tool: virsh"
			export OCF_RESKEY_use_virsh=1
		else
			echo "Management tool: xm"
			export OCF_RESKEY_use_virsh=0
		fi
	fi

	#
	# Set the hypervisor URI
	#
	if [ -z "$OCF_RESKEY_hypervisor_uri" -o "$OCF_RESKEY_hypervisor_uri" = "auto" ] &&
	   [ "$OCF_RESKEY_use_virsh" = "1" ]; then

		# Virsh makes it easier to do this.  Really.
		if [ "$OCF_RESKEY_hypervisor" = "qemu" ]; then
			OCF_RESKEY_hypervisor_uri="qemu:///system"
		fi

		# I just need to believe in it more.
		if [ "$OCF_RESKEY_hypervisor" = "xen" ]; then
			OCF_RESKEY_hypervisor_uri="xen:///"
		fi

		echo Hypervisor URI: $OCF_RESKEY_hypervisor_uri
	fi

	#
	# Set the migration URI
	#
	if [ -z "$OCF_RESKEY_migration_uri" -o "$OCF_RESKEY_migration_uri" = "auto" ] &&
	   [ "$OCF_RESKEY_use_virsh" = "1" ]; then

		# Virsh makes it easier to do this.  Really.
		if [ "$OCF_RESKEY_hypervisor" = "qemu" ]; then
			export OCF_RESKEY_migration_uri="qemu+ssh://%s/system"
		fi

		# I just need to believe in it more.
		if [ "$OCF_RESKEY_hypervisor" = "xen" ]; then
			export OCF_RESKEY_migration_uri="xenmigr://%s/"
		fi

		[ -n "$OCF_RESKEY_migration_uri" ] && echo Migration URI format: $(printf $OCF_RESKEY_migration_uri target_host)
	fi

	if [ -z "$OCF_RESKEY_name" ]; then
		echo No domain name specified
		return $OCF_ERR_ARGS
	fi

	#virsh list --all | awk '{print $2}' | grep -q "^$OCF_RESKEY_name\$"
	return $?
}


virsh_migrate()
{
	declare $target=$1
	declare rv=1

	#
	# Xen and qemu have different migration mechanisms
	#
	if [ "$OCF_RESKEY_hypervisor" = "xen" ]; then 
		cmd="virsh migrate $migrate_opt $OCF_RESKEY_name $OCF_RESKEY_hypervisor_uri $(printf $OCF_RESKEY_migration_uri $target)"
		ocf_log debug "$cmd"
		
		err=$($cmd 2>&1 | head -1; exit ${PIPESTATUS[0]})
		rv=$?
	elif [ "$OCF_RESKEY_hypervisor" = "qemu" ]; then
		cmd="virsh migrate $migrate_opt $OCF_RESKEY_name $(printf $OCF_RESKEY_migration_uri $target)"
		ocf_log debug "$cmd"
		
		err=$($cmd 2>&1 | head -1; exit ${PIPESTATUS[0]})
		rv=$?
	fi

	if [ $rv -ne 0 ]; then
		ocf_log err "Migrate $OCF_RESKEY_name to $target failed:"
		ocf_log err "$err"

		if [ "$err" != "${err/does not exist/}" ]; then
			return $OCF_NOT_RUNNING
		fi
		if [ "$err" != "${err/Domain not found/}" ]; then
			return $OCF_NOT_RUNNING
		fi
		if [ "$err" != "${err/Connection refused/}" ]; then
			return $OCF_ERR_CONFIGURED
		fi

		return $OCF_ERR_GENERIC
	fi

	return $rv
}


#
# XM migrate
#
xm_migrate()
{
	declare target=$1
	declare errstr rv migrate_opt cmd

	rv=1

	if [ "$OCF_RESKEY_migrate" = "live" ]; then
		migrate_opt="-l"
	fi

	# migrate() function  sets target using migration_mapping;
	# no need to do it here anymore
	cmd="xm migrate $migrate_opt $OCF_RESKEY_name $target"
	ocf_log debug "$cmd"

	err=$($cmd 2>&1 | head -1; exit ${PIPESTATUS[0]})
	rv=$?

	if [ $rv -ne 0 ]; then
		ocf_log err "Migrate $OCF_RESKEY_name to $target failed:"
		ocf_log err "$err"

		if [ "$err" != "${err/does not exist/}" ]; then
			return $OCF_NOT_RUNNING
		fi
		if [ "$err" != "${err/Connection refused/}" ]; then
			return $OCF_ERR_CONFIGURED
		fi

		return $OCF_ERR_GENERIC
	fi

	return $?
}

# 
# Virsh migrate
#
migrate()
{
	declare target=$1
	declare rv migrate_opt

	if [ "$OCF_RESKEY_migrate" = "live" ]; then
		migrate_opt="--live"
	fi

	# Patch from Marcelo Azevedo to migrate over private
	# LANs instead of public LANs
        if [ -n "$OCF_RESKEY_migration_mapping" ] ; then
                target=${OCF_RESKEY_migration_mapping#*$target:} target=${target%%,*}
        fi

	if [ "$OCF_RESKEY_use_virsh" = "1" ]; then
		virsh_migrate $target
		rv=$?
	else
		xm_migrate $target
		rv=$?
	fi

	return $rv
}

#
#
#

case $1 in
	start)
		validate_all || exit $OCF_ERR_ARGS
		do_start
		exit $?
		;;
	stop)
		validate_all || exit $OCF_ERR_ARGS
		do_stop shutdown destroy
		exit $?
		;;
	kill)
		validate_all || exit $OCF_ERR_ARGS
		do_stop destroy
		exit $?
		;;
	recover|restart)
		exit 0
		;;
	status|monitor)
		validate_all || exit $OCF_ERR_ARGS
		echo -n "Virtual machine $OCF_RESKEY_name is "
		do_status
		exit $?
		;;
	migrate)
		validate_all || exit $OCF_ERR_ARGS
		migrate $2 # Send VM to this node
		exit $?
		;;
	reload)
		exit 0
		;;
	reconfig)
		validate_all || exit $OCF_ERR_ARGS
		echo "$0 RECONFIGURING $OCF_RESKEY_memory"
		reconfigure
		exit $?
		;;
	meta-data)
		meta_data
		exit 0
		;;
	validate-all)
		validate_all
		exit $?
		;;
	*)
		echo "usage: $0 {start|stop|restart|status|reload|reconfig|meta-data|validate-all}"
		exit 1
		;;
esac
