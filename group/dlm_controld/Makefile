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

TARGET= dlm_controld

OBJS=	main.o \
	member_cman.o \
	group.o \
	action.o

CFLAGS += -g 
CFLAGS += -I${incdir} -I${ccsincdir} -I${cmanincdir}
CFLAGS += -idirafter ${KERNEL_SRC}/include/linux
CFLAGS += -I../../group/lib/ -I../include/

LDFLAGS += -L${ccslibdir} -L${cmanlibdir} -lccs -lcman
LDFLAGS += -L../lib -lgroup

all: ${TARGET}

${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

install: all
	install -d ${sbindir}
	install ${TARGET} ${sbindir}

uninstall:
	${UNINSTALL} ${TARGET} ${sbindir}

clean:
	rm -f *.o ${TARGET}

