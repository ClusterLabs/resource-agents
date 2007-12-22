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

TARGET1= clubufflush
TARGET2= clufindhostname
TARGET3= clustat
TARGET4= clusvcadm
TARGET5= clulog
TARGET6= clunfslock

SBINDIRT=$(TARGET1) $(TARGET2) $(TARGET3) $(TARGET4) $(TARGET5) $(TARGET6)

include ../../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

DISABLEDTARGETS= cluarp

CFLAGS += -Werror -Wstrict-prototypes -Wshadow -fPIC -D_GNU_SOURCE
CFLAGS += -I${ccsincdir} -I${cmanincdir} -I${dlmincdir} -I${ncursesincdir}
CFLAGS += -I$(S)/../../include
CFLAGS += -I${incdir}

LDFLAGS += -L${libdir} -L${ccslibdir} -L${cmanlibdir} -L${ncurseslibdir}
LDFLAGS += -lcman -lpthread -ldl -lncurses -lccs
LDFLAGS += -L../clulib -lclulib

all: depends ${TARGET1} ${TARGET2} ${TARGET3} ${TARGET4} ${TARGET5} ${TARGET6}

${TARGET1}: ${TARGET1:=.o}
	$(CC) -o $@ $^ $(LDFLAGS)

${TARGET2}: ${TARGET2:=.o}
	$(CC) -o $@ $^ $(LDFLAGS)

${TARGET3}: ${TARGET3:=.o}
	$(CC) -o $@ $^ $(LDFLAGS)

${TARGET4}: ${TARGET4:=.o}
	$(CC) -o $@ $^ $(LDFLAGS)

${TARGET5}: ${TARGET5:=.o}
	$(CC) -o $@ $^ $(LDFLAGS)

${TARGET6}:
	cp $(S)/${TARGET6}.sh ${TARGET6}
	chmod 755 ${TARGET6}

depends:
	$(MAKE) -C ../clulib all

clean: generalclean

.PHONY: all ${TARGET1} ${TARGET2} ${TARGET3} ${TARGET4} ${TARGET5} ${TARGET6}
