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

SOURCE=libgroup.c

LIBNAME=libgroup
STATICLIB=$(LIBNAME).a

top_srcdir=..
UNINSTALL=${top_srcdir}/scripts/uninstall.pl

# include ${top_srcdir}/make/defines.mk

CFLAGS += -g -O -I. -fPIC -I../daemon

all: $(STATICLIB)

$(LIBNAME).a: libgroup.o
	${AR} r libgroup.a libgroup.o
#	${RANLIB} libgroup.a 

libgroup.o: libgroup.c
	$(CC) $(CFLAGS) -c -o $@ $< -I../daemon

install: all

uninstall:

clean:
	rm -f *.o *.a *.so *.so.*
	rm -f *~
