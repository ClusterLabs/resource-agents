##############################################################################
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

TARGETS= mmdd sfdd

all: $(TARGETS)

include ../../../make/defines.mk
include $(OBJDIR)/make/clean.mk

CFLAGS += -D_GNU_SOURCE

mmdd.o: mmdd.c
	$(CC) $(CFLAGS) -D_FILE_OFFSET_BITS=64 -c -o $@ $<

sfdd.o: mmdd.c
	$(CC) $(CFLAGS) -DUSE_SENDFILE -c -o $@ $<

%: %.o
	$(CC) -o $@ $^ $(LDFLAGS)

clean: generalclean

-include mmdd.d
-include sfdd.d
