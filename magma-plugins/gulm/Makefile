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

INCLUDES+= -I. -I${incdir}
LIBS    += -L${libdir} -lgulm
CFLAGS  += -D_CLUSTER_ -D_REENTRANT
OBJS     = gulm.o gulm-lock.o
TARGETS  = magma_gulm.so

all: $(TARGETS)

install: all
	mkdir -p ${libdir}/magma
	install -m 0755 $(TARGETS) ${libdir}/magma

uninstall:
	${UNINSTALL} ${TARGETS} ${libdir}/magma

magma_gulm.so: buildtest $(OBJS)
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

