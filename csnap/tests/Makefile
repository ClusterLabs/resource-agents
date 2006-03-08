###############################################################################
###############################################################################
##
##  Copyright (C) 2006 Red Hat, Inc.  All rights reserved.
##
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

binaries = testclient devpoke devspam
deps = ../src/csnap.h ../src/trace.h ../src/sock.h ../src/buffer.h ../src/list.h

all: $(binaries)

testclient: testclient.c $(deps)
	cc -Wall testclient.c -o testclient -I../src

devpoke: devpoke.c
	cc -Wall devpoke.c -o devpoke -lpopt

devspam: devspam.c
	cc -Wall devspam.c -o devspam -lpopt

clean:
	rm -f $(binaries) *.o a.out

test: test1 test2 test3

test1:
	killall csnap-server || true
	sudo killall csnap-agent || true
	./mksnapstore /dev/test-snapstore /dev/test-origin
	sudo /sbin/dmsetup remove testdev || true
	sudo ./csnap-agent @test
	./csnap-server /dev/test-snapstore /dev/test-origin @test 8080

test2:
	sudo ./csnap-create localhost:8080 0
	echo 0 497976 csnapshot /dev/test-snapstore /dev/test-origin @test -1 | sudo /sbin/dmsetup create testdev

test3:
	sudo ./devspam /dev/mapper/testdev write 19 77

test9:
	sudo /sbin/dmsetup remove testdev

