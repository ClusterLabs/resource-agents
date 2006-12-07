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
top_srcdir = ../../..
UNINSTALL=${top_srcdir}/scripts/uninstall.pl

BINARIES=dlmtest asttest lstest pingtest lvb dlmtest2

all: $(BINARIES)

CFLAGS+=-I${top_srcdir}/dlm/lib -L${top_srcdir}/dlm/lib -g -D_REENTRANT

ifneq (${KERNEL_SRC}, )
CFLAGS += -I${KERNEL_SRC}/include/cluster
else
CFLAGS += -I/usr/include/linux/cluster
endif
CFLAGS += -I${top_srcdir}/lib/dlm


dlmtest: dlmtest.c
	$(CC) $(CFLAGS) -o $@ $< -ldlm -lpthread

dlmtest2: dlmtest2.c
	$(CC) $(CFLAGS) -o $@ $< -ldlm -lpthread

asttest: asttest.c
	$(CC) $(CFLAGS) -o $@ $< -ldlm -lpthread

lstest: lstest.c
	$(CC) $(CFLAGS) -o $@ $< -ldlm -lpthread

pingtest: pingtest.c	
	$(CC) $(CFLAGS) -o $@ $< -ldlm -lpthread

lvb: lvb.c	
	$(CC) $(CFLAGS) -o $@ $< -ldlm -lpthread

clean:
	rm -f *.o $(BINARIES) *~ core
