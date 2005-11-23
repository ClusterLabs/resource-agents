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


PEGASUS_CXXFLAGS = -DPEGASUS_PLATFORM_LINUX_IX86_GNU
PEGASUS_PROVIDERS_DIR = /usr/lib/Pegasus/providers/


CXXFLAGS += $(PEGASUS_CXXFLAGS)

#LDFLAGS += -fPIC -shared -ldl -lpthread -lcrypt \
#	-L${top_srcdir}/src/common -lClusterMonitor `xml2-config --libs`
LDFLAGS += -fPIC -shared -ldl -lpthread -lcrypt \
	${top_srcdir}/src/common/*.o `xml2-config --libs`

OBJECTS = ClusterProviderMain.o ClusterProvider.o

TARGET = libRedHatClusterProvider.so
TARGET_TEST = clusterCIM_test



all: ${TARGET} $(TARGET_TEST)

install: 
	install ${TARGET} ${PEGASUS_PROVIDERS_DIR}

uninstall: 
	${UNINSTALL} ${TARGET} ${PEGASUS_PROVIDERS_DIR}

clean:
	rm -f $(OBJECTS) $(TARGET) $(TARGET_TEST)

check: 

rebuild: clean all


$(TARGET): $(OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ $(OBJECTS)

$(TARGET_TEST): clusterCIM_test.*
	$(CXX) -Wall -Wno-unused ${INCLUDE} $(PEGASUS_CXXFLAGS) -lpegcommon -lpegclient -lpthread -lcrypt -o $@ $@.cpp 

*.o: *.h $(INCLUDE_DIR)/*.h
