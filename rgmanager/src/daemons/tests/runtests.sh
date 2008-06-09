#!/bin/sh
#
# Super primitive sanity check test program.  If the output format of
# any of the trees/lists changes, the tests will need to be regenerated
# and manually checked.
#
# Poor design, but it does effectively detect memory leaks. (when linked
# against the alloc.c in ../../clulib)
#

LANG=C
LC_ALL=C
LOCALE=C
export LANG LC_ALL LOCALE

. ./testlist

echo "Running sanity+memory leak checks on rgmanager tree operations..."

#
# Basic config tests.
#
for t in $TESTS; do
	echo -n "  Checking $t.conf..."
	../rg_test ../../resources test $t.conf > $t.out 2> $t.out.stderr
	diff -uw $t.expected $t.out
	if [ $? -ne 0 ]; then
		echo "FAILED"
		echo "*** Basic Test $t failed"
		echo -n "Accept new output [y/N] ? "
		read ovr
		if [ "$ovr" = "y" ]; then
			cp $t.out $t.expected
		else 
			exit 1
		fi
	fi
	if grep -q "allocation trace" $t.out.stderr; then
		echo "FAILED - memory leak"
		echo "*** Memory Test $t failed"
		echo
		echo Output:
		echo
		cat $t.out.stderr
		exit 1
	fi
	rm -f $t.out $t.out.stderr
	echo OK
done


#
# Start/stop tests (noop)
#
for t in $TESTS; do
	declare SERVICES=$(echo "xpath /cluster/rm/service" | xmllint $t.conf --shell | grep content | cut -f2 -d'=')
	declare phase svc
	echo -n "  Checking $t.conf..."
	for phase in start stop; do
		echo -n "$phase..."
		rm -f $t.$phase.out
		for svc in $SERVICES; do
			../rg_test ../../resources noop $t.conf $phase service $svc >> $t.$phase.out 2> $t.$phase.out.stderr
		done
		diff -w $t.$phase.expected $t.$phase.out
		if [ $? -ne 0 ]; then
			echo "FAILED"
			echo "*** Start Test $t failed"
			exit 1
		fi
		if grep -q "allocation trace" $t.$phase.out.stderr; then
			echo "FAILED - memory leak"
			echo "*** Memory Test $t failed"
			echo
			echo Output:
			echo
			cat $t.$phase.out.stderr
			exit 1
		fi
		rm -f $t.$phase.out $t.$phase.out.stderr
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
	echo -n "  Checking delta between $prev and $t..."
	../rg_test ../../resources delta \
		$prev.conf $t.conf > delta-$prev-$t.out 2> delta-$prev-$t.out.stderr
	diff -uw delta-$prev-$t.expected delta-$prev-$t.out 
	if [ $? -ne 0 ]; then
		echo "FAILED"
		echo "*** Differential test between $prev and $t failed"
		echo -n "Accept new output [y/N] ? "
		read ovr
		if [ "$ovr" = "y" ]; then
			cp delta-$prev-$t.out delta-$prev-$t.expected
		else 
			exit 1
		fi
	fi
	if grep -q "allocation trace" delta-$prev-$t.out.stderr; then
		echo "FAILED - memory leak"
		echo "*** Memory Test $t failed"
		echo
		echo Output:
		echo
		cat delta-$prev-$t.out.stderr
		exit 1
	fi
	rm -f delta-$prev-$t.out delta-$prev-$t.out.stderr
	prev=$t
	echo OK
done
