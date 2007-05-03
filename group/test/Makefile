###############################################################################
###############################################################################
##
##  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
##
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
###############################################################################
###############################################################################

CFLAGS+= -g -I. 

all: client clientd

client: client.o
	$(CC) $(LDFLAGS) -o $@ $<

clientd: clientd.o
	$(CC) $(LDFLAGS) -o $@ $<

client.o: client.c
	$(CC) $(CFLAGS) -c -o $@ $<

clientd.o: clientd.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o client clientd

