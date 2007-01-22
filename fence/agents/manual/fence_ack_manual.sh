#!/bin/bash

if [ -z "$1" ]; then
	echo "usage: $0 <nodename>"
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
