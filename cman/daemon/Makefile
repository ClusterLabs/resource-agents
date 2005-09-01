###############################################################################
###############################################################################
##
##  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
##
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

top_srcdir=..
UNINSTALL=${top_srcdir}/scripts/uninstall.pl
AISDIR=openais20050830


include ${top_srcdir}/make/defines.mk

CFLAGS+= -g -I${ccsincdir} -I${incdir} -I${top_srcdir}/config \
	-DCMAN_RELEASE_NAME=\"${RELEASE}\" -I../lib -DDEBUG \
	-I ${AISDIR}/trunk/exec -L ${AISDIR}/trunk/exec

TARGET=cmand

all: ${TARGET}

copytobin: all
	cp ${TARGET} ${top_srcdir}/bin

cmand:	${AISDIR}/trunk/exec/libtotem_pg.a daemon.o config.o logging.o ais.o commands.o barrier.o ../lib/libcman.a 
	$(CC) $(CFLAGS) -o $@ $^ -L../lib -L${ccslibdir} -lccs -lcman -lpthread -Wl,-Bstatic -ltotem_pg -Wl,-Bdynamic

daemon.o: daemon.c list.h cnxman-socket.h cnxman-private.h xlate.h \
  daemon.h logging.h commands.h barrier.h config.h
	$(CC) $(CFLAGS) -c -o $@ $<

cnxman.o: cnxman.c list.h cnxman-socket.h cnxman-private.h xlate.h \
  daemon.h barrier.h commands.h config.h logging.h
	$(CC) $(CFLAGS) -c -o $@ $<

config.o: config.c config.h
	$(CC) $(CFLAGS) -c -o $@ $<

barrier.o: barrier.c list.h cnxman-socket.h cnxman-private.h xlate.h \
  daemon.h logging.h config.h barrier.h
	$(CC) $(CFLAGS) -c -o $@ $<

commands.o: commands.c list.h cnxman-socket.h cnxman-private.h xlate.h \
  daemon.h barrier.h  logging.h config.h
	$(CC) $(CFLAGS) -c -o $@ $<

logging.o: logging.c list.h cnxman-socket.h cnxman-private.h xlate.h
	$(CC) $(CFLAGS) -c -o $@ $<

# Target to download & unpack openAIS
${AISDIR}/${AISDIR}.tar.gz:
	mkdir -p ${AISDIR}
	cd ${AISDIR} && wget http://people.redhat.com/pcaulfie/${AISDIR}.tar.gz

${AISDIR}/trunk: ${AISDIR}/${AISDIR}.tar.gz
	cd ${AISDIR} && tar -xzf ${AISDIR}.tar.gz
	touch ${AISDIR}/trunk

${AISDIR}/trunk/exec/libtotem_pg.a: ${AISDIR}/trunk
	cd ${AISDIR}/trunk && make

install: cmand
	install -d ${sbindir}
	install cmand ${sbindir}

uninstall:
	${UNINSTALL} cmand ${sbindir}

clean:
	rm -f *.o cmand
	rm -rf ${AISDIR}/trunk

reallyclean: clean
	rm -rf ${AISDIR}

