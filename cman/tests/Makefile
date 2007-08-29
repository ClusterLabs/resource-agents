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

include ../../make/defines.mk

TARGETS= client libtest sysman sysmand

CFLAGS += -I${cmanincdir}
CFLAGS += -I${incdir}

LDFLAGS += -L${cmanlibdir} -lcman

all: depends ${TARGETS}

depends:
	$(MAKE) -C ../lib all

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%: %.o
	$(CC) -o $@ $^ $(LDFLAGS)

install: all

clean:
	rm -f *.o ${TARGETS}
