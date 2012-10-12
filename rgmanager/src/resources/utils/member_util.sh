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
# Use corosync-quorumtool to figure out if the specified node is a member
# of the cluster.  Returns 1 if not a member, and
# 0 if the node is happily running.
#
# Tested on RHEL6 and F17  Note that the old version of this function utilized
# clustat, which had introspection in to the configuration.
# If a node was not found, the old version would return '2', but the only
# consumer of this function never cared about that value.
#
is_node_member_clustat()
{
	# Still having a tag while (a) online but (b) not running pacemaker 
	# (e.g. crm_node) or rgmanager not considered adequate for things like
	# the LVM agent - so we use corosync-quorumtool instead.  The function
	# name really should be changed.
	#
	# corosync 1.4.1 output looks like:
	#
	#  # corosync-quorumtool  -l
	#  Nodeid     Name
	#     1   rhel6-1
	#     2   rhel6-2
	#
	# corosync 2.0.1 output looks like:
	#  # corosync-quorumtool -l
	#
	#  Membership information
	#  ----------------------
	#      Nodeid      Votes Name
	#           1          1 rhel7-1.priv.redhat.com
	#           2          1 rhel7-2.priv.redhat.com
	#
	corosync-quorumtool -l | grep -v "^Nodeid" | grep -i " $1\$" &> /dev/null
	return $?
}


#
# Print the local node name to stdout
# Returns 0 if could be found, 1 if not
# Tested on RHEL6 (cman) and Fedora 17 (corosync/pacemaker)
#
local_node_name()
{
	local node nid localid

	if which magma_tool &> /dev/null; then
		# Use magma_tool, if available.
		line=$(magma_tool localname | grep "^Local")

		if [ -n "$line" ]; then
			echo ${line/* = /}
			return 0
		fi
	fi

	if which cman_tool &> /dev/null; then
		# Use cman_tool

		line=$(cman_tool status | grep -i "Node name: $1")
		[ -n "$line" ] || return 1
		echo ${line/*name: /}
		return 0
	fi

	if ! which crm_node &> /dev/null; then
		# no crm_node? :(
		return 2
	fi

	localid=$(crm_node -i)
	while read nid node; do
		if [ "$nid" = "$localid" ]; then
			echo $node
			return 0
		fi
	done < <(crm_node -l)

	return 1
}

