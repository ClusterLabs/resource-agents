#ifndef ClusterMonitor_h
#define ClusterMonitor_h

#include <pthread.h>

#include <string>
#include <vector>

#include "Cluster.h"
#include "counting_auto_ptr.h"


namespace ClusterMonitoring
{

class ClusterMonitor
{
 public:
  ClusterMonitor(std::string socket_path);
  virtual ~ClusterMonitor();
  
  counting_auto_ptr<Cluster> getCluster();
  
  void print();
  
 private:
  
  counting_auto_ptr<Cluster> buildCluster(std::vector<std::string>& commands);
  
  std::string socket_path;
  int sock;
  
  void openSocket(int num);
  void closeSocket();
  bool checkSocket();
  bool send(std::string msg);
  std::string receive(unsigned int wait);
  
  pthread_mutex_t mutex;
  
};


// helper function
inline counting_auto_ptr<Cluster>
  getCluster(std::string socket_path)
  {
    ClusterMonitor mon(socket_path);
    return mon.getCluster();
  }

};

#endif
