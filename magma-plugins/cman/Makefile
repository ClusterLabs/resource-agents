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

INCLUDES+= -I. -I${KERNEL_SRC}/include/cluster -I${incdir}
LIBS     = -L${libdir} -ldlm -lpthread
CFLAGS  += -D__CLUSTER__ -D_REENTRANT
OBJS     = cman.o
TARGETS  = cman.so

all: $(TARGETS)

install: all
	install -d ${libdir}/magma/plugins
	install -m 0755 $(TARGETS) ${libdir}/magma/plugins

uninstall:
	${UNINSTALL} ${TARGETS} ${libdir}/magma/plugins

cman.so: buildtest $(OBJS)
	$(CC) -o $@ $(OBJS) -shared $(CFLAGS) $(LIBS)

buildtest: $(OBJS)
	echo "int main(void) { return 0; }" > __buildtest.c
	$(CC) -c __buildtest.c
	$(CC) -o $@ __buildtest.o $(OBJS) $(LIBS) 
	rm -f __buildtest*

%.o: %.c
	$(CC) -c -o $@ $^ $(INCLUDES) $(CFLAGS)

clean:
	rm -f *o *~ *.so buildtest

