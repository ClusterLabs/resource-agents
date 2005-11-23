
CXX = g++ 
CXXFLAGS = -c -O2 -Wall -Wno-unused -I../include/ `xml2-config --cflags`

OBJECTS = ClusterMonitor.o \
	Cluster.o Node.o Service.o \
	Socket.o ClientSocket.o ServerSocket.o \
	Thread.o \
	XML.o

TARGET = libClusterMonitor.a


$(TARGET): $(OBJECTS) 
	ar rc $(TARGET) $(OBJECTS)
	ranlib $(TARGET)

#*.o: *.h

all: $(TARGET) 

rebuild: clean all

clean: 
	rm -f $(OBJECTS) $(TARGET)
