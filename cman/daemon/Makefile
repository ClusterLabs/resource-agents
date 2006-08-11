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

include ${top_srcdir}/make/defines.mk

CFLAGS+= -fPIC -I${ccsincdir} -I${incdir} -I${top_srcdir}/config \
	-DCMAN_RELEASE_NAME=\"${RELEASE}\" -I../lib -DOPENAIS_EXTERNAL_SERVICE

ifeq ($(DEBUG),y)
CFLAGS+= -O0 -g -DDEBUG
else
CFLAGS+= -O2
endif

TARGET=service_cman.lcrso
CMAN_OBJS=daemon.o config.o logging.o ais.o commands.o barrier.o cmanccs.o

all:  ${TARGET}

service_cman.lcrso: ${CMAN_OBJS}
	$(CC) $(CFLAGS) -shared -Wl,-soname,service_cman.lcrso -o $@ ${CMAN_OBJS} -L../lib -L${ccslibdir} -lccs

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

install: all
	install -d ${libexecdir}/lcrso
	install -s service_cman.lcrso ${libexecdir}/lcrso

uninstall:
	rm -f ${libexecdir}/lcrso/service_cman.lcrso

clean:
	rm -f *.o service_cman.lcrso

reallyclean: clean
	rm -rf ${AISDIR}

