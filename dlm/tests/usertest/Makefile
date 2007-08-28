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


TARGETS= dlmtest asttest lstest pingtest lvb dlmtest2 flood alternate-lvb joinleave threads

all: depends ${TARGETS}

CFLAGS += -D_REENTRANT
CFLAGS += -g
CFLAGS += -I${dlmincdir}

LDFLAGS += -L${dlmlibdir} -ldlm -lpthread

depends:
	$(MAKE) -C ../../lib all

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%: %.o
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	rm -f *.o ${TARGETS} *~ core
