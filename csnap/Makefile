utilities = mksnapstore csnap-server csnap-create csnap-delete csnap-agent sendagent testclient devpoke devspam
headers = csnap.h ../dm-csnap.h trace.h sock.h

all: $(utilities)

mksnapstore: csnap.c $(headers)
	cc -Wall csnap.c -DCREATE -o mksnapstore

csnap-server: csnap.c $(headers)
	cc -Wall csnap.c -DSERVER -o csnap-server

csnap-agent: agent.c $(headers)
	cc -Wall agent.c -o csnap-agent

csnap-create csnap-delete: create.c $(headers)
	cc -Wall create.c -DCREATE -o csnap-create
	cc -Wall create.c -DDELETE -o csnap-delete

testclient: testclient.c $(headers)
	cc -Wall testclient.c -o csnap-test

sendagent: sendagent.c $(headers)
	cc -Wall sendagent.c -o sendagent

devpoke: devpoke.c
	cc -Wall devpoke.c -o devpoke

devspam: devspam.c
	cc -Wall devspam.c -o devspam

clean:
	rm -f $(utilities) *.o a.out

kernel:
	cd ../../.. && make bzImage

kern:
	cd ../../.. && make bzImage SUBDIRS=drivers/md

test: test1 test2 test3

test1:
	killall csnap-server || true
	./mksnapstore /dev/test-origin /dev/test-snapstore 8080
	./csnap-server /dev/test-origin /dev/test-snapstore 8080
	sudo /sbin/dmsetup remove testdev || true
	sudo ./csnap-agent @testdev-control localhost:8080

test2:
	sudo ./csnap-create localhost:8080 0
	echo 0 497976 csnapshot 0 /dev/test-origin /dev/test-snapstore @testdev-control | sudo /sbin/dmsetup create testdev

test3:
#	sudo ./devpoke /dev/mapper/testdev write 2
	sudo ./devpoke /dev/mapper/testdev read 2

test4:
	sudo ./devspam /dev/mapper/testdev write 1 77

test9:
	sudo /sbin/dmsetup remove testdev
