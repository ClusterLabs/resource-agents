###############################################################################
###############################################################################
##
##  Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
##
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

include ../../make/defines.mk

CFLAGS += -D_GNU_SOURCE
CFLAGS += -Wall -Werror -Wstrict-prototypes -Wshadow -g
CFLAGS += -I${ccsincdir} -I${cmanincdir} -I${incdir}
CFLAGS += -I.

EXTRA_LDFLAGS += -L${cmanlibdir} -L${ccslibdir} -lcman -lccs
EXTRA_LDFLAGS += -lpthread

TARGET1= qdiskd
TARGET2= mkqdisk

OBJS1=	main.o \
	score.o \
	bitmap.o \
	clulog.o \
	gettid.o \
	daemon_init.o

OBJS2=	mkqdisk.o

SHAREDOBJS= disk.o \
	    crc32.o \
	    disk_util.o \
	    proc.o

all: depends ${TARGET1} ${TARGET2}

${TARGET1}: ${SHAREDOBJS} ${OBJS1}
	$(CC) -o $@ $^ $(LDFLAGS) $(EXTRA_LDFLAGS)

${TARGET2}: ${SHAREDOBJS} ${OBJS2}
	$(CC) -o $@ $^ $(LDFLAGS)

install: all
	install -d ${sbindir}
	install ${TARGET1} ${TARGET2} ${sbindir}

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

depends:
	$(MAKE) -C ../lib all

clean:
	rm -f *.o ${TARGET1} ${TARGET2}

uninstall:
	${UNINSTALL} ${TARGET1} ${TARGET2} ${sbindir}
