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
LIBSNT   = -L${top_srcdir}/lib -lmagma_nt -ldl
TARGETS  = magma_tool circleping cluster_cmd
TESTS    = cptester thread_test
CFLAGS  += -DDEBUG -D_GNU_SOURCE -DPLUGINDIR=\"${plugindir}\" \
	   -DINCDIR=\"${incdir}\" -DLIBDIR=\"${libdir}\" \
	   -DSLIBDIR=\"${slibdir}\"

all: $(TARGETS) $(TESTS)

install: all
	install -d ${sbindir}
	install -m 0755 $(TARGETS) ${sbindir}

uninstall:
	${UNINSTALL} ${TARGETS} ${sbindir}

circleping: circleping.o
	$(CC) -o $@ $^ $(LIBS) -ggdb

cluster_cmd: cluster_cmd.o
	$(CC) -o $@ $^ $(LIBS)

magma_tool: magma_tool.o
	$(CC) -o $@ $^ $(LIBS) -ggdb

cptester: cptester.o
	$(CC) -o $@ $^ $(LIBSNT)

thread_test: thread_test.o
	$(CC) -o $@ $^ $(LIBS) -ggdb


%.o: %.c
	$(CC) -c -o $@ $^ $(INCLUDES) $(CFLAGS) -ggdb

clean:
	rm -f *o *~ *.so $(TARGETS) $(TESTS)

