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

include ../../make/defines.mk

TARGET= gfs2_quota

OBJS=	main.o \
	names.o \
	check.o

CFLAGS += -DHELPER_PROGRAM -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE
CFLAGS += -I${KERNEL_SRC}/fs/gfs2/ -I${KERNEL_SRC}/include/
CFLAGS += -I$(S)/../include -I$(S)/../libgfs2
CFLAGS += -I${incdir}

LDFLAGS += -L../libgfs2 -lgfs2

all: depends ${TARGET}

${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: $(S)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

depends:
	$(MAKE) -C ../libgfs2 all

install: all
	install -m 0755 ${TARGET} ${sbindir}

uninstall:
	${UNINSTALL} ${TARGET} ${sbindir}

clean:
	rm -f *.o ${TARGET}

.PHONY: all ${TARGET}
