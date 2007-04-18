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
LDFLAGS += -lpthread 

all: $(STATICLIB) $(SHAREDLIB)


$(LIBNAME).a: libdlm.o libaislock.o
	${AR} cr libdlm.a $^
	${RANLIB} libdlm.a 

$(LIBNAME)_lt.a: libdlm_lt.o
	${AR} r libdlm_lt.a $^
	${RANLIB} libdlm_lt.a 

$(LIBNAME).so.${RELEASE_MAJOR}.${RELEASE_MINOR}: libdlm.po libaislock.po
	$(CC) $(LDFLAGS) -shared -o $@ -Wl,-soname=$(LIBNAME).so.$(RELEASE_MAJOR) $^

$(LIBNAME)_lt.so.${RELEASE_MAJOR}.${RELEASE_MINOR}: libdlm_lt.po
	$(CC) $(LDFLAGS) -shared -o $@ -Wl,-soname=$(LIBNAME)_lt.so.$(RELEASE_MAJOR) $^

%_lt.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%_lt.po: %.c
	$(CC) $(CFLAGS) -fPIC -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -D_REENTRANT -c -o $@ $<

%.po: %.c
	$(CC) $(CFLAGS) -fPIC -D_REENTRANT -c -o $@ $<

install: all
	install -d ${incdir}
	install -m644 libdlm.h ${incdir}
	install -d ${libdir}
	install -d ${DESTDIR}/etc/udev/rules.d
	install -m644 ../scripts/51-dlm.rules ${DESTDIR}/etc/udev/rules.d
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
	$(UNINSTALL} ${DESTDIR}/etc/udev/rules.d/51-dlm.rules
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
