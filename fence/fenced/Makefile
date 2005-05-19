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
TARGET = fenced

top_srcdir=..
include ${top_srcdir}/make/defines.mk
UNINSTALL=${top_srcdir}/make/uninstall.pl

CFLAGS+= -g -DFENCE_RELEASE_NAME=\"${RELEASE}\" \
	-I. \
	-I${top_srcdir}/include \
	-I${incdir} \
	-I${top_srcdir}/config \
	-I${top_srcdir}/../group/lib \
	-I${top_srcdir}/../cman/lib

all: ${TARGET}

${TARGET}: \
	main.o \
	recover.o \
	group.o \
	member_cman.o \
	agent.o \
	../../ccs/lib/libccs.a \
	../../cman/lib/libcman.a \
	../../group/lib/libgroup.a
	$(CC) -o $@ $^

main.o: main.c fd.h
	$(CC) $(CFLAGS) -c -o $@ $<

recover.o: recover.c fd.h
	$(CC) $(CFLAGS) -c -o $@ $<

group.o: group.c fd.h
	$(CC) $(CFLAGS) -c -o $@ $<

member_cman.o: member_cman.c fd.h
	$(CC) $(CFLAGS) -c -o $@ $<

agent.o: agent.c fd.h
	$(CC) $(CFLAGS) -c -o $@ $<

copytobin: all
	strip ${TARGET}
	cp ${TARGET} ${top_srcdir}/bin

clean:
	rm -rf *~ *.o ${TARGET}
