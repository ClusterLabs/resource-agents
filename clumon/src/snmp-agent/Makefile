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


SNMP_CFLAGS = `net-snmp-config --cflags`
CXXFLAGS += $(SNMP_CFLAGS)


#LDFLAGS += -fPIC -shared -lpthread \
#	-L${top_srcdir}/src/common -lClusterMonitor \
#	`net-snmp-config --libs` \
#	`xml2-config --libs`

LDFLAGS += -fPIC -shared -lpthread \
	${top_srcdir}/src/common/*.o \
	`net-snmp-config --libs` \
	`xml2-config --libs`


OBJECTS = clusterMonitorSnmp.o \
	clusterMIB.o \
	nodesMIB.o nodesMIB_access.o nodesMIB_checkfns.o nodesMIB_checkfns_local.o \
	servicesMIB.o servicesMIB_access.o servicesMIB_checkfns.o servicesMIB_checkfns_local.o

TARGET = libClusterMonitorSnmp.so



all: ${TARGET}

install: 
	install -d ${libdir}/clumon-snmp
	install ${TARGET} ${libdir}/clumon-snmp

uninstall: 
	${UNINSTALL} ${TARGET} ${libdir}/clumon-snmp

clean:
	rm -f $(OBJECTS) $(TARGET)

check: 

rebuild: clean all


$(TARGET): $(OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ $(OBJECTS)

*.o: *.h $(INCLUDE_DIR)/*.h
