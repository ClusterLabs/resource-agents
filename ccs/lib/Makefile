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

include ../../make/defines.mk

TARGET=libccs.a

OBJS=	libccs.o \
	log.o

CFLAGS += -D_FILE_OFFSET_BITS=64
CFLAGS += -fPIC
CFLAGS += -I../include -I../common
CFLAGS += -I${incdir}

ifeq ($(DEBUG),y)
CFLAGS += -O0 -DDEBUG -g
else
CFLAGS += -O2
endif

all: ${TARGET}

${TARGET}: ${OBJS}
	${AR} cr ${TARGET} ${OBJS}

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

log.c:
	ln -sf ../common/log.c log.c

install: all
	install -d ${libdir}
	install -m644 ${TARGET} ${libdir}
	install -d ${incdir}
	install -m644 ccs.h ${incdir}

uninstall:
	${UNINSTALL} ${TARGET} ${libdir} 
	${UNINSTALL} ccs.h ${incdir} 

clean:
	rm -rf *~ *.o ${TARGET} ccs.h.gch log.c
