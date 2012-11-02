#!/bin/bash
#
#  Copyright LINBIT, 2008
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
# DRBD resource management using the drbdadm utility.
#

LC_ALL=C
LANG=C
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export LC_ALL LANG PATH

. $(dirname $0)/ocf-shellfuncs

drbd_verify_all()
{
    # Do we have the drbdadm utility?
    if ! which drbdadm >/dev/null 2>&1 ; then
	ocf_log error "drbdadm not installed, not found in PATH ($PATH), or not executable."
	return $OCF_ERR_INSTALLED
    fi

    # Is drbd loaded?
    if ! grep drbd /proc/modules >/dev/null 2>&1; then
	ocf_log error "drbd not found in /proc/modules. Do you need to modprobe?"
	return $OCF_ERR_INSTALLED
    fi

    # Do we have the "resource" parameter?
    if [ -n "$OCF_RESKEY_resource" ]; then

      # Can drbdadm parse the resource name?
      if ! drbdadm sh-dev $OCF_RESKEY_resource >/dev/null 2>&1; then
  	ocf_log error "DRBD resource \"$OCF_RESKEY_resource\" not found." 
  	return $OCF_ERR_CONFIGURED
      fi

      # Is the backing device a locally available block device?
      backing_dev=$(drbdadm sh-ll-dev $OCF_RESKEY_resource)
      if [ ! -b $backing_dev ]; then
  	ocf_log error "Backing device for DRBD resource \"$OCF_RESKEY_resource\" ($backing_dev) not found or not a block device."
  	return $OCF_ERR_INSTALLED
      fi

    fi

    return 0
}

drbd_status() {
    role=$(drbdadm role $OCF_RESKEY_resource)
    case $role in
	Primary/*)
	    return $OCF_SUCCESS
	    ;;
	Secondary/*)
	    return $OCF_NOT_RUNNING
	    ;;

    esac
    return $OCF_ERR_GENERIC
}

drbd_promote() {
    drbdadm primary $OCF_RESKEY_resource || return $?
}

drbd_demote() {
    drbdadm secondary $OCF_RESKEY_resource || return $?
}


if [ -z "$OCF_CHECK_LEVEL" ]; then
	OCF_CHECK_LEVEL=0
fi

# This one doesn't need to pass the verify check
case $1 in
    meta-data)
	cat `echo $0 | sed 's/^\(.*\)\.sh$/\1.metadata/'` && exit 0
	exit $OCF_ERR_GENERIC
	;;
esac

# Everything else does
drbd_verify_all || exit $?
case $1 in
    start)
	if drbd_status; then
	    ocf_log debug "DRBD resource ${OCF_RESKEY_resource} already configured"
	    exit 0
	fi
	drbd_promote 
	if [ $? -ne 0 ]; then
	    exit $OCF_ERR_GENERIC
	fi
	
	exit $?
	;;
    stop)
	if drbd_status; then
	    drbd_demote
	    if [ $? -ne 0 ]; then
		exit $OCF_ERR_GENERIC
	    fi
	else
	    ocf_log debug "DRBD resource ${OCF_RESKEY_resource} is not configured"
	fi
	exit 0
	;;
    status|monitor)
	drbd_status
	exit $?
	;;
    restart)
	$0 stop || exit $OCF_ERR_GENERIC
	$0 start || exit $OCF_ERR_GENERIC
	exit 0
	;;
    verify-all)
    	exit 0
    	;;
    *)
	echo "usage: $0 {start|stop|status|monitor|restart|meta-data|verify-all}"
	exit $OCF_ERR_GENERIC
	;;
esac
