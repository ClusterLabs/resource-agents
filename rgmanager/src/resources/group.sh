#!/bin/bash

#
# Dummy OCF script for resource group; the OCF spec doesn't support abstract
# resources. ;(
#

meta_data()
{
    cat <<EOT
<?xml version="1.0"?>
<resource-agent version="rgmanager 2.0" name="group">
    <version>1.0</version>

    <longdesc lang="en">
        This defines a collection of resources, known as a resource
        group.  A resource group might also be know as a "clustered
	service".
    </longdesc>
    <shortdesc lang="en">
        Defines a resource group.
    </shortdesc>

    <parameters>
        <parameter name="name" unique="1" required="1" primary="1">
            <longdesc lang="en">
                This is the name of the resource group.
            </longdesc>
            <shortdesc lang="en">
                Name
            </shortdesc>
            <content type="string"/>
        </parameter>
    
        <parameter name="domain">
            <longdesc lang="en">
                Fail over domains define lists of cluster members
                to try in the event that a resource group fails.
            </longdesc>
            <shortdesc lang="en">
                Fail over Domain
            </shortdesc>
            <content type="string"/>
        </parameter>
    </parameters>

    <actions>
        <action name="start" timeout="5"/>
        <action name="stop" timeout="5"/>
	
	<!-- No-ops.  Groups are abstract resource types.  -->
        <action name="status" timeout="5"/>
        <action name="monitor" timeout="5"/>

        <action name="recover" timeout="5"/>
        <action name="reload" timeout="5"/>
        <action name="meta-data" timeout="5"/>
        <action name="verify-all" timeout="5"/>
    </actions>
    
    <special tag="rgmanager">
        <attributes root="1" maxinstances="1"/>
        <child type="group" start="1" stop="5"/>
        <child type="fs" start="2" stop="4"/>
        <child type="sharedfs" start="2" stop="4"/>
        <child type="ip" start="3" stop="3"/>
        <child type="samba" start="4" stop="2"/>
        <child type="script" start="5" stop="1"/>
        <!-- child type="perlscript" start="5" stop="1"/ -->
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
	verify-all)
		exit 0
		;;
	*)
		exit 0
		;;
esac
