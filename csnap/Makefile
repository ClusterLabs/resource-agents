binaries = mksnapstore csnap-server csnap-create csnap-delete csnap-agent testclient devpoke devspam
deps = csnap.h ../dm-csnap.h trace.h sock.h buffer.h list.h buffer.h Makefile

all: buffer.o $(binaries)

buffer.o: buffer.c $(deps)
	cc -g -Wall buffer.c -c

mksnapstore: csnap.c buffer.o $(deps)
	cc -g -Wall csnap.c buffer.o -DCREATE -o mksnapstore -lpopt

csnap-server: csnap.c buffer.o $(deps)
	cc -g -Wall csnap.c buffer.o -DSERVER -o csnap-server -lpopt

csnap-agent: agent.c $(deps)
	cc -g -Wall agent.c -I../../../ -ldlm -lmagma -ldl -lpthread -o csnap-agent -lpopt

csnap-create csnap-delete: create.c $(deps)
	cc -Wall create.c -DCREATE -o csnap-create -lpopt
	cc -Wall create.c -DDELETE -o csnap-delete -lpopt

testclient: testclient.c $(deps)
	cc -Wall testclient.c -o testclient

devpoke: devpoke.c
	cc -Wall devpoke.c -o devpoke -lpopt

devspam: devspam.c
	cc -Wall devspam.c -o devspam -lpopt

clean:
	rm -f $(binaries) *.o a.out

kernel:
	cd ../../.. && make bzImage

kern:
	cd ../../.. && make bzImage SUBDIRS=drivers/md

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
#	sudo ./devpoke /dev/mapper/testdev write 2
#	sudo ./devpoke /dev/mapper/testdev read 2
#	sleep 1
#	killall csnap-server
	sudo ./devspam /dev/mapper/testdev write 19 77

test4:
#	sudo ./devspam /dev/mapper/testdev write 1 77

test9:
	sudo /sbin/dmsetup remove testdev
