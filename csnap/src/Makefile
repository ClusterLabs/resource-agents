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

binaries = mksnapstore csnap-server csnap-create csnap-delete csnap-agent
deps = csnap.h trace.h sock.h buffer.h list.h

top_srcdir=..
include $(top_srcdir)/make/defines.mk

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

install: all
	if [ ! -d ${sbindir} ]; then \
		install -d ${sbindir}; \
	fi
	strip ${TARGET}
	install -m755 ${binaries} ${sbindir}

clean:
	rm -f $(binaries) *.o a.out
