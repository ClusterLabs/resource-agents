
PEGASUS_CXXFLAGS = -DPEGASUS_PLATFORM_LINUX_IX86_GNU

CXX = g++ 
CXXFLAGS = -O2 -Wall -Wno-unused -I../include/ $(PEGASUS_CXXFLAGS)

LDFLAGS = -fPIC -shared -ldl -lpthread -lcrypt ../common/*.o `xml2-config --libs`

OBJECTS = ClusterProviderMain.o ClusterProvider.o
TARGET = libRedHatClusterProvider.so

TARGET_TEST = clusterCIM_test


$(TARGET): $(OBJECTS) 
	$(CXX) $(LDFLAGS) -o $@ $(OBJECTS)

*.o: *.h

all: $(TARGET) $(TARGET_TEST)

clean: 
	rm -f *\.o  $(TARGET) $(TARGET_TEST)

rebuild: clean all

$(TARGET_TEST): clusterCIM_test.*
	$(CXX) $(CXXFLAGS) -lpegcommon -lpegclient -lpthread -lcrypt -o $@ $@.cpp 
