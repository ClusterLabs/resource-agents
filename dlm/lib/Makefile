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

SOURCE=libdlm.c libaislock.c

LIBNAME=libdlm
SHAREDLIB=$(LIBNAME).so.${RELEASE_MAJOR}.${RELEASE_MINOR} $(LIBNAME)_lt.so.${RELEASE_MAJOR}.${RELEASE_MINOR}
STATICLIB=$(LIBNAME).a $(LIBNAME)_lt.a

top_srcdir=..
UNINSTALL=${top_srcdir}/scripts/uninstall.pl

include ${top_srcdir}/make/defines.mk

CFLAGS += -g -I. -O2
LDFLAGS +=  

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


$(LIBNAME).a: libdlm.o libaislock.o dlm32.o
	${AR} cr libdlm.a $^
	${RANLIB} libdlm.a 
  
$(LIBNAME)_lt.a: libdlm_lt.o dlm32.o
	${AR} r libdlm_lt.a $^
	${RANLIB} libdlm_lt.a 
  
$(LIBNAME).so.${RELEASE_MAJOR}.${RELEASE_MINOR}: libdlm.po libaislock.po dlm32.po
	$(CC) $(LDFLAGS) -shared -o $@ -Wl,-soname=$(LIBNAME).so.$(RELEASE_MAJOR) $^
  
$(LIBNAME)_lt.so.${RELEASE_MAJOR}.${RELEASE_MINOR}: libdlm_lt.po dlm32.po
	$(CC) $(LDFLAGS) -shared -o $@ -Wl,-soname=$(LIBNAME)_lt.so.$(RELEASE_MAJOR) $^
  
%_lt.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<
  
%_lt.po: %.c
	$(CC) $(CFLAGS) -fPIC -c -o $@ $<
  
%.o: %.c
	$(CC) $(CFLAGS) -D_REENTRANT -c -o $@ $<
  
%.po: %.c
	$(CC) $(CFLAGS) -fPIC -D_REENTRANT -c -o $@ $<
  
copytobin: all


install: all
	install -d ${incdir}
	install -m644 libdlm.h ${incdir}
	install -d ${libdir}
	install $(LIBNAME).a ${libdir}
	install $(LIBNAME)_lt.a ${libdir}
	install $(LIBNAME).so.$(RELEASE_MAJOR).$(RELEASE_MINOR) ${libdir}
	install $(LIBNAME)_lt.so.$(RELEASE_MAJOR).$(RELEASE_MINOR) ${libdir}
	ln -sf $(LIBNAME).so.$(RELEASE_MAJOR).$(RELEASE_MINOR) ${libdir}/$(LIBNAME).so
	ln -sf $(LIBNAME).so.$(RELEASE_MAJOR).$(RELEASE_MINOR) ${libdir}/$(LIBNAME).so.$(RELEASE_MAJOR)
	ln -sf $(LIBNAME)_lt.so.$(RELEASE_MAJOR).$(RELEASE_MINOR) ${libdir}/$(LIBNAME)_lt.so
	ln -sf $(LIBNAME)_lt.so.$(RELEASE_MAJOR).$(RELEASE_MINOR) ${libdir}/$(LIBNAME)_lt.so.$(RELEASE_MAJOR)

uninstall:
	${UNINSTALL} libdlm.h ${incdir}
	${UNINSTALL} \
		${LIBNAME}.a \
		${LIBNAME}_lt.a \
		$(LIBNAME).so \
		$(LIBNAME)_lt.so \
		$(LIBNAME).so.$(RELEASE_MAJOR) \
		${LIBNAME}.so.$(RELEASE_MAJOR).$(RELEASE_MINOR) ${libdir} \
		$(LIBNAME)_lt.so.$(RELEASE_MAJOR) \
		${LIBNAME}_lt.so.$(RELEASE_MAJOR).$(RELEASE_MINOR) ${libdir}
	rm -f /lib/${LIBNAME}.*

clean:
	rm -f *.o *.a *.so *.so.* *.po
	rm -f *~
