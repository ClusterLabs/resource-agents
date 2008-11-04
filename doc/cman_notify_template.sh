#!/bin/bash

# This is a simple template that can be used as reference
# for notification scripts. Note: notification scripts need
# to be executable in order for cman_notify to run them.

# Set the path for the commands you expect to execute!
# cmannotifyd does not set any for you.

PATH=/sbin:/bin:/usr/sbin:/usr/bin:/usr/local/sbin:/usr/local/bin

# define a simple wrapper to echo that will log to file only if
# debugging is enable.
my_echo() {
	[ -n "$OUT" ] && echo $@ >> $OUT
}

LOGFILE="/var/log/cluster/file.log"

# verify if you are running in debugging mode
if [ "$CMAN_NOTIFICATION_DEBUG" = "1" ]; then
	# in debuggin mode, we want to see the whole output somewhere
	OUT="$LOGFILE"
	my_echo "debugging is enabled"
fi

# parse the notification we received.
case "$CMAN_NOTIFICATION" in
	CMAN_REASON_CONFIG_UPDATE)
		# we received a configuration change
		my_echo "replace me with something to do"
	;;
	CMAN_REASON_STATECHANGE)
		# we received a status change. A node might have left or joined
		# the cluster
		my_echo "replace me with something to do"

		# STATECHANGE contains information about the quorum status of
		# the node.
		# 1 = the node is part of a quorate cluster
		# 0 = there is no quorum
		if [ "$CMAN_NOTIFICATION_QUORUM" = "1" ]; then
			my_echo "we still have quorum"
		fi
	;;
	CMAN_REASON_TRY_SHUTDOWN)
		# we received a shutdown request. It means that cman might go
		# offline very soon.
		my_echo "replace me with something to do"
	;;
	*)
		# we received an unknown notification.
		my_echo "no clue of what to do with this"
	;;
esac

exit 0
