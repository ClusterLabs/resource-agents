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
include ${top_srcdir}/make/defines.mk
UNINSTALL=${top_srcdir}/scripts/uninstall.pl

INCLUDE+= -I${top_srcdir}/lib
LIBS     = -L${top_srcdir}/lib -lmagma -lmagmamsg -ldl -lpthread
TARGETS  = cpt cptester circleping cluster_cmd
CFLAGS  += -DDEBUG

all: $(TARGETS)

install: all
	install -d ${sbindir}
	install -m 0755 $(TARGETS) ${sbindir}

uninstall:
	${UNINSTALL} ${TARGETS} ${sbindir}

circleping: circleping.o
	$(CC) -o $@ $^ $(LIBS) -ggdb

cluster_cmd: cluster_cmd.o
	$(CC) -o $@ $^ $(LIBS)

cpt: cpt.o
	$(CC) -o $@ $^ $(LIBS) -ggdb

cptester: cptester.o
	$(CC) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) -c -o $@ $^ $(INCLUDES) $(CFLAGS) -ggdb

clean:
	rm -f *o *~ *.so $(TARGETS)

