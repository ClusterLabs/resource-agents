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


TARGETS=filecon2_server filecon2_client



all:			$(TARGETS)


filecon2_server:	filecon2_server.c
			$(CC) -O3 -Wall -o filecon2_server filecon2_server.c -I../../include -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64

filecon2_client:	filecon2_client.c
			$(CC) -O3 -Wall -o filecon2_client filecon2_client.c -I../../include -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64


clean:
			rm -f *.o *.s *.a *~ core .depend $(TARGETS)

