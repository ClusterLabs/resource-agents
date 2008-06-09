#!/bin/sh

LANG=C
LC_ALL=C
LOCALE=C
export LANG LC_ALL LOCALE

. testlist

echo "WARNING:"
echo "  You will need to MANUALLY verify these test cases after generation!"
echo "  Do NOT commit them to CVS without first hand-checking each and"
echo "  every one!  These are meant to help determine possible regressions"
echo "  in the tree handling code and the resource code."
echo ""

echo -n "Are you sure [y/N] ?"
read foo
if [ "$foo" != "y" ]; then
	echo "Ok, aborting..."
	exit 0
fi

#
# Basic config tests.
#
for t in $TESTS; do
	echo -n "Generating $t..."
	../rg_test ../../resources test $t.conf > $t.expected 2> /dev/null
	if grep "Error" $t.expected; then
		echo "FAILED"
		exit 1
	fi
	echo OK
done


#
# Start/stop tests (noop)
#
for t in $TESTS; do
	declare SERVICES=$(echo "xpath /cluster/rm/service" | xmllint $t.conf --shell | grep content | cut -f2 -d'=')
	declare phase svc
	echo -n "Generating $t exec..."
	for phase in start stop; do
		echo -n "$phase..."
		rm -f $t.$phase.expected
		for svc in $SERVICES; do
			../rg_test ../../resources noop $t.conf $phase service $svc >> $t.$phase.expected 2> /dev/null
		done
	done
	echo "OK"
done


#
# Delta tests
#
prev=
for t in $TESTS; do
	if [ -z "$prev" ]; then
		prev=$t
		continue
	fi
	echo -n "Generating delta between $prev and $t..."
	../rg_test ../../resources delta \
		$prev.conf $t.conf > delta-$prev-$t.expected 2> /dev/null
	if grep "Error" delta-$prev-$t.expected; then
		echo "FAILED"
		exit 1
	fi
	prev=$t
	echo OK
done
