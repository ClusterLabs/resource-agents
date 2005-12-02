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

include ${top_srcdir}/make/defines.mk
INCLUDE += -I $(top_srcdir)/src/include `xml2-config --cflags`


TARGET = libClusterMonitor.a

OBJECTS = ClusterMonitor.o \
	Cluster.o Node.o Service.o \
	Socket.o ClientSocket.o ServerSocket.o \
	Thread.o \
	XML.o \
	Logger.o \
	Time.o


all: ${TARGET}

install: 

uninstall: 

clean:
	rm -f $(OBJECTS) $(TARGET)

check: 

rebuild: clean all


$(TARGET): $(OBJECTS) 
	$(AR) rc $(TARGET) $(OBJECTS)
	$(RANLIB) $(TARGET)
