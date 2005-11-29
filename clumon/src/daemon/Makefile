################################################################################################################################################################
##
##  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
##
##  This copyrighted material is made available to anyone wishing to use,
##  modify, copy, or redistribute it subject to the terms and conditions
##  of the GNU General Public License v.2.
##
##############################################################################################################################################################

top_srcdir=../..
UNINSTALL = ${top_srcdir}/scripts/uninstall.pl

INCLUDE_DIR = $(top_srcdir)/src/include

include ${top_srcdir}/make/defines.mk
INCLUDE += -I ${INCLUDE_DIR}

#LDFLAGS += -lpthread -L${top_srcdir}/src/common/ -lClusterMonitor `xml2-config --libs`
LDFLAGS += -lpthread ${top_srcdir}/src/common/*.o `xml2-config --libs`

TARGET = clumond

OBJECTS = main.o \
	daemon_init.o signals.o executils.o \
	Monitor.o Peer.o Communicator.o



all: ${TARGET}

install: 
	install -d ${sbindir}
	install ${TARGET} ${sbindir}

uninstall: 
	${UNINSTALL} ${TARGET} ${sbindir}

clean:
	rm -f $(OBJECTS) $(TARGET)

check: 

rebuild: clean all


$(TARGET): $(OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ $(OBJECTS)

*.o: *.h $(INCLUDE_DIR)/*.h
