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
STATICLIB=libdlm.a

SOURCE=libdlm.c

LIBNAME=libdlm
SHAREDLIB=$(LIBNAME).so.${RELEASE_MAJOR}.${RELEASE_MINOR}

top_srcdir=..
UNINSTALL=${top_srcdir}/scripts/uninstall.pl

include ${top_srcdir}/make/defines.mk

CFLAGS += -g -O -D_REENTRANT -I.

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

$(LIBNAME).a: libdlm.o
	${AR} r libdlm.a libdlm.o
	${RANLIB} libdlm.a 

$(LIBNAME).so.${RELEASE_MAJOR}.${RELEASE_MINOR}: libdlm.po
	$(CC) -shared -o $@ -Wl,-soname=$(LIBNAME).so.$(RELEASE_MAJOR) $^

libdlm.po: libdlm.c
	$(CC) $(CFLAGS) -fPIC -c -o $@ $<

copytobin: all


install: all
	install -d ${incdir}
	install libdlm.h ${incdir}
	install -d ${libdir}
	install $(LIBNAME).a ${libdir}
	install $(LIBNAME).so.$(RELEASE_MAJOR).$(RELEASE_MINOR) ${libdir}
	ln -sf ${libdir}/$(LIBNAME).so.$(RELEASE_MAJOR).$(RELEASE_MINOR) ${libdir}/$(LIBNAME).so
	ln -sf ${libdir}/$(LIBNAME).so.$(RELEASE_MAJOR).$(RELEASE_MINOR) ${libdir}/$(LIBNAME).so.$(RELEASE_MAJOR)

uninstall:
	${UNINSTALL} libdlm.h ${incdir}
	${UNINSTALL} \
		${LIBNAME}.a \
		$(LIBNAME).so \
		$(LIBNAME).so.$(RELEASE_MAJOR) \
		${LIBNAME}.so.$(RELEASE_MAJOR).$(RELEASE_MINOR) ${libdir}

clean:
	rm -f *.o *.a *.so *.so.* *.po
	rm -f *~
