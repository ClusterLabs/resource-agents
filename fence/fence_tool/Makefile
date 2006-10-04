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

TARGET = fence_tool

top_srcdir=..
include ${top_srcdir}/make/defines.mk
UNINSTALL=${top_srcdir}/make/uninstall.pl

ifeq ($(DEBUG),y)
CFLAGS+= -O2 -D_FILE_OFFSET_BITS=64 -DDEBUG -g -DFENCE_RELEASE_NAME=\"${RELEASE}\"
else
CFLAGS+= -O2 -D_FILE_OFFSET_BITS=64 -DFENCE_RELEASE_NAME=\"${RELEASE}\"
endif

INCLUDE = -I${top_srcdir}/include -I${ccsincdir} -I${cmanincdir} -I${incdir} -I${top_srcdir}/config -I${top_srcdir}/../group/lib

LDFLAGS+= -L${ccslibdir} -L${libdir} -L${top_srcdir}/../cman/lib -L${top_srcdir}/../group/lib
LOADLIBES+= -lccs -lcman -lgroup

FENCE_TOOL_SRC = \
	agent.c \
	fence_tool.c

all: ${TARGET}

fence_tool: ${FENCE_TOOL_SRC:.c=.o}
	${CC} ${CFLAGS} ${INCLUDE} ${FENCE_TOOL_SRC:.c=.o} ${LDFLAGS} ${LOADLIBES} ${LDLIBS} -o $@

agent.c:
	ln -s ${top_srcdir}/fenced/agent.c .

install: all
	if [ ! -d ${sbindir} ]; then \
		install -d ${sbindir}; \
	fi
	strip ${TARGET}
	install -m755 ${TARGET} ${sbindir}

clean:
	rm -rf *~ *.o ${TARGET} agent.c
