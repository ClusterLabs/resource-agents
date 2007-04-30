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

TARGET= libdlm
AISTARGET= libaislock

SHAREDLIB=$(TARGET).so.${RELEASE_MAJOR}.${RELEASE_MINOR} $(TARGET)_lt.so.${RELEASE_MAJOR}.${RELEASE_MINOR}
STATICLIB=$(TARGET).a $(TARGET)_lt.a

CFLAGS += -g -O2 
CFLAGS += -I.

LDFLAGS += -lpthread 

all: $(STATICLIB) $(SHAREDLIB)

$(TARGET).a: $(TARGET).o $(AISTARGET).o
	${AR} r $@ $^
	${RANLIB} $@

$(TARGET)_lt.a: $(TARGET)_lt.o
	${AR} r $@ $^
	${RANLIB} $@

$(TARGET).so.${RELEASE_MAJOR}.${RELEASE_MINOR}: $(TARGET).po $(AISTARGET).po
	$(CC) $(LDFLAGS) -shared -o $@ -Wl,-soname=$(TARGET).so.$(RELEASE_MAJOR) $^
	ln -sf $(TARGET).so.$(RELEASE_MAJOR).$(RELEASE_MINOR) $(TARGET).so
	ln -sf $(TARGET).so.$(RELEASE_MAJOR).$(RELEASE_MINOR) $(TARGET).so.$(RELEASE_MAJOR)

$(TARGET)_lt.so.${RELEASE_MAJOR}.${RELEASE_MINOR}: $(TARGET)_lt.po
	$(CC) $(LDFLAGS) -shared -o $@ -Wl,-soname=$(TARGET)_lt.so.$(RELEASE_MAJOR) $^
	ln -sf $(TARGET)_lt.so.$(RELEASE_MAJOR).$(RELEASE_MINOR) $(TARGET)_lt.so
	ln -sf $(TARGET)_lt.so.$(RELEASE_MAJOR).$(RELEASE_MINOR) $(TARGET)_lt.so.$(RELEASE_MAJOR)

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
	install -m644 $(TARGET).h ${incdir}
	install -d ${libdir}
	install -d ${DESTDIR}/etc/udev/rules.d
	install -m644 ../scripts/51-dlm.rules ${DESTDIR}/etc/udev/rules.d
	install $(TARGET).a ${libdir}
	install $(TARGET)_lt.a ${libdir}
	install $(TARGET).so.$(RELEASE_MAJOR).$(RELEASE_MINOR) ${libdir}
	install $(TARGET)_lt.so.$(RELEASE_MAJOR).$(RELEASE_MINOR) ${libdir}
	ln -sf $(TARGET).so.$(RELEASE_MAJOR).$(RELEASE_MINOR) ${libdir}/$(TARGET).so
	ln -sf $(TARGET).so.$(RELEASE_MAJOR).$(RELEASE_MINOR) ${libdir}/$(TARGET).so.$(RELEASE_MAJOR)
	ln -sf $(TARGET)_lt.so.$(RELEASE_MAJOR).$(RELEASE_MINOR) ${libdir}/$(TARGET)_lt.so
	ln -sf $(TARGET)_lt.so.$(RELEASE_MAJOR).$(RELEASE_MINOR) ${libdir}/$(TARGET)_lt.so.$(RELEASE_MAJOR)

uninstall:
	${UNINSTALL} $(TARGET).h ${incdir}
	${UNINSTALL} 51-dlm.rules ${DESTDIR}/etc/udev/rules.d
	${UNINSTALL} \
		${TARGET}.a \
		${TARGET}_lt.a \
		$(TARGET).so \
		$(TARGET)_lt.so \
		$(TARGET).so.$(RELEASE_MAJOR) \
		${TARGET}.so.$(RELEASE_MAJOR).$(RELEASE_MINOR) \
		$(TARGET)_lt.so.$(RELEASE_MAJOR) \
		${TARGET}_lt.so.$(RELEASE_MAJOR).$(RELEASE_MINOR) ${libdir}

clean:
	rm -f *.o *.a *.so *.so.* *.po
	rm -f *~
