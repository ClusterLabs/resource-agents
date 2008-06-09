#!/bin/bash

verbose="-D /dev/stdout -v "
passwd="calvin"
login="root"

drac="drac.localhost.localdomain"
modulename=" -m Server-1 "


fence_drac="./fence_drac.pl"
i=0

cmd()
{
	echo $*
	out=$(echo $*; $*)
	out_exit=$?

	echo "$out" > out.log.$i
	echo "Exit $out_exit" >> out.log.$i

	if [ $out_exit -ne 0 ]
	then
		echo "ASSERTION FAILURE: command failed: see out.log.$i" >&2
		exit 1
	fi

	: $((i++))
}

pause()
{
	echo "press ENTER to continue"
	read
}

assert()
{
	get_status
	if [ "$status" != "$1" ]
	then
		echo "ASSERTION FAILURE: power status is '$status' not '$1'" >&2
		exit 1
	fi
}

get_status()
{
	cmd $fence_drac -o status -a $drac -l $login -p $passwd $modulename $verbose
	status=$(echo "$out" | awk '($1 == "status:") { print tolower($2)}')
	echo "status -> $status"
}

get_status
pause

cmd $fence_drac -o off -a $drac -l $login -p $passwd $modulename $verbose 
assert off

cmd $fence_drac -o on -a $drac -l $login -p $passwd $modulename $verbose 
assert on

cmd $fence_drac -o off -a $drac -l $login -p $passwd $modulename $verbose 
assert off

cmd $fence_drac -o reboot -a $drac -l $login -p $passwd $modulename $verbose 
assert on

cmd $fence_drac -o on -a $drac -l $login -p $passwd $modulename $verbose 
assert on

cmd $fence_drac -o reboot -a $drac -l $login -p $passwd $modulename $verbose 
assert on

cmd $fence_drac -o off -a $drac -l $login -p $passwd $modulename $verbose 
assert off

cmd $fence_drac -o off -a $drac -l $login -p $passwd $modulename $verbose 
assert off

echo SUCCESS
exit 0
