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

TARGET = fenced

OBJS=	main.o \
	recover.o \
	group.o \
	member_cman.o \
	agent.o 

CFLAGS += -D_FILE_OFFSET_BITS=64
CFLAGS += -I${ccsincdir} -I${cmanincdir}
CFLAGS += -I$(S) -I$(S)/../include -I$(SRCDIR)/group/lib
CFLAGS += -I${incdir}

LDFLAGS += -L${ccslibdir} -L${cmanlibdir} -lccs -lcman
LDFLAGS += -L../../group/lib -l group

all: ${TARGET}

${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: $(S)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

install: all
	if [ ! -d ${sbindir} ]; then \
		install -d ${sbindir}; \
	fi
	install -m755 ${TARGET} ${sbindir}

uninstall:
	${UNINSTALL} ${TARGET} ${sbindir}

clean:
	rm -rf *~ *.o ${TARGET}
