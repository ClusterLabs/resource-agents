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

INCLUDES+= -I. -I${incdir}
LIBS     = -L${libdir}
CFLAGS  += -D__CLUSTER__
OBJS     = dumb.o
TARGETS  = dumb.so

all: $(TARGETS)

install: all

uninstall:

dumb.so: buildtest $(OBJS)
	$(CC) -o $@ $(OBJS) -shared $(CFLAGS) $(LIBS)

buildtest: $(OBJS)
	echo "int main(void) { return 0; }" > __buildtest.c
	$(CC) -c __buildtest.c
	$(CC) -o $@ __buildtest.o $(OBJS) -lxml2 $(LIBS)
	rm -f __buildtest*

%.o: %.c
	$(CC) -c -o $@ $^ $(INCLUDES) $(CFLAGS)

clean:
	rm -f *o *~ *.so buildtest

