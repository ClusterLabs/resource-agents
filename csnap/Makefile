utilities = mksnapstore csnap-server csnap-create csnap-delete csnap-connect csnap-test devpoke
headers = csnap.h ../dm-csnap.h trace.h sock.h

all: $(utilities)

mksnapstore: csnap.c $(headers)
	cc -Wall csnap.c -DCREATE -o mksnapstore

csnap-server: csnap.c $(headers)
	cc -Wall csnap.c -DSERVER -o csnap-server

csnap-connect: service.c $(headers)
	cc -Wall service.c -o csnap-connect

csnap-create csnap-delete: create.c $(headers)
	cc -Wall create.c -DCREATE -o csnap-create
	cc -Wall create.c -DDELETE -o csnap-delete

csnap-test: testclient.c $(headers)
	cc -Wall testclient.c -o csnap-test

devpoke: devpoke.c
	cc -Wall devpoke.c -o devpoke

clean:
	rm -f $(utilities) *.o a.out

kernel:
	cd ../../.. && make bzImage

kern:
	cd ../../.. && make bzImage SUBDIRS=drivers/md

test: test1 delay test2 test3

delay:
	echo Bogus delay...
	sleep 1

test1:
	killall csnap-server || true
	./mksnapstore /dev/test-origin /dev/test-snapstore 8080
	./csnap-server /dev/test-origin /dev/test-snapstore 8080 &
	sudo /sbin/dmsetup remove testdev || true
#	sudo /sbin/dmsetup create testdev test.dm

test2:
	sudo ./csnap-create localhost:8080 0
	sudo ./csnap-connect /dev/mapper/testdev localhost:8080

test3:
#	sudo ./devpoke /dev/mapper/testdev write 2
	sudo ./devpoke /dev/mapper/testdev read 2

test9:
	sudo /sbin/dmsetup remove testdev
