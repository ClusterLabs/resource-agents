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

INCLUDE = -I${top_srcdir}/include -I${cmankincdir} -I${ccsincdir} -I${incdir} -I${top_srcdir}/config

ifneq (${KERNEL_SRC}, )
# Use the kernel tree if patched, otherwise, look where cluster headers
#  should be installed
INCLUDE += $(shell if [ -d ${KERNEL_SRC}/include/cluster ]; then \
		echo '-I${KERNEL_SRC}/include/cluster'; else \
		echo '-I${incdir}/cluster'; fi)
else
INCLUDE += -I${incdir}/cluster
endif

LDFLAGS+= -L${ccslibdir} -L${libdir} 
LOADLIBES+= -lccs

FENCE_TOOL_SRC = \
	agent.c \
	fence_tool.c

all: ${TARGET}

fence_tool: ${FENCE_TOOL_SRC:.c=.o}
	${CC} ${CFLAGS} ${INCLUDE} ${FENCE_TOOL_SRC:.c=.o} ${LDFLAGS} ${LOADLIBES} ${LDLIBS} -o $@

agent.c:
	ln -s ${top_srcdir}/fenced/agent.c .

copytobin: all
	strip ${TARGET}
	cp ${TARGET} ${top_srcdir}/bin

clean:
	rm -rf *~ *.o ${TARGET} agent.c
