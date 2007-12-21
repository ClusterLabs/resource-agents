###############################################################################
###############################################################################
##
##  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
##  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
##
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

include ../../../make/defines.mk
include $(OBJDIR)/make/cobj.mk

TARGETS= filecon2_server filecon2_client

CFLAGS += -O3
CFLAGS += -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64
CFLAGS += -I../../include

all: $(TARGETS)

%: %.o
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	rm -f *.o *.s *.a *~ core .depend $(TARGETS)

