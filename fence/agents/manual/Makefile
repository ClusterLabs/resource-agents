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

fence_manual_SOURCE= manual.c
fence_ack_manual_SOURCE= ack.c

top_srcdir=../..


INCLUDE= -I${top_srcdir}/include -I${top_srcdir}/config -I${cmankincdir} -I${incdir}


include ${top_srcdir}/make/defines.mk

CFLAGS+= -DFENCE_RELEASE_NAME=\"${RELEASE}\"

all: ${TARGET}

fence_manual: ${fence_manual_SOURCE:.c=.o}
	${CC} ${CFLAGS} ${LDFLAGS} ${fence_manual_SOURCE:.c=.o} ${LOADLIBES} ${LDLIBS} -o $@

fence_ack_manual: ${fence_ack_manual_SOURCE:.c=.o}
	${CC} ${CFLAGS} ${LDFLAGS} ${fence_ack_manual_SOURCE:.c=.o} ${LOADLIBES} ${LDLIBS} -o $@

copytobin: all
	cp ${TARGET} ${top_srcdir}/bin

clean:
	rm -f *.o ${TARGET}
