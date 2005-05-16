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

copytobin: all

install: all
	install -d ${incdir}
	install libgroup.h ${incdir}
	install -d ${libdir}
	install $(LIBNAME).a ${libdir}
	install $(LIBNAME).so.$(RELEASE_MAJOR).$(RELEASE_MINOR) ${libdir}
	ln -sf $(LIBNAME).so.$(RELEASE_MAJOR).$(RELEASE_MINOR) ${libdir}/$(LIBNAME).so
	ln -sf $(LIBNAME).so.$(RELEASE_MAJOR).$(RELEASE_MINOR) ${libdir}/$(LIBNAME).so.$(RELEASE_MAJOR)

uninstall:
	${UNINSTALL} libgroup.h ${incdir}
	${UNINSTALL} \
		${LIBNAME}.a \
		$(LIBNAME).so \
		$(LIBNAME).so.$(RELEASE_MAJOR) \
		${LIBNAME}.so.$(RELEASE_MAJOR).$(RELEASE_MINOR) ${libdir} \
	rm -f /lib/${LIBNAME}.*

clean:
	rm -f *.o *.a *.so *.so.*
	rm -f *~
