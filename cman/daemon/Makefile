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

CFLAGS += -DCMAN_RELEASE_NAME=\"${RELEASE}\" -DOPENAIS_EXTERNAL_SERVICE
CFLAGS += -fPIC
CFLAGS += -I${ccsincdir} -I${incdir} -I${openaisincdir} -I${cmanincdir}
CFLAGS += -I${openaisincdir}/openais/service

ifeq ($(DEBUG),y)
CFLAGS += -O0 -g -DDEBUG
else
CFLAGS += -O2
endif

LDFLAGS += -L${ccslibdir} -L${cmanlibdir} -lccs -lcman

TARGET= service_cman.lcrso

OBJS=	daemon.o \
	config.o \
	logging.o \
	ais.o \
	commands.o \
	barrier.o \
	cmanccs.o

all:  ${TARGET}

${TARGET}: ${OBJS}
	$(CC) -shared -Wl,-soname,$@ -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

install: all
	install -d ${libexecdir}/lcrso
	install -s ${TARGET} ${libexecdir}/lcrso

uninstall:
	rm -f ${libexecdir}/lcrso/${TARGET}

clean:
	rm -f *.o ${TARGET}
