#!/bin/bash

#
# Dummy OCF script for resource group
#
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

# Grab nfs lock tricks if available
export NFS_TRICKS=1
if [ -f "$(dirname $0)/svclib_nfslock" ]; then
	. $(dirname $0)/svclib_nfslock
	NFS_TRICKS=0
fi

meta_data()
{
    cat <<EOT
<?xml version="1.0"?>
<resource-agent version="rgmanager 2.0" name="service">
    <version>1.0</version>

    <longdesc lang="en">
        This defines a collection of resources, known as a resource
        group or cluster service.
    </longdesc>
    <shortdesc lang="en">
        Defines a service (resource group).
    </shortdesc>

    <parameters>
        <parameter name="name" unique="1" required="1" primary="1">
            <longdesc lang="en">
                This is the name of the resource group.
            </longdesc>
            <shortdesc lang="en">
                Name.
            </shortdesc>
            <content type="string"/>
        </parameter>
    
        <parameter name="domain" reconfig="1">
            <longdesc lang="en">
                Failover domains define lists of cluster members
                to try in the event that a resource group fails.
            </longdesc>
            <shortdesc lang="en">
                Failover domain.
            </shortdesc>
            <content type="string"/>
        </parameter>

        <parameter name="autostart" reconfig="1">
            <longdesc lang="en">
	    	If set to yes, this resource group will automatically be started
		after the cluster forms a quorum.  If set to no, this resource
		group will start in the 'disabled' state after the cluster forms
		a quorum.
            </longdesc>
            <shortdesc lang="en">
	    	Automatic start after quorum formation
            </shortdesc>
            <content type="boolean" default="1"/>
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
	        Exclusive service.
            </shortdesc>
            <content type="boolean" default="0"/>
        </parameter>

	<parameter name="nfslock">
	    <longdesc lang="en">
	    	Enable NFS lock workarounds.  When used with a compatible
		HA-callout program like clunfslock, this could be used
		to provide NFS lock failover, but at significant cost to
		other services on the machine.  This requires a compatible
		version of nfs-utils and manual configuration of rpc.statd;
		see 'man rpc.statd' to see if your version supports
		the -H parameter.
	    </longdesc>
	    <shortdesc lang="en">
	        Enable NFS lock workarounds.
	    </shortdesc>
	    <content type="boolean" default="0"/>
	</parameter>

	<parameter name="nfs_client_cache">
            <longdesc lang="en">
	   	On systems with large numbers of exports, a performance
		problem in the exportfs command can cause inordinately long
		status check times for services with lots of mounted
		NFS clients.  This occurs because exportfs does DNS queries
		on all clients in the export list.

		Setting this option to '1' will enable caching of the export
		list returned from the exportfs command on a per-service
		basis.  The cache will last for 30 seconds before expiring
		instead of being generated each time an nfsclient resource
		is called.
            </longdesc>
            <shortdesc lang="en">
	    	Enable exportfs list caching (performance).
            </shortdesc>
	    <content type="integer" default="0"/>
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
	    	Failure recovery policy (restart, relocate, or disable).
            </shortdesc>
            <content type="string" default="restart"/>
        </parameter>

        <parameter name="depend">
            <longdesc lang="en">
		Service dependency; will not start without the specified
		service running.
            </longdesc>
            <shortdesc lang="en">
		Top-level service this depends on, in service:name format.
            </shortdesc>
            <content type="string"/>
        </parameter>

        <parameter name="depend_mode">
            <longdesc lang="en">
		Service dependency mode.
		hard - This service is stopped/started if its dependency
		       is stopped/started
		soft - This service only depends on the other service for
		       initial startip.  If the other service stops, this
		       service is not stopped.
            </longdesc>
            <shortdesc lang="en">
	    	Service dependency mode (soft or hard).
            </shortdesc>
            <content type="string" default="hard"/>
        </parameter>

        <parameter name="max_restarts">
            <longdesc lang="en">
	    	Maximum restarts for this service.
            </longdesc>
            <shortdesc lang="en">
	    	Maximum restarts for this service.
            </shortdesc>
            <content type="string" default="0"/>
        </parameter>

        <parameter name="restart_expire_time">
            <longdesc lang="en">
	    	Restart expiration time.  A restart is forgotten
		after this time.  When combined with the max_restarts
		option, this lets administrators specify a threshold
		for when to fail over services.  If max_restarts
		is exceeded in this given expiration time, the service
		is relocated instead of restarted again.
            </longdesc>
            <shortdesc lang="en">
	    	Restart expiration time; amount of time before a restart
		is forgotten.
            </shortdesc>
            <content type="string" default="0"/>
        </parameter>

	<parameter name="priority">
	    <longdesc lang="en">
		Priority for the service.  In a failover scenario, this
		indicates the ordering of the service (1 is processed
		first, 2 is processed second, etc.).  This overrides the
		order presented in cluster.conf.  This option only has
		an effect if central processing within rgmanager is turned
		on.
	    </longdesc>
	    <shortdesc lang="en">
		Service priority.
	    </shortdesc>
	    <content type="integer" default="0"/>
	</parameter>

    </parameters>

    <actions>
        <action name="start" timeout="5"/>
        <action name="stop" timeout="5"/>
	
	<!-- No-ops.  Groups are abstract resource types. 
        <action name="status" timeout="5" interval="1h"/>
        <action name="monitor" timeout="5" interval="1h"/>
 -->

        <action name="reconfig" timeout="5"/>
        <action name="recover" timeout="5"/>
        <action name="reload" timeout="5"/>
        <action name="meta-data" timeout="5"/>
        <action name="validate-all" timeout="5"/>
    </actions>
    
    <special tag="rgmanager">
        <attributes maxinstances="1"/>
        <child type="lvm" start="1" stop="9"/>
        <child type="fs" start="2" stop="8"/>
        <child type="clusterfs" start="3" stop="7"/>
        <child type="netfs" start="4" stop="6"/>
	<child type="nfsexport" start="5" stop="5"/>

	<child type="nfsclient" start="6" stop="4"/>

        <child type="ip" start="7" stop="2"/>
        <child type="smb" start="8" stop="3"/>
        <child type="script" start="9" stop="1"/>
    </special>
</resource-agent>
EOT
}


#
# A Resource group is abstract, but the OCF RA API doesn't allow for abstract
# resources, so here it is.
#
case $1 in
	start)
		#
		# XXX If this is set, we kill lockd.  If there is no
		# child IP address, then clients will NOT get the reclaim
		# notification.
		#
		if [ $NFS_TRICKS -eq 0 ]; then
			if [ "$OCF_RESKEY_nfslock" = "yes" ] || \
	   		   [ "$OCF_RESKEY_nfslock" = "1" ]; then
				pkill -KILL -x lockd
			fi
		fi
		exit 0
		;;
	stop)
		exit 0
		;;
	recover|restart)
		exit 0
		;;
	status|monitor)
		exit 0
		;;
	reload)
		exit 0
		;;
	meta-data)
		meta_data
		exit 0
		;;
	validate-all)
		exit 0
		;;
	reconfig)
		exit 0
		;;
	*)
		exit 0
		;;
esac
