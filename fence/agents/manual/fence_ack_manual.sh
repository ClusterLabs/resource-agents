#!/bin/bash
#
###############################################################################
###############################################################################
##
##  COPYRIGHT (C) 2007 RED HAT, INC.
##  
##  THIS COPYRIGHTED MATERIAL IS MADE AVAILABLE TO ANYONE WISHING TO USE,
##  MODIFY, COPY, OR REDISTRIBUTE IT SUBJECT TO THE TERMS AND CONDITIONS
##  OF THE GNU GENERAL PUBLIC LICENSE V.2.
##
###############################################################################
###############################################################################
#
# Manual override after fencing has failed.
#

if [ "$1" = "-n" ]; then
	shift
fi

if [ -z "$1" ] || [ "$1" = "-h" ]; then
	echo "usage:"
        echo " 	$0 <nodename>"
        echo " 	$0 -n <nodename>"
	echo 
	echo "The -n flag exists to preserve compatibility with previous "
	echo "releases of $0, and is no longer required."
	exit 1
fi

declare answer

echo "About to override fencing for $1."
echo "Improper use of this command can cause severe file system damage."
echo
read -p "Continue [NO/absolutely]? " answer

if [ "$answer" != "absolutely" ]; then
	echo "Aborted."
	exit 1
fi

while ! [ -e /var/run/cluster/fenced_override ]; do
	sleep 1
done

echo $1>/var/run/cluster/fenced_override
echo Done
