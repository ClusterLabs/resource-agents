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

include ../../../make/defines.mk
include $(OBJDIR)/make/clean.mk

TARGETS= mmdd sfdd

CFLAGS += -D_GNU_SOURCE

all: $(TARGETS)

mmdd.o: mmdd.c
	$(CC) $(CFLAGS) -D_FILE_OFFSET_BITS=64 -c -o $@ $<

sfdd.o: mmdd.c
	$(CC) $(CFLAGS) -DUSE_SENDFILE -c -o $@ $<

%: %.o
	$(CC) -o $@ $^ $(LDFLAGS)

clean: generalclean
