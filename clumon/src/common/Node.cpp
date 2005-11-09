#include "Cluster.h"

using namespace std;
using namespace ClusterMonitoring;


Node::Node(Cluster* cluster, 
	   const string &name, 
	   unsigned int votes,
	   bool online,
	   bool clustered) : 
  cluster(cluster), 
  name(name),
  votes(votes),
  online(online),
  clustered(clustered)
{
  
}

Node::~Node(void)
{
  
}


void 
Node::addService(Service* service)
{
  services.push_back(service);
}

list<Service*>
Node::getServices()
{
  return services;
}
