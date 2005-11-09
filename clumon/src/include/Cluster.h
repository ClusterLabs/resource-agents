#ifndef Cluster_h
#define Cluster_h


#include <list>
#include <string>


namespace ClusterMonitoring
{


class Node;
class Service;


class Cluster
{
 public:
  Cluster(const std::string &name, unsigned int minQuorum);
  virtual ~Cluster();
  
  const std::string name;
  
  unsigned int getVotes();
  unsigned int getMinQuorum();
  
  Node& addNode(std::string name, 
		unsigned int votes, 
		bool online,
		bool clustered);
  std::list<Node>& getNodes();
  std::list<Node*> getClusteredNodes();
  std::list<Node*> getUnclusteredNodes();
  
  Service& addService(std::string name, 
		      std::string nodeName,
		      bool failed,
		      bool autostart);
  std::list<Service>& getServices();
  std::list<Service*> getRunningServices();
  std::list<Service*> getStoppedServices();
  std::list<Service*> getFailedServices();
  
  bool quorumed();
  
  
 private:
  unsigned int minQuorum;
  std::list<Node> nodes;
  std::list<Service> services;
};



class Node
{
 public:
  Node(Cluster* cluster, 
       const std::string &name, 
       unsigned int votes,
       bool online,
       bool clustered);
  virtual ~Node();
  
  const Cluster* cluster;
  const std::string name;
  const unsigned int votes;
  const bool online;
  const bool clustered; // available to cluster
  
  void addService(Service* service);
  std::list<Service*> getServices();
  
 private:
  std::list<Service*> services;
  
};



class Service
{
 public:
  Service(Cluster *cluster, 
	  const std::string &name,
	  Node* node,
	  bool failed,
	  bool autostart);
  virtual ~Service();
  
  const Cluster *cluster;
  const std::string name;
  
  bool running();
  Node* getNode();
  
  bool failed();
  bool autostart();
  
 private:
  Node* _node;
  bool _autostart;
  bool _failed;
  
};


}; // namespace ClusterMonitoring


#endif
