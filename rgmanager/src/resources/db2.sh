#!/bin/bash
#
# Copyright (c) 2011 Holger Teutsch <holger.teutsch@web.de>
# Copyright (c) 2014 David Vossel <davidvossel@gmail.com>
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

# NOTE:
#
# This agent is a wrapper around the heartbeat/db2 agent which limits the heartbeat
# db2 agent to Standard role support.  This allows cluster managers such as rgmanager
# which do not have multi-state resource support to manage db2 instances with
# a limited feature set.
#

export LC_ALL=C
export LANG=C
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
. $(dirname $0)/ocf-shellfuncs

meta_data() {
cat <<END
<?xml version="1.0"?>
<!DOCTYPE resource-agent SYSTEM "ra-api-1.dtd">
<resource-agent name="db2.sh">
<version>1.0</version>
<longdesc lang="en">
Resource Agent that manages an IBM DB2 LUW databases in Standard role. Multiple partitions are supported.

When partitions are in use, each partition must be configured as a separate primitive resource.

</longdesc>
<shortdesc lang="en">Resource Agent that manages an IBM DB2 LUW databases in Standard role with multiple partition support.</shortdesc>

<parameters>
<parameter name="instance" unique="1" required="1">
<longdesc lang="en">
The instance of the database(s).
</longdesc>
<shortdesc lang="en">instance</shortdesc>
<content type="string" default="" />
</parameter>
<parameter name="dblist" unique="0" required="0">
<longdesc lang="en">
List of databases to be managed, e.g "db1 db2".
Defaults to all databases in the instance.
</longdesc>
<shortdesc lang="en">List of databases to be managed</shortdesc>
<content type="string"/>
</parameter>
<parameter name="dbpartitionnum" unique="0" required="0">
<longdesc lang="en">
The number of the partion (DBPARTITIONNUM) to be managed.
</longdesc>
<shortdesc lang="en">database partition number (DBPARTITIONNUM)</shortdesc>
<content type="string" default="0" />
</parameter>
</parameters>

<actions>
<action name="start" timeout="120"/>
<action name="stop" timeout="120"/>
<action name="monitor" depth="0" timeout="60" interval="20"/>
<action name="monitor" depth="0" timeout="60" role="Master" interval="22"/>
<action name="validate-all" timeout="5"/>
<action name="meta-data" timeout="5"/>
</actions>
</resource-agent>
END
}

heartbeat_db2_wrapper()
{
	# default heartbeat agent ocf root.
	export OCF_ROOT=/usr/lib/ocf
	heartbeat_db2="${OCF_ROOT}/resource.d/heartbeat/db2"

	if ! [ -a $heartbeat_db2 ]; then
		echo "heartbeat db2 agent not found at '${heartbeat_db2}'"
		exit $OCF_ERR_INSTALLED 
	fi

	$heartbeat_db2 $1
}

case $1 in
	meta-data)
		meta_data
		exit 0
		;;
	validate-all)
		heartbeat_db2_wrapper $1	
		exit $?
		;;
	start)
		heartbeat_db2_wrapper $1	
		exit $?
		;;
	stop)
		heartbeat_db2_wrapper $1	
		exit $?
		;;
	status|monitor)
		heartbeat_db2_wrapper "monitor"
		exit $?
		;;
	restart)
		heartbeat_db2_wrapper "stop"
		rc=$?
		if [ $rc -ne 0 ]; then
			exit $rc
		fi 
		heartbeat_db2_wrapper "start"
		exit $?
		;;
	*)
		echo "Usage: db2.sh {start|stop|monitor|validate-all|meta-data}"
		exit $OCF_ERR_UNIMPLEMENTED
		;;
esac
