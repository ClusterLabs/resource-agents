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
CFLAGS  += -D_CLUSTER_
OBJS     = dumb.o
TARGETS  = magma_dumb.so

all: $(TARGETS)

install: all
	

uninstall:

magma_dumb.so: buildtest $(OBJS)
	$(CC) -o $@ $(OBJS) -shared $(CFLAGS) $(LIBS)

buildtest: $(OBJS)
	echo "int main(void) { return 0; }" > _buildtest.c
	$(CC) -c _buildtest.c
	$(CC) -o $@ _buildtest.o $(OBJS) $(LIBS)
	rm -f _buildtest*

%.o: %.c
	$(CC) -c -o $@ $^ $(INCLUDES) -fPIC $(CFLAGS)

clean:
	rm -f *o *~ *.so buildtest

