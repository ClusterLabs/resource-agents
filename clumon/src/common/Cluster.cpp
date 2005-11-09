#include "Cluster.h"

using namespace std;
using namespace ClusterMonitoring;



Cluster::Cluster(const string &name, unsigned int minQuorum) : 
  name(name), 
  minQuorum(minQuorum)
{
  
}

Cluster::~Cluster(void)
{
  
}


unsigned int 
Cluster::getVotes()
{
  list<Node*> availNodes = getClusteredNodes();
  list<Node*>::iterator iter = availNodes.begin();
  unsigned int votes = 0;
  for( ; iter != availNodes.end(); iter++)
    votes += (*iter)->votes;
  return votes;
}

unsigned int 
Cluster::getMinQuorum()
{
  return minQuorum;
}

Node&
Cluster::addNode(string name,
		 unsigned int votes,
		 bool online,
		 bool clustered)
{
  nodes.push_back(Node(this, name, votes, online, clustered));
  return nodes.back();
}

list<Node>&
Cluster::getNodes()
{
  return nodes;
}

list<Node*> 
Cluster::getClusteredNodes()
{
  list<Node*> ret;
  list<Node>::iterator iter = nodes.begin();
  for( ; iter != nodes.end(); iter++)
    if(iter->clustered)
      ret.push_back(&(*iter));
  return ret;
}

list<Node*> 
Cluster::getUnclusteredNodes()
{
  list<Node*> ret;
  list<Node>::iterator iter = nodes.begin();
  for( ; iter != nodes.end(); iter++)
    if( ! iter->clustered)
      ret.push_back(&(*iter));
  return ret;
}
  
Service&
Cluster::addService(string name,
		    string nodeName,
		    bool failed,
		    bool autostart)
{
  Node* node = NULL;
  list<Node>::iterator iter = nodes.begin();
  for( ; iter != nodes.end(); iter++)
    if(iter->name == nodeName)
      node = &(*iter);
  
  services.push_back(Service(this, name, node, failed, autostart));
  Service& service = services.back();
  if(node)
    node->addService(&service);
  
  return service;
}

list<Service>& 
Cluster::getServices()
{
  return services;
}

list<Service*> 
Cluster::getRunningServices()
{
  list<Service*> ret;
  list<Service>::iterator iter = services.begin();
  for( ; iter != services.end(); iter++)
    if(iter->running())
      ret.push_back(&(*iter));
  return ret;
}

list<Service*> 
Cluster::getStoppedServices()
{
  list<Service*> ret;
  list<Service>::iterator iter = services.begin();
  for( ; iter != services.end(); iter++)
    if(!iter->running())
      ret.push_back(&(*iter));
  return ret;
}

list<Service*> 
Cluster::getFailedServices()
{
  list<Service*> ret;
  list<Service>::iterator iter = services.begin();
  for( ; iter != services.end(); iter++)
    if(iter->failed())
      ret.push_back(&(*iter));
  return ret;
}
  
bool 
Cluster::quorumed()
{
  return getVotes() >= getMinQuorum();
}
