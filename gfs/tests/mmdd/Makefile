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

CC=gcc
LD=gcc

TARGETS=mmdd sfdd

all:			$(TARGETS)


mmdd:			mmdd.c
			$(CC) -Wall -O2 -o mmdd mmdd.c  -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64

sfdd:	    	    	mmdd.c
			$(CC) -Wall -O2 -o sfdd mmdd.c  -D_GNU_SOURCE -DUSE_SENDFILE

clean:
			rm -f *.o *.s *.a *~ core .depend $(TARGETS)
