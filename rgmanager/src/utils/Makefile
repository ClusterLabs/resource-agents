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

include ../../../make/defines.mk

TARGET1= clubufflush
TARGET2= clufindhostname
TARGET3= clustat
TARGET4= clusvcadm
TARGET5= clulog
TARGET6= clunfslock

DISABLEDTARGETS= cluarp

CFLAGS += -Werror -Wstrict-prototypes -Wshadow -fPIC -D_GNU_SOURCE
CFLAGS += -I${ccsincdir} -I${cmanincdir} -I${dlmincdir} -I${ncursesincdir}
CFLAGS += -I../../include
CFLAGS += -I${incdir}

LDFLAGS += -L${libdir} -L${ccslibdir} -L${cmanlibdir} -L${ncurseslibdir}
LDFLAGS += -lcman -lpthread -ldl -lncurses -lccs
LDFLAGS += -L../clulib -lclulib

all: depends ${TARGET1} ${TARGET2} ${TARGET3} ${TARGET4} ${TARGET5} ${TARGET6}

${TARGET1}: ${TARGET1:=.o}
	$(CC) -o $@ $^ $(LDFLAGS)

${TARGET2}: ${TARGET2:=.o}
	$(CC) -o $@ $^ $(LDFLAGS)

${TARGET3}: ${TARGET3:=.o}
	$(CC) -o $@ $^ $(LDFLAGS)

${TARGET4}: ${TARGET4:=.o}
	$(CC) -o $@ $^ $(LDFLAGS)

${TARGET5}: ${TARGET5:=.o}
	$(CC) -o $@ $^ $(LDFLAGS)

${TARGET6}: ${TARGET6}.sh
	cp ${TARGET6}.sh ${TARGET6}
	chmod 755 ${TARGET6}

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

depends:
	$(MAKE) -C ../clulib all

install: all
	install -d ${sbindir}
	install ${TARGET1} ${TARGET2} ${TARGET3} ${TARGET4} ${TARGET5} ${TARGET6} ${sbindir}

uninstall:
	${UNINSTALL} ${TARGET1} ${TARGET2} ${TARGET3} ${TARGET4} ${TARGET5} ${TARGET6} ${sbindir}

clean:
	rm -f *.o ${TARGET1} ${TARGET2} ${TARGET3} ${TARGET4} ${TARGET5} ${TARGET6}

.PHONY: all ${TARGET1} ${TARGET2} ${TARGET3} ${TARGET4} ${TARGET5} ${TARGET6}
