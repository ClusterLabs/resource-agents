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

ifeq ($(arch),i386)
PEGASUS_CXXFLAGS = -DPEGASUS_PLATFORM_LINUX_IX86_GNU
endif
ifeq ($(arch),i586)
PEGASUS_CXXFLAGS = -DPEGASUS_PLATFORM_LINUX_IX86_GNU
endif
ifeq ($(arch),i686)
PEGASUS_CXXFLAGS = -DPEGASUS_PLATFORM_LINUX_IX86_GNU
endif
ifeq ($(arch),athlon)
PEGASUS_CXXFLAGS = -DPEGASUS_PLATFORM_LINUX_IX86_GNU
endif
ifeq ($(arch),ia64)
PEGASUS_CXXFLAGS = -DPEGASUS_PLATFORM_LINUX_IA64_GNU
endif
ifeq ($(arch),x86_64)
PEGASUS_CXXFLAGS = -DPEGASUS_PLATFORM_LINUX_IA64_GNU
endif

ifeq ($(arch),ppc)
PEGASUS_CXXFLAGS = -DPEGASUS_PLATFORM_LINUX_PPC_GNU
endif
ifeq ($(arch),ppc64)
PEGASUS_CXXFLAGS = -DPEGASUS_PLATFORM_LINUX_PPC64_GNU
endif

ifeq ($(arch),s390)
PEGASUS_CXXFLAGS = -DPEGASUS_PLATFORM_LINUX_ZSERIES_GNU
endif
ifeq ($(arch),s390x)
PEGASUS_CXXFLAGS = -DPEGASUS_PLATFORM_LINUX_ZSERIES64_GNU
endif



CXXFLAGS += $(PEGASUS_CXXFLAGS)

#LDFLAGS += -fPIC -shared -ldl -lpthread -lcrypt \
#	-L${top_srcdir}/src/common -lClusterMonitor `xml2-config --libs`
LDFLAGS += -fPIC -shared -ldl -lpthread -lcrypt \
	${top_srcdir}/src/common/*.o `xml2-config --libs`

OBJECTS = ClusterProviderMain.o ClusterProvider.o

TARGET = libRedHatClusterProvider.so
TARGET_TEST = clusterCIM_test



all: ${TARGET} $(TARGET_TEST)

install: all
	install -d ${pegasus_providers_dir}
	install ${TARGET} ${pegasus_providers_dir}

uninstall: 
	${UNINSTALL} ${TARGET} ${pegasus_providers_dir}

clean:
	rm -f $(OBJECTS) $(TARGET) $(TARGET_TEST)

check: 

rebuild: clean all


$(TARGET): $(OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ $(OBJECTS)

$(TARGET_TEST): clusterCIM_test.*
	$(CXX) -Wall -Wno-unused ${INCLUDE} $(PEGASUS_CXXFLAGS) -lpegcommon -lpegclient -lpthread -lcrypt -o $@ $@.cpp 

*.o: *.h $(INCLUDE_DIR)/*.h
