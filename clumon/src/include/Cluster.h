/*
  Copyright Red Hat, Inc. 2005

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation; either version 2, or (at your option) any
  later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; see the file COPYING.  If not, write to the
  Free Software Foundation, Inc.,  675 Mass Ave, Cambridge, 
  MA 02139, USA.
*/
/*
 * Author: Stanko Kupcevic <kupcevic@redhat.com>
 */


#ifndef Cluster_h
#define Cluster_h

#include <counting_auto_ptr.h>
#include <string>
#include <map>
#include <list>

#include "XML.h"


namespace ClusterMonitoring 
{


class Node;
class Service;
class Cluster;


std::string cluster2xml(Cluster& cluster);
counting_auto_ptr<Cluster> xml2cluster(const std::string& xml);


class Cluster
{
 public:
  Cluster(const std::string& name, unsigned int minQuorum=0);
  virtual ~Cluster();
  
  std::string name();
  unsigned int votes();
  unsigned int minQuorum();
  bool quorate();
  
  counting_auto_ptr<Node> addNode(const std::string& name, 
				  unsigned int votes, 
				  bool online, 
				  bool clustered);
  
  counting_auto_ptr<Service> addService(const std::string& name, 
					const std::string& nodeName, 
					bool failed, 
					bool autostart);
  
  std::list<counting_auto_ptr<Node> > nodes();
  std::list<counting_auto_ptr<Node> > clusteredNodes();
  std::list<counting_auto_ptr<Node> > unclusteredNodes();
  
  std::list<counting_auto_ptr<Service> > services();
  std::list<counting_auto_ptr<Service> > runningServices();
  std::list<counting_auto_ptr<Service> > stoppedServices();
  std::list<counting_auto_ptr<Service> > failedServices();
  
 private:
  std::string _name;
  unsigned int _minQuorum;
  std::map<std::string, counting_auto_ptr<Node> > _nodes;

};


class Node
{
 public:
  Node(const std::string& name, 
       const std::string& clustername,
       unsigned int votes,
       bool online,
       bool clustered);
  virtual ~Node();
  
  std::string name() const;
  std::string clustername() const;
  unsigned int votes() const;
  bool online() const;
  bool clustered() const;  // available to cluster
  
  counting_auto_ptr<Service> addService(const std::string& name,
					bool failed,
					bool autostart);
  std::list<counting_auto_ptr<Service> > services();
  
 private:
  std::string _name;
  std::string _clustername;
  unsigned int _votes;
  bool _online;
  bool _clustered;  // available to cluster
  
  std::map<std::string, counting_auto_ptr<Service> > _services;
  
};


class Service
{
 public:
  Service(const std::string& name,
	  const std::string& clustername,
	  const Node& node,
	  bool failed,
	  bool autostart);
  virtual ~Service();
  
  std::string name() const;
  std::string clustername() const;
  bool running() const;
  std::string nodename() const;
  bool failed() const;
  bool autostart() const;
  
 private:
  std::string _name;
  std::string _clustername;
  std::string _nodename;
  bool _autostart;
  bool _failed;
  
};


};  // namespace ClusterMonitoring 


#endif
