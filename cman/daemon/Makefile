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

top_srcdir=..
UNINSTALL=${top_srcdir}/scripts/uninstall.pl
AISDIR=openais20060307
AISTAG=trunk

include ${top_srcdir}/make/defines.mk

CFLAGS+= -g -I${ccsincdir} -I${incdir} -I${top_srcdir}/config \
	-DCMAN_RELEASE_NAME=\"${RELEASE}\" -I../lib -DDEBUG \
	-I ${AISDIR}/${AISTAG}/exec -L ${AISDIR}/${AISTAG}/exec

TARGET=service_cman.lcrso
CMAN_OBJS=daemon.o config.o logging.o ais.o commands.o barrier.o cmanccs.o

all: ${AISDIR}/${AISTAG}/exec/aisexec ${TARGET}

copytobin: all
	cp ${TARGET} ${top_srcdir}/bin

service_cman.lcrso: ${CMAN_OBJS} ${AISDIR}/${AISTAG}/exec/aisexec
	$(CC) $(CFLAGS) -shared -shared -Wl,-soname,service_cman.lcrso -o $@ ${CMAN_OBJS} -L../lib -L${ccslibdir} -lccs
	cp ${TARGET} ${AISDIR}/${AISTAG}/exec
	rm -f openais
	ln -sf ${AISDIR} openais

daemon.o: daemon.c list.h cnxman-socket.h cnxman-private.h \
  daemon.h logging.h commands.h barrier.h config.h cmanccs.h
	$(CC) $(CFLAGS) -c -o $@ $<

cnxman.o: cnxman.c list.h cnxman-socket.h cnxman-private.h \
  daemon.h barrier.h commands.h config.h logging.h
	$(CC) $(CFLAGS) -c -o $@ $<

config.o: config.c config.h
	$(CC) $(CFLAGS) -c -o $@ $<

cmanccs.o: cmanccs.c cmanccs.h
	$(CC) $(CFLAGS) -c -o $@ $<

barrier.o: barrier.c list.h cnxman-socket.h cnxman-private.h \
  daemon.h logging.h config.h barrier.h
	$(CC) $(CFLAGS) -c -o $@ $<

commands.o: commands.c list.h cnxman-socket.h cnxman-private.h \
  daemon.h barrier.h  logging.h config.h
	$(CC) $(CFLAGS) -c -o $@ $<

logging.o: logging.c list.h cnxman-socket.h cnxman-private.h 
	$(CC) $(CFLAGS) -c -o $@ $<

# Target to download openAIS
${AISDIR}/${AISDIR}.tar.gz:
	mkdir -p ${AISDIR}
	cd ${AISDIR} && wget http://people.redhat.com/pcaulfie/${AISDIR}.tar.gz

# Unpack the tarball and apply our patches
${AISDIR}/${AISTAG}: ${AISDIR}/${AISDIR}.tar.gz
	cd ${AISDIR} && tar -xzf ${AISDIR}.tar.gz
	cd ${AISDIR} && patch -p1 < ../ais-cman-hack.patch
	touch ${AISDIR}/${AISTAG}

${AISDIR}/${AISTAG}/exec/aisexec: ${AISDIR}/${AISTAG}
	cd ${AISDIR}/${AISTAG} && make BUILD_DYNAMIC=1

install: 
	install -d ${libexecdir}
	install -d ${libexecdir}/cman
	install ${AISDIR}/${AISTAG}/exec/aisexec ${libexecdir}/cman/
	install ${AISDIR}/${AISTAG}/exec/service*.lcrso ${libexecdir}/cman/
	install ${AISDIR}/${AISTAG}/exec/keygen ${sbindir}/ais-keygen

uninstall:
	rm -rf ${libexecdir}/cman
	${UNINSTALL} ais-keygen ${sbindir}

clean:
	rm -f *.o service_cman.lcrso
	rm -rf ${AISDIR}/${AISTAG}
	rm -f openais

reallyclean: clean
	rm -rf ${AISDIR}

