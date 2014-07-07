#!/bin/bash
#
#  Copyright Red Hat Inc., 2014
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

#
# Bind mount script - mounts parent file system -o bind in another
# location
#

LC_ALL=C
LANG=C
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export LC_ALL LANG PATH

. $(dirname $0)/ocf-shellfuncs
. $(dirname $0)/utils/fs-lib.sh

export IS_BIND_MOUNT=1
export OCF_RESKEY_use_findmnt=0
export OCF_RESKEY_options="bind"
export OCF_RESKEY_device="$OCF_RESKEY_source"
rv=0

do_metadata()
{
	cat <<EOT
<?xml version="1.0" ?>
<resource-agent name="bind-mount" version="rgmanager 2.0">
	<version>1.0</version>

	<longdesc lang="en">
		Defines a bind mount.
	</longdesc>

	<shortdesc lang="en">
		Defines a bind mount.
	</shortdesc>

	<parameters>

		<parameter name="name" primary="1" unique="1">
			<longdesc lang="en">
			Symbolic name for this bind mount.
			</longdesc>
			<shortdesc lang="en">
			Bind Mount Name
			</shortdesc>
		<content type="string"/>
		</parameter>

		<parameter name="mountpoint" unique="1" required="1">
			<longdesc lang="en">
			Target of this bind mount
			</longdesc>
			<shortdesc lang="en">
			Target mountpoint
			</shortdesc>
		<content type="string"/>
		</parameter>

		<parameter name="source" required="1">
			<longdesc lang="en">
			Source of the bind mount
			</longdesc>
			<shortdesc lang="en">
			Source of the bind mount
			</shortdesc>
		<content type="string"/>
		</parameter>

		<parameter name="force_unmount">
			<longdesc lang="en">
				If set, the cluster will kill all processes using 
				this file system when the resource group is 
				stopped.  Otherwise, the unmount will fail, and
				the resource group will be restarted.
			</longdesc>
			<shortdesc lang="en">
				Force Unmount
			</shortdesc>
		<content type="boolean"/>
		</parameter>
	</parameters>

	<actions>
		<action name="start" timeout="5"/>
		<action name="stop" timeout="5"/>
		<action name="recover" timeout="5"/>

		<action name="status" timeout="5" interval="1h"/>
		<action name="monitor" timeout="5" interval="1h"/>

		<action name="meta-data" timeout="5"/>
		<action name="verify-all" timeout="30"/>
	</actions>

	<special tag="rgmanager">
		<child type="nfsexport" forbid="1"/>
		<child type="nfsclient"/>
	</special>

</resource-agent>
EOT
}

verify_source()
{
	if [ -z "$OCF_RESKEY_source" ]; then
		ocf_log err "No source specified."
		return $OCF_ERR_ARGS
	fi

	[ -d "$OCF_RESKEY_source" ] && return 0

	ocf_log err "$OCF_RESKEY_source is not a directory"
	
	return $OCF_ERR_ARGS
}

verify_mountpoint()
{
	if [ -z "$OCF_RESKEY_mountpoint" ]; then
		ocf_log err "No target path specified."
		return $OCF_ERR_ARGS
	fi

	[ -d "$OCF_RESKEY_mountpoint" ] && return 0

	mkdir -p $OCF_RESKEY_mountpoint && return 0

	ocf_log err "$OCF_RESKEY_mountpoint is not a directory and could not be created"
	
	return $OCF_ERR_ARGS
}

do_validate()
{
	declare -i ret=0

	verify_source || ret=$OCF_ERR_ARGS
	verify_mountpoint || ret=$OCF_ERR_ARGS

	return $ret
}

do_pre_mount()
{
	do_validate || exit $OCF_ERR_ARGS
}

main $*
