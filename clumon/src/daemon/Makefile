
INCLUDE_DIR = ../include/

CXX = g++ 
CXXFLAGS = -O2 -Wall -Wno-unused -I$(INCLUDE_DIR)

LDFLAGS = -lpthread ../common/*.o `xml2-config --libs`

OBJECTS = daemon_init.o executils.o \
	Monitor.o Peer.o Communicator.o

OBJECTS_ALL = main.o $(OBJECTS)

TARGET = clumond
TARGET_TEST = clumond_test


$(TARGET): $(OBJECTS_ALL) 
	$(CXX) $(LDFLAGS) -o $@ $(OBJECTS_ALL)

*.o: *.h $(INCLUDE_DIR)/*.h

all: $(TARGET) $(TARGET_TEST)

clean: 
	rm -f *\.o  $(TARGET) $(TARGET_TEST)

rebuild: clean all

$(TARGET_TEST): $(OBJECTS) $(TARGET_TEST).*
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(OBJECTS) $(TARGET_TEST).cpp
