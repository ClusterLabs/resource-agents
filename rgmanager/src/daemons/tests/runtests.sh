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
	diff -w $t.expected $t.out
	if [ $? -ne 0 ]; then
		echo "FAILED"
		echo "*** Basic Test $t failed"
		exit 1
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
	diff -w delta-$prev-$t.expected delta-$prev-$t.out 
	if [ $? -ne 0 ]; then
		echo "FAILED"
		echo "*** Differential test between $prev and $t failed"
		exit 1
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
