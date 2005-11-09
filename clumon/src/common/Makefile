
CXX = g++ 
CXXFLAGS = -c -O2 -Wall -Wno-unused -I../include/

OBJECTS = ClusterMonitor.o Cluster.o Node.o Service.o 
TARGET = libClusterMonitor.a


$(TARGET): $(OBJECTS) 
	ar rc $(TARGET) $(OBJECTS)
	ranlib $(TARGET)

#*.o: *.h

all: $(TARGET) 

rebuild: clean all

clean: 
	rm -f $(OBJECTS) $(TARGET)
