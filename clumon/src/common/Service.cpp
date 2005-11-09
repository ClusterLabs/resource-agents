#include "Cluster.h"

using namespace std;
using namespace ClusterMonitoring;


Service::Service(Cluster *cluster, 
		 const string &name,
		 Node* node,
		 bool failed,
		 bool autostart) : 
  cluster(cluster), 
  name(name),
  _node(node),
  _autostart(autostart),
  _failed(failed)
{
  
}

Service::~Service(void)
{
  
}


bool 
Service::running()
{
  return _node != NULL;
}
Node*
Service::getNode()
{
  return _node;
}

bool 
Service::failed()
{
  return _failed;
}

bool 
Service::autostart()
{
  return _autostart;
}
