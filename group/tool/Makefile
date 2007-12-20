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

TARGET= group_tool

OBJS=	main.o

CFLAGS += -I$(S) -I$(S)/../daemon/ -I$(S)/../lib/
CFLAGS += -I${incdir}

LDFLAGS += -L../lib -lgroup

all: depends ${TARGET}

${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: $(S)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

depends:
	$(MAKE) -C ../lib all

clean:
	rm -f *.o ${TARGET}

install: all
	install -d ${sbindir}
	install ${TARGET} ${sbindir}

uninstall:
	${UNINSTALL} ${TARGET} ${sbindir}

.PHONY: all ${TARGET}
