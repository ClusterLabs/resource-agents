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
include $(OBJDIR)/make/cobj.mk

TARGET= dlm_controld

OBJS=	main.o \
	member_cman.o \
	group.o \
	action.o \
	deadlock.o

CFLAGS += -I${ccsincdir} -I${cmanincdir} -I${dlmincdir} -I${openaisincdir}
CFLAGS += -I${KERNEL_SRC}/include/
CFLAGS += -I$(S)/../lib/ -I$(S)/../include/
CFLAGS += -I${incdir}

LDFLAGS += -L${ccslibdir} -L${cmanlibdir} -L${dlmlibdir} -lccs -lcman -ldlm
LDFLAGS += -L${openaislibdir} -lcpg -lSaCkpt
LDFLAGS += -L../lib -lgroup

all: depends ${TARGET}

${TARGET}: ${OBJS}
	$(CC) -o $@ $^ $(LDFLAGS)

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
