#!/bin/bash
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

#
# Use clustat to figure out if the specified node is a member
# of the cluster.  Returns 2 if not found, 1 if not a member, and
# 0 if the node is happily running.
# Tested on RHEL4 and RHEL5; requires RHCS 5.1 or 4.5 to operate
# properly in all cases.
#
is_node_member_clustat()
{
	declare line=$(clustat -xm $1 | grep "name=\"$1\"")
	declare tmp

	# Done if there's no node in the list with that name: not a 
	# cluster member, and not in the configuration.
	[ -n "$line" ] || return 2

	# Clear out xml tag seps.
	line=${line/*</}
	line=${line/\/>/}

	# Make vars out of XML attributes.
	for tmp in $line; do
		eval declare __$tmp
	done

	# Flip the value.  clustat reports 1 for member, 0 for not;
	# Exactly the opposite of what a shell script expects.
	((__state = !__state))
	return $__state
}


#
# Print the local node name to stdout
# Returns 0 if could be found, 1 if not
# Tested on RHEL4 (magma) and RHEL5 (cman)
#
local_node_name()
{
	declare node state line

	if which magma_tool &> /dev/null; then
		# Use magma_tool, if available.
		line=$(magma_tool localname | grep "^Local")

		if [ -n "$line" ]; then
			echo ${line/* = /}
			return 0
		fi
	fi

	if ! which cman_tool &> /dev/null; then
		# No cman tool? :(
		return 2
	fi

	# Use cman_tool

	line=$(cman_tool status | grep -i "Node name: $1")
	[ -n "$line" ] || return 1
	echo ${line/*name: /}
	return 0
}

