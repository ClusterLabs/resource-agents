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

include ../../make/defines.mk

TARGET= gfs_controld

OBJS= 	main.o \
	member_cman.o \
	cpg.o \
	group.o \
	plock.o \
	recover.o

CFLAGS += -g -O2
CFLAGS += -I${cmanincdir} -I${incdir}
CFLAGS += -idirafter ${KERNEL_SRC}/include/linux
CFLAGS += -I../include/ -I../lib/

LDFLAGS += -L${cmanlibdir} -L${openaislibdir} -L${libdir} -lcman -lcpg -lSaCkpt
LDFLAGS += -L../lib -lgroup

all: depends ${TARGET}

${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)


%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

depends:
	$(MAKE) -C ../lib all

install: all
	install -d ${sbindir}
	install ${TARGET} ${sbindir}

uninstall:
	${UNINSTALL} ${TARGET} ${sbindir}

clean:
	rm -f *.o ${TARGET}

.PHONY: all ${TARGET}
