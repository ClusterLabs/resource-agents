ddraid_binaries =  mkddraid ddraid-server ddraid-agent
ddsnap_binaries =  mkddsnap ddsnap-server ddsnap-create ddsnap-delete ddsnap-agent ddsnap-cman-agent
binaries =  $(ddraid_binaries) $(ddsnap_binaries) devspam
deps = Makefile trace.h buffer.h list.h sock.h
ddsnap_deps = $(deps) ddsnap.h ../dm-ddsnap.h 
ddraid_deps = $(deps) ddraid.h ../dm-ddraid.h

all: $(binaries)

buffer.o: buffer.c $(deps)
	cc -g -Wall buffer.c -c

mkddraid: ddraid.c buffer.o $(ddraid_deps)
	cc -g -Wall ddraid.c buffer.o -DCREATE -o mkddraid

ddraid-server: ddraid.c buffer.o $(ddraid_deps)
	cc -g -Wall ddraid.c buffer.o -DSERVER -o ddraid-server

ddraid-agent: ddraid.agent.c $(ddraid_deps)
	cc -Wall ddraid.agent.c -ldlm -lpthread -o ddraid-agent

mkddsnap: ddsnap.c buffer.o $(ddsnap_deps)
	cc -g -Wall ddsnap.c buffer.o -DCREATE -o mkddsnap -lpopt

ddsnap-server: ddsnap.c buffer.o $(ddsnap_deps)
	cc -g -Wall ddsnap.c buffer.o -DSERVER -o ddsnap-server -lpopt

ddsnap-cman-agent: ddsnap.cman.agent.c $(ddsnap_deps)
	cc -Wall ddsnap.cman.agent.c -ldlm -lpthread -o ddsnap-cman-agent

ddsnap-agent: ddsnap.cman.agent.c $(ddsnap_deps)
	cc -Wall ddsnap.agent.c -o ddsnap-agent

ddsnap-create ddsnap-delete: ddsnap.control.c $(ddsnap_deps)
	cc -Wall ddsnap.control.c -DCREATE -o ddsnap-create -lpopt
	cc -Wall ddsnap.control.c -DDELETE -o ddsnap-delete -lpopt

devspam: devspam.c trace.h
	cc -Wall devspam.c -o devspam

clean:
	rm -f $(binaries) *.o a.out

install:
	cp $(binaries) /usr/bin

kernel:
	cd ../../.. && make bzImage

kern:
	cd ../../.. && make bzImage SUBDIRS=drivers/md

# Just tests from here on.
device=test
port=8080

# Use an anonymous socket (@ is ddraid's anon socket name extension)
sock=@test

# Intended to run as non-root user (recommended)
# These devices need a+rw permission:
meta=hda5

data0=hda6
#data0=nbd0

data1=hda7
#data1=mapper/loop0

data0=hda6
data1=hda7
data2=hda8
data3=hda9
data4=hda10

data=/dev/$(data0) /dev/$(data1) /dev/$(data2) /dev/$(data3) /dev/$(data4)
#data=/dev/$(data0) /dev/$(data1)

ddraid-test: ddraid-test1 ddraid-test2

ddraid-test1:
	killall ddraid-server || true
	sudo killall ddraid-agent || true
	./mkddraid /dev/$(meta)
	sudo /sbin/dmsetup remove $(device) || true
	sudo ./ddraid-agent @test
	./ddraid-server /dev/$(meta) $(data) $(sock) $(port)

ddraid-test2: ddraid-order2

ddraid-order0:
	echo 0 204800 ddraid 2 $(data) $(sock) | sudo /sbin/dmsetup create $(device)
	sudo chmod a+rw /dev/mapper/$(device)

ddraid-order1:
	echo 0 204800 ddraid 3 $(data) $(sock) | sudo /sbin/dmsetup create $(device)
	sudo chmod a+rw /dev/mapper/$(device)

ddraid-order2:
	echo 0 204800 ddraid 5 $(data) $(sock) | sudo /sbin/dmsetup create $(device)
	sudo chmod a+rw /dev/mapper/$(device)

ddraid-test3:
	./devspam /dev/mapper/$(device) write 100 20 77

ddraid-test4:
	./devspam /dev/mapper/$(device) read 100 20 77

ddraid-test8:
	killall ddraid-server

ddraid-test9:
	sudo /sbin/dmsetup remove $(device)

ddsnap-test: ddsnap-test1 ddsnap-test2 ddsnap-test3

ddsnap-test1:
	killall ddsnap-server || true
	sudo killall ddsnap-agent || true
	./mksnapstore /dev/test-snapstore /dev/test-origin 
	sudo /sbin/dmsetup remove testdev || true
	sudo ./ddsnap-agent @test
	./ddsnap-server /dev/test-snapstore /dev/test-origin @test 8080

ddsnap-test2:
	sudo ./ddsnap-create localhost:8080 0
	echo 0 497976 ddsnap /dev/test-snapstore /dev/test-origin @test -1 | sudo /sbin/dmsetup create testdev

ddsnap-test3:
#	sudo ./devpoke /dev/mapper/testdev write 2
#	sudo ./devpoke /dev/mapper/testdev read 2
#	sleep 1
#	killall ddsnap-server
	sudo ./devspam /dev/mapper/testdev write 19 77

ddsnap-test4:
#	sudo ./devspam /dev/mapper/testdev write 1 77

ddsnap-test9:
	sudo /sbin/dmsetup remove testdev
