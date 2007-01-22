###############################################################################
###############################################################################
##
##  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
##  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
##  
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

TARGET= fence_manual fence_ack_manual

top_srcdir=../..


INCLUDE= -I${top_srcdir}/include -I${top_srcdir}/config -I${incdir} -I../../../cman/lib -I../../../group/lib


include ${top_srcdir}/make/defines.mk

CFLAGS+= -DFENCE_RELEASE_NAME=\"${RELEASE}\"

LDFLAGS+= -L../../../cman/lib

all: ${TARGET}

fence_manual: manual.o ../../../group/lib/libgroup.a
	$(CC) $(LDFLAGS) -o $@ $^ -lcman

manual.o: manual.c
	$(CC) $(CFLAGS) -c -o $@ $<

fence_ack_manual: ack.o
	${CC} -o $@ $^

# Using manual override instead of the binary version.
# (this version does not require any configuration params
#  in cluster.conf, but fencing must fail first)
#fence_ack_manual: fence_ack_manual.sh
#	cp $@ $^

ack.o: ack.c
	$(CC) $(CFLAGS) -c -o $@ $<

install: all
	if [ ! -d ${sbindir} ]; then \
		install -d ${sbindir}; \
	fi
	install -m755 ${TARGET} ${sbindir}

clean:
	rm -f *.o ${TARGET}
