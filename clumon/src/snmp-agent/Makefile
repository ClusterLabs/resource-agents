
SNMP_CFLAGS = `net-snmp-config --cflags`
# AGENTLIBS=`net-snmp-config --agent-libs`

CXX = g++
CXXFLAGS = -O2 -Wall -Wno-unused -I../include/ $(SNMP_CFLAGS)

LDFLAGS = -fPIC -shared -lpthread ../common/*.o `xml2-config --libs`

OBJECTS = clusterMonitorSnmp.o \
	clusterMIB.o \
	nodesMIB.o nodesMIB_access.o nodesMIB_checkfns.o nodesMIB_checkfns_local.o \
	servicesMIB.o servicesMIB_access.o servicesMIB_checkfns.o servicesMIB_checkfns_local.o

TARGET = libClusterMonitorSnmp.so


$(TARGET): $(OBJECTS)
	$(CXX) $(LDFLAGS) -o $@  $(OBJECTS)

*.o: *.h 

all: $(TARGET)

clean:
	rm -f $(TARGET) $(OBJECTS)

rebuild: clean all
