#!/bin/sh

MYNAME=`basename $0`

do_log()
{
	declare severity=$1

	shift
	echo "<$severity> $*"
	clulog -s $severity "$*"
}

if [ -z "$1" ]; then
	do_log 4 No host specified.
	exit 1
fi

do_log 6 "Checking RHEV status on $1"

tries=3
http_code=

while [ $tries -gt 0 ]; do

	# Record start/end times so we can calculate the difference
	start_time=$(date +%s)
	http_code="$(curl -m 10 -sk https://$1/RHEVManagerWeb/HealthStatus.aspx -D - | head -1 | cut -f2 -d' ')"

	if [ "$http_code" = "200" ]; then
		exit 0
	fi

	# Reduce sleep time if the attempt took a noticeable amount
	# of time.
	end_time=$(date +%s)
	delta=$(((end_time - start_time)))
	sleep_time=$(((90 - delta)))

	((tries-=1))

	# if we're going to retry and we have a nonzero sleep time,
	# go to sleep.
	if [ $tries -gt 0 ] && [ $sleep_time -gt 0 ]; then
		sleep $sleep_time
	fi
done

if [ -n "$http_code" ]; then
	do_log 3 "RHEV Status check on $1 failed; last HTTP code: $http_code"
else
	do_log 3 "RHEV Status check on $1 failed"
fi

exit 1
