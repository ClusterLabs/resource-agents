###############################################################################
###############################################################################
##
##  Copyright (C) Sistina Software, Inc.  1997-2004  All rights reserved.
##  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
##  
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

SOURCE=libcman.c

LIBNAME=libcman
SHAREDLIB=$(LIBNAME).so.${RELEASE_MAJOR}.${RELEASE_MINOR}
STATICLIB=$(LIBNAME).a

top_srcdir=..
UNINSTALL=${top_srcdir}/scripts/uninstall.pl

include ${top_srcdir}/make/defines.mk

CFLAGS += -g -O -I. -fPIC

ifneq (${KERNEL_SRC}, )
# Use the kernel tree if patched, otherwise, look where cluster headers
#  should be installed
CFLAGS += $(shell if [ -d ${KERNEL_SRC}/include/cluster ]; then \
		echo '-I${KERNEL_SRC}/include/cluster'; else \
		echo '-I${incdir}/cluster'; fi)
else
CFLAGS += -I${incdir}/cluster
endif


all: $(STATICLIB) $(SHAREDLIB)

$(LIBNAME).a: libcman.o
	${AR} r libcman.a libcman.o
	${RANLIB} libcman.a 


$(LIBNAME).so.${RELEASE_MAJOR}.${RELEASE_MINOR}: libcman.o
	$(CC) -shared -o $@ -Wl,-soname=$(LIBNAME).so.$(RELEASE_MAJOR) $^

libcman.o: libcman.c
	$(CC) $(CFLAGS) -c -o $@ $<

copytobin: all


install: all
	install -d ${incdir}
	install libcman.h ${incdir}
	install -d ${libdir}
	install $(LIBNAME).a ${libdir}
	install $(LIBNAME).so.$(RELEASE_MAJOR).$(RELEASE_MINOR) ${libdir}
	ln -sf $(LIBNAME).so.$(RELEASE_MAJOR).$(RELEASE_MINOR) ${libdir}/$(LIBNAME).so
	ln -sf $(LIBNAME).so.$(RELEASE_MAJOR).$(RELEASE_MINOR) ${libdir}/$(LIBNAME).so.$(RELEASE_MAJOR)

uninstall:
	${UNINSTALL} libcman.h ${incdir}
	${UNINSTALL} \
		${LIBNAME}.a \
		$(LIBNAME).so \
		$(LIBNAME).so.$(RELEASE_MAJOR) \
		${LIBNAME}.so.$(RELEASE_MAJOR).$(RELEASE_MINOR) ${libdir} \
	rm -f /lib/${LIBNAME}.*

clean:
	rm -f *.o *.a *.so *.so.*
	rm -f *~
