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

include ../../../make/defines.mk

TARGET1= libclulib.a
TARGET2= liblalloc.a
TARGET3= msgtest

OBJS1=	clulog.o daemon_init.o signals.o msgsimple.o \
	gettid.o rg_strings.o message.o members.o fdops.o \
	lock.o cman.o vft.o msg_cluster.o msg_socket.o \
	wrap_lock.o sets.o

OBJS2= alloc.o

OBJS3= msgtest.o

CFLAGS += -Werror -Wstrict-prototypes -Wshadow -fPIC -D_GNU_SOURCE
CFLAGS += -I${cmanincdir} -I${dlmincdir}
CFLAGS += -I$(S)/../../include
CFLAGS += -I${incdir}

LDFLAGS += -L${cmanlibdir} -lcman 
LDFLAGS += -L. -lclulib -llalloc
LDFLAGS += -lpthread

all: ${TARGET1} ${TARGET2} ${TARGET3}

install: all

uninstall:

${TARGET1}: ${OBJS1}
	${AR} cru $@ $^
	${RANLIB} $@

${TARGET2}: ${OBJS2}
	${AR} cru $@ $^
	${RANLIB} $@

${TARGET3}: ${OBJS3} ${TARGET1}
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: $(S)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o ${TARGET1} ${TARGET2} ${TARGET3}
