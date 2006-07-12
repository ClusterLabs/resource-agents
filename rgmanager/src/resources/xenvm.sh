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

#
# Xen para VM start/stop script.
#

meta_data()
{
    cat <<EOT
<?xml version="1.0"?>
<resource-agent version="rgmanager 2.0" name="xenvm">
    <version>1.0</version>

    <longdesc lang="en">
	Defines a Xen Para-Virtual Machine
    </longdesc>
    <shortdesc lang="en">
        Defines a Xen domain.
    </shortdesc>

    <parameters>
        <parameter name="name" primary="1">
            <longdesc lang="en">
                This is the name of the Xen domain.
            </longdesc>
            <shortdesc lang="en">
                Name
            </shortdesc>
            <content type="string"/>
        </parameter>
    
        <parameter name="domain">
            <longdesc lang="en">
                Fail over domains define lists of cluster members
                to try in the event that the host of the Xen domain
		fails.
            </longdesc>
            <shortdesc lang="en">
                Cluster Fail Over Domain
            </shortdesc>
            <content type="string"/>
        </parameter>

        <parameter name="autostart">
            <longdesc lang="en">
	    	If set to yes, this resource group will automatically be started
		after the cluster forms a quorum.  If set to no, this resource
		group will start in the 'disabled' state after the cluster forms
		a quorum.
            </longdesc>
            <shortdesc lang="en">
	    	Automatic start after quorum formation
            </shortdesc>
            <content type="boolean"/>
        </parameter>

        <parameter name="recovery" reconfig="1">
            <longdesc lang="en">
	        This currently has three possible options: "restart" tries
		to restart failed parts of this resource group locally before
		attempting to relocate (default); "relocate" does not bother
		trying to restart the service locally; "disable" disables
		the resource group if any component fails.  Note that
		any resource with a valid "recover" operation which can be
		recovered without a restart will be.
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

	<parameter name="bootloader" required="1">
	    <longdesc lang="en">
		Root disk for the Xen VM (as presented to the VM)
	    </longdesc>
	    <shortdesc lang="en">
		Boot loader that can start the Xen VM from physical image
	    </shortdesc>
            <content type="string"/>
        </parameter>

	<parameter name="rootdisk_physical" unique="1">
	    <longdesc lang="en">
		root disk for the Xen VM.  (physical, on the host)
	    </longdesc>
	    <shortdesc lang="en">
		root disk for the Xen VM.  (physical, on the host)
	    </shortdesc>
            <content type="string"/>
        </parameter>
        
	<parameter name="rootdisk_virtual">
	    <longdesc lang="en">
		rootdisk for the Xen VM.  (as presented to the VM)
	    </longdesc>
	    <shortdesc lang="en">
		rootdisk for the Xen VM.  (as presented to the VM)
	    </shortdesc>
            <content type="string"/>
        </parameter>


	<parameter name="swapdisk_physical" unique="1">
	    <longdesc lang="en">
		Swap disk for the Xen VM.  (physical, on the host)
	    </longdesc>
	    <shortdesc lang="en">
		Swap disk for the Xen VM.  (physical, on the host)
	    </shortdesc>
            <content type="string"/>
        </parameter>
        
	<parameter name="swapdisk_virtual">
	    <longdesc lang="en">
		Swap disk for the Xen VM.  (as presented to the VM)
	    </longdesc>
	    <shortdesc lang="en">
		Swap disk for the Xen VM.  (as presented to the VM)
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


    </parameters>

    <actions>
        <action name="start" timeout="20"/>
        <action name="stop" timeout="240"/>
	
	<!-- No-ops.  Groups are abstract resource types.  -->
        <action name="status" timeout="10" interval="30m"/>
        <action name="monitor" timeout="10" interval="30m"/>

	<!-- reconfigure - reconfigure with new OCF parameters.
	     NOT OCF COMPATIBLE AT ALL -->
	<action name="reconfig" timeout="10"/>

	<!-- Suspend: if available, suspend this resource instead of
	     doing a full stop. -->
	<!-- <action name="suspend" timeout="10m"/> -->

        <action name="meta-data" timeout="5"/>
        <action name="verify-all" timeout="5"/>

    </actions>
    
    <special tag="rgmanager">
        <attributes maxinstances="1"/>
    </special>
</resource-agent>
EOT
}


#
# Find a list of possible IP addresses to try.
#
xen_host_ips()
{
	declare xen_ips=$(ip -f inet -o addr list | grep 'xen-br[0-9]\+[^:]' | awk '{print $4}')
	declare tmp1=""
	declare i

	for i in $xen_ips; do
                i=${i/\/*/}
		if [ -z "$tmp1" ]; then
			tmp1="$i"
		else
			tmp1="$i,$tmp1"
		fi
	done

	echo $tmp1
}


build_xen_cmdline()
{
	#
	# Virtual domains should never restart themselves when 
	# controlled externally; the external monitoring app
	# should.
	#
	declare cmdline="restart=\"never\""
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
		*)
			cmdline="$cmdline $varp=\"$val\""
			;;
		esac
	done

	echo $cmdline
}


#
# Start a Xen para-virtual machine given the parameters from
# the environment.
#
start()
{
	# Use /dev/null for the configuration file, if xmdefconfig
	# doesn't exist...
	#
	declare cmdline

	if [ -f "/etc/xen/xmdefconfig" ]; then
		cmdline="`build_xen_cmdline`"
	else
		cmdline="`build_xen_cmdline` /dev/null"
	fi

	echo $cmdline

	eval xm create $cmdline
	return $?
}


#
# Stop a Xen VM.  Try to shut it down.  Wait a bit, and if it
# doesn't shut down, destroy it.
#
stop()
{
	declare -i timeout=120
	declare -i ret=1
	declare st

	for op in $*; do
		echo xm $op $OCF_RESKEY_name ...
		xm $op $OCF_RESKEY_name

		timeout=120
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
# Reconfigure a running Xen VM.  Currently, all we support is
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
# Simple status check: Find the Xen VM in the list of running
# VMs
#
status()
{
	xm list $OCF_RESKEY_name &> /dev/null
	return $?
}


verify_all()
{
	declare errors=0
	declare tmp1, tmp2

	if [ -z "$OCF_RESKEY_kernel" ]; then
		echo "Required parameter OCF_RESKEY_kernel is not present"
		((errors++))
	elif ! [ -f "$OCF_RESKEY_kernel" ]; then
		echo "$OCF_RESKEY_kernel (OCF_RESKEY_kernel) is not valid"
		((errors++))
	fi

	tmp1=`echo $OCF_RESKEY_swapdisk | cut -f1 -d,`
	tmp2=`echo $OCF_RESKEY_swapdisk | cut -f2 -d,`
		
	if [ -z "$tmp2" ]; then
		echo "Swapdisk option malformed"
		((errors++))
	fi

	if ! [ -b "$tmp1" ]; then
		echo "Specified swapdisk device $tmp1 is not a block device"
		((errors++))
	fi

	if [ -z "$OCF_RESKEY_rootdisk" ]; then
		echo "Required parameter OCF_RESKEY_rootdisk is not present"
		((errors++))
	else
		tmp1=`echo $OCF_RESKEY_rootdisk | cut -f1 -d,`
		tmp2=`echo $OCF_RESKEY_rootdisk | cut -f2 -d,`
		
		if [ -z "$tmp2" ]; then
			echo "Rootdisk option malformed"
			((errors++))
		fi

		if ! [ -b "$tmp1" ]; then
			echo "Specified rootdisk device $tmp1 is not a block device"
			((errors++))
		fi
	fi
}

#
# A Resource group is abstract, but the OCF RA API doesn't allow for abstract
# resources, so here it is.
#
case $1 in
	start)
		start
		exit $?
		;;
	stop)
		stop shutdown destroy
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
		status
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
	verify-all)
		verify_all
		exit $?
		;;
	*)
		echo "usage: $0 {start|stop|restart|status|reload|reconfig|meta-data|verify-all}"
		exit 1
		;;
esac
