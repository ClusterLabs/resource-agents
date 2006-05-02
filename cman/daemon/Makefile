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
AISDIR=openais20060502
AISTAG=trunk

include ${top_srcdir}/make/defines.mk

CFLAGS+= -fPIC -g -I${ccsincdir} -I${incdir} -I${top_srcdir}/config \
	-DCMAN_RELEASE_NAME=\"${RELEASE}\" -I../lib -DDEBUG \
	-I ${AISDIR}/${AISTAG}/exec -L ${AISDIR}/${AISTAG}/exec

TARGET=service_cman.lcrso
CMAN_OBJS=daemon.o config.o logging.o ais.o commands.o barrier.o cmanccs.o
AIS_LIBS=libSaClm libSaAmf libSaCkpt libSaEvt libSaLck libSaMsg libcpg libais libevs
AIS_HEADERS=cpg.h evs.h openaisCfg.h saAis.h saCkpt.h saClm.h saEvt.h saLck.h saMsg.h

all: ${AISDIR}/${AISTAG}/exec/aisexec ${TARGET}

copytobin: all
	cp ${TARGET} ${top_srcdir}/bin

service_cman.lcrso: ${CMAN_OBJS} ${AISDIR}/${AISTAG}/exec/aisexec
	$(CC) $(CFLAGS) -shared -Wl,-soname,service_cman.lcrso -o $@ ${CMAN_OBJS} -L../lib -L${ccslibdir} -lccs
	cp ${TARGET} ${AISDIR}/${AISTAG}/exec
	rm -f openais
	ln -sf ${AISDIR} openais

daemon.o: daemon.c list.h cnxman-socket.h cnxman-private.h \
  daemon.h logging.h commands.h barrier.h cmanccs.h
	$(CC) $(CFLAGS) -c -o $@ $<

cnxman.o: cnxman.c list.h cnxman-socket.h cnxman-private.h \
  daemon.h barrier.h commands.h logging.h
	$(CC) $(CFLAGS) -c -o $@ $<

config.o: config.c
	$(CC) $(CFLAGS) -c -o $@ $<

cmanccs.o: cmanccs.c cmanccs.h
	$(CC) $(CFLAGS) -c -o $@ $<

barrier.o: barrier.c list.h cnxman-socket.h cnxman-private.h \
  daemon.h logging.h barrier.h
	$(CC) $(CFLAGS) -c -o $@ $<

commands.o: commands.c list.h cnxman-socket.h cnxman-private.h \
  daemon.h barrier.h  logging.h
	$(CC) $(CFLAGS) -c -o $@ $<

logging.o: logging.c list.h cnxman-socket.h cnxman-private.h 
	$(CC) $(CFLAGS) -c -o $@ $<

# Target to download openAIS
${AISDIR}/${AISDIR}.tar.gz:
	mkdir -p ${AISDIR}
	cd ${AISDIR} && wget http://people.redhat.com/pcaulfie/${AISDIR}.tar.gz

# Unpack the tarball
${AISDIR}/${AISTAG}: ${AISDIR}/${AISDIR}.tar.gz
	cd ${AISDIR} && tar -xzf ${AISDIR}.tar.gz
	touch ${AISDIR}/${AISTAG}

${AISDIR}/${AISTAG}/exec/aisexec: ${AISDIR}/${AISTAG}
	cd ${AISDIR}/${AISTAG} && make BUILD_DYNAMIC=1 OPENAIS_BUILD=DEBUG

install: 
	install -d ${libexecdir}
	install -d ${libexecdir}/cman
	install ${AISDIR}/${AISTAG}/exec/aisexec ${libexecdir}/cman/
	install ${AISDIR}/${AISTAG}/exec/*.lcrso ${libexecdir}/cman/
	install ${AISDIR}/${AISTAG}/exec/keygen ${sbindir}/ais-keygen
	install -d ${incdir}
	cd ${AISDIR}/${AISTAG}/include; install ${AIS_HEADERS} ${incdir}
	cd ${AISDIR}/${AISTAG}/lib; for i in ${AIS_LIBS}; do \
		install $$i.a $$i.so.1.0 ${libdir}; \
		ln -sf ${libdir}/$$i.so.1.0 ${libdir}/$$i.so.1; \
		ln -sf ${libdir}/$$i.so.1 ${libdir}/$$i.so; \
		done


uninstall:
	rm -rf ${libexecdir}/cman
	${UNINSTALL} ais-keygen ${sbindir}
	${UNINSTALL} ${AIS_HEADERS} ${incdir}
	for i in ${AIS_LIBS}; do \
		${UNINSTALL} $$i.a $$i.so.1.0 $$i.so.1 $$i.so ${libdir}; done

clean:
	rm -f *.o service_cman.lcrso
	rm -rf ${AISDIR}/${AISTAG}
	rm -f openais

reallyclean: clean
	rm -rf ${AISDIR}

