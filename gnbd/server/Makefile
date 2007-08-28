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

include ../../make/defines.mk

TARGET1= gnbd_serv
TARGET2= gnbd_clusterd

OBJS1=	gnbd_serv.o \
	local_req.o \
	extern_req.o \
	device.o \
	gserv.o \
	fence.o \
	../utils/trans.o

OBJS2=	gnbd_clusterd.o \
	../utils/member_cman.o

SHAREDOBJS= ../utils/gnbd_utils.o

CFLAGS += -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE
CFLAGS += -I${gnbdkincdir} -I${cmanincdir}
CFLAGS += -I../include -I../utils
CFLAGS += -I${incdir}

LDFLAGS+= -L${libdir} -L${cmanlibdir} -lcman -ldl -lpthread

all: ${TARGET1} ${TARGET2}

${TARGET1}: ${SHAREDOBJS} ${OBJS1}
	$(CC) -o $@ $^ $(LDFLAGS)

${TARGET2}: ${SHAREDOBJS} ${OBJS2}
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

install: all
	if [ ! -d ${sbindir} ]; then \
		install -d ${sbindir}; \
	fi
	install -m755 ${TARGET1} ${sbindir}
	install -m755 ${TARGET2} ${sbindir}

uninstall:
	${UNINSTALL} ${TARGET1} ${TARGET2} ${sbindir}

clean:
	rm -f *.o ${TARGET1} ${TARGET2}
