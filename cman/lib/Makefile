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

include ../../make/defines.mk

TARGET=libcman

SHAREDLIB=$(TARGET).so.${RELEASE_MAJOR}.${RELEASE_MINOR}
STATICLIB=$(TARGET).a

CFLAGS += -g -I. -fPIC
CFLAGS += -I../daemon

all: $(STATICLIB) $(SHAREDLIB)

$(TARGET).a: $(TARGET).o
	${AR} r $@ $^
	${RANLIB} $@

$(TARGET).so.${RELEASE_MAJOR}.${RELEASE_MINOR}: $(TARGET).o
	$(CC) -shared -o $@ -Wl,-soname=$(TARGET).so.$(RELEASE_MAJOR) $<
	ln -sf $(TARGET).so.$(RELEASE_MAJOR).$(RELEASE_MINOR) $(TARGET).so
	ln -sf $(TARGET).so.$(RELEASE_MAJOR).$(RELEASE_MINOR) $(TARGET).so.$(RELEASE_MAJOR)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

install: all
	install -d ${incdir}
	install $(TARGET).h ${incdir}
	install -d ${libdir}
	install $(TARGET).a ${libdir}
	install $(TARGET).so.$(RELEASE_MAJOR).$(RELEASE_MINOR) ${libdir}
	ln -sf $(TARGET).so.$(RELEASE_MAJOR).$(RELEASE_MINOR) ${libdir}/$(TARGET).so
	ln -sf $(TARGET).so.$(RELEASE_MAJOR).$(RELEASE_MINOR) ${libdir}/$(TARGET).so.$(RELEASE_MAJOR)

uninstall:
	${UNINSTALL} $(TARGET).h ${incdir}
	${UNINSTALL} \
		${TARGET}.a \
		$(TARGET).so \
		$(TARGET).so.$(RELEASE_MAJOR) \
		${TARGET}.so.$(RELEASE_MAJOR).$(RELEASE_MINOR) ${libdir}

clean:
	rm -f *.o *.a *.so *.so.*
	rm -f *~
