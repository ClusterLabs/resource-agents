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

TARGETS=clubufflush clufindhostname clustat clusvcadm clulog clunfslock
DISABLEDTARGETS=cluarp

CFLAGS += -DPACKAGE_VERSION=\"${RELEASE}\"
CFLAGS += -g -Werror -Wstrict-prototypes -Wshadow -fPIC -D_GNU_SOURCE
CFLAGS += -I${incdir} -I${ccsincdir} -I${cmanincdir} -I${dlmincdir} -I${ncursesincdir}
CFLAGS += -I../../include

LDFLAGS += -L${libdir} -L${ccslibdir} -L${cmanlibdir} -L${ncurseslibdir}
LDFLAGS += -lcman -lpthread -ldl -lncurses -lccs
LDFLAGS += -L../clulib -lclulib

all: ${TARGETS}

%: %.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clunfslock: clunfslock.sh
	cp clunfslock.sh clunfslock
	chmod 755 clunfslock

install: all
	install -d ${sbindir}
	install $(TARGETS) ${sbindir}

uninstall:
	${UNINSTALL} $(TARGETS) ${sbindir}

clean:
	rm -f *.o $(TARGETS)
