###############################################################################
###############################################################################
##
##  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
##
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

include ../../make/defines.mk

TARGET1= mount.gfs2
TARGET2= umount.gfs2

OBJS1=	mount.gfs2.o

OBJS2=	umount.gfs2.o

SHAREDOBJS= ondisk1.o \
	    util.o \
	    mtab.o

CFLAGS += -DHELPER_PROGRAM -D_FILE_OFFSET_BITS=64
CFLAGS += -DGFS2_RELEASE_NAME=\"${RELEASE}\" -D_GNU_SOURCE
CFLAGS += -O2
CFLAGS += -I${gfskincdir}
CFLAGS += -I../include
CFLAGS += -I${incdir}

all: ${TARGET1} ${TARGET2}

${TARGET1}: ${SHAREDOBJS} ${OBJS1}
	$(CC) -o $@ $^ $(LDFLAGS)

${TARGET2}: ${SHAREDOBJS} ${OBJS2}
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

install: all
	install ${TARGET1} ${sbindir}
	install ${TARGET2} ${sbindir}

uninstall:
	${UNINSTALL} ${TARGET1} ${TARGET2} ${sbindir}

clean:
	rm -f *.o ${TARGET1} ${TARGET2}

