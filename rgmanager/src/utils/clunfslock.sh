#!/bin/bash
#
# rpc.statd -H $0 to enable.  This provides the HA-callout capability
# for RHCS-managed NFS services.  Note that you must edit
# /etc/sysconfig/nfs in order to make this work; clumanager/rgmanager
# will not interfere with a running nfs statd.
#
# Arg 3 (server as known to client) does not work; it's always 127.0.0.1
# so we traverse all cluster mount points.
#

clustered_mounts()
{
	declare dev mp

	while read dev mp; do
		if [ "${dev:0:4}" != "/dev" ]; then
			continue
		fi

		# XXX Need clumanager to create this on mount
		if [ -d "$mp/.clumanager" ]; then
			echo $dev $mp
		fi
	done < <(cat /proc/mounts | awk '{print $1,$2}')
}


add-client()
{
	declare dev mp

	while read dev mp; do
		[ -d "$mp/.clumanager/statd/sm" ] || \
			mkdir -p $mp/.clumanager/statd/sm
		touch $mp/.clumanager/statd/sm/$1
	done < <(clustered_mounts)
}


del-client()
{
	while read $dev $mp; do
		[ -d "$mp/.clumanager/statd/sm" ] || \
			mkdir -p $mp/.clumanager/statd/sm
		rm -f $mp/.clumanager/statd/sm/$1
	done < <(clustered_mounts)
}

case "$1" in
	add-client)
		:
		;;
	del-client)
		:
		;;
	*)
		echo "Usage: $0 <add-client|del-client> <host> [server]"
		exit 0
esac


if [ -z "$2" ]; then
	echo "Usage: $0 <add-client|del-client> <host> [server]"
	exit 1
fi

$1 $2 $3
exit 0
