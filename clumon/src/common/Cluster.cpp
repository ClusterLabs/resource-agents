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


#include "Cluster.h"

#include <stdio.h>

using namespace std;
using namespace ClusterMonitoring;


Cluster::Cluster(const string &name, unsigned int minQuorum) : 
  _name(name), 
  _minQuorum(minQuorum)
{
  // add no-node node
  addNode("", 0, false, false);
}

Cluster::~Cluster(void)
{}



std::string 
Cluster::name()
{
  return _name;
}

unsigned int 
Cluster::votes()
{
  unsigned int votes = 0;
  for (map<string, counting_auto_ptr<Node> >::iterator iter = _nodes.begin();
       iter != _nodes.end();
       iter++) {
    Node& node = *(iter->second);
    if (node.clustered())
      votes += node.votes();
  }
  return votes;
}

unsigned int 
Cluster::minQuorum()
{
  if (_minQuorum != 0)
    return _minQuorum;
  else {
    unsigned int votes = 0;
    list<counting_auto_ptr<Node> > nodes = this->nodes();
    for (list<counting_auto_ptr<Node> >::iterator iter = nodes.begin();
	 iter != nodes.end();
	 iter++)
      votes += (*iter)->votes();
    return votes/2 + 1;
  }
}

bool 
Cluster::quorate()
{
  return votes() >= minQuorum();
}


counting_auto_ptr<Node> 
Cluster::addNode(const std::string& name, 
		 unsigned int votes, 
		 bool online, 
		 bool clustered)
{
  counting_auto_ptr<Node> node(new Node(name, _name, votes, online, clustered));
  if (_nodes.insert(pair<string, counting_auto_ptr<Node> >(name, node)).second)
    return node;
  else
    // already present
    return _nodes[name];
}

counting_auto_ptr<Service> 
Cluster::addService(const std::string& name, 
		    const std::string& nodeName, 
		    bool failed, 
		    bool autostart)
{
  map<string, counting_auto_ptr<Node> >::iterator iter = _nodes.find(nodeName);
  if (iter == _nodes.end())
    throw string("Cluster::addService(): add node first");
  return iter->second->addService(name, failed, autostart);
}


list<counting_auto_ptr<Node> > 
Cluster::nodes()
{
  list<counting_auto_ptr<Node> > ret;
  
  for (map<string, counting_auto_ptr<Node> >::iterator iter = _nodes.begin();
       iter != _nodes.end();
       iter++) {
    counting_auto_ptr<Node>& node = iter->second;
    if (!node->name().empty())
      ret.push_back(node);
  }
  return ret;
}

std::list<counting_auto_ptr<Node> > 
Cluster::clusteredNodes()
{
  list<counting_auto_ptr<Node> > ret;
  
  for (map<string, counting_auto_ptr<Node> >::iterator iter = _nodes.begin();
       iter != _nodes.end();
       iter++) {
    counting_auto_ptr<Node>& node = iter->second;
    if (node->name().size() && node->clustered())
      ret.push_back(node);
  }
  return ret;
}

list<counting_auto_ptr<Node> > 
Cluster::unclusteredNodes()
{
  list<counting_auto_ptr<Node> > ret;
  
  for (map<string, counting_auto_ptr<Node> >::iterator iter = _nodes.begin();
       iter != _nodes.end();
       iter++) {
    counting_auto_ptr<Node>& node = iter->second;
    if (node->name().size() && !node->clustered())
      ret.push_back(node);
  }
  return ret;
}


list<counting_auto_ptr<Service> > 
Cluster::services()
{
  list<counting_auto_ptr<Service> > ret;
  
  for (map<string, counting_auto_ptr<Node> >::iterator iter = _nodes.begin();
       iter != _nodes.end();
       iter++) {
    list<counting_auto_ptr<Service> > services = iter->second->services();
    ret.insert(ret.end(), services.begin(), services.end());
  }
  return ret;
}

list<counting_auto_ptr<Service> > 
Cluster::runningServices()
{
  list<counting_auto_ptr<Service> > ret;
  
  list<counting_auto_ptr<Node> > nodes = this->nodes();
  for (list<counting_auto_ptr<Node> >::iterator iter = nodes.begin();
       iter != nodes.end();
       iter++) {
    counting_auto_ptr<Node>& node = *iter;
    list<counting_auto_ptr<Service> > services = node->services();
    if (node->name().size())
      ret.insert(ret.end(), services.begin(), services.end());
  }
  return ret;
}

std::list<counting_auto_ptr<Service> > 
Cluster::stoppedServices()
{
  list<counting_auto_ptr<Service> > ret;
  list<counting_auto_ptr<Service> > services = _nodes.find("")->second->services();
  for (list<counting_auto_ptr<Service> >::iterator iter = services.begin();
       iter != services.end();
       iter++) {
    counting_auto_ptr<Service>& service = *iter;
    if (!service->running() && !service->failed())
      ret.push_back(service);
  }
  return ret;
}

std::list<counting_auto_ptr<Service> > 
Cluster::failedServices()
{
  list<counting_auto_ptr<Service> > ret;
  list<counting_auto_ptr<Service> > services = _nodes.find("")->second->services();
  for (list<counting_auto_ptr<Service> >::iterator iter = services.begin();
       iter != services.end();
       iter++) {
    counting_auto_ptr<Service>& service = *iter;
    if (service->failed())
      ret.push_back(service);
  }
  return ret;
}




string 
ClusterMonitoring::cluster2xml(Cluster& cluster)
{
  char buff[1024];
  
  // cluster
  XMLObject clu("cluster");
  clu.set_attr("name", cluster.name());
  sprintf(buff, "%u", cluster.votes());
  clu.set_attr("votes", buff);
  sprintf(buff, "%u", cluster.minQuorum());
  clu.set_attr("minQuorum", buff);
  clu.set_attr("quorate", (cluster.quorate()) ? "true" : "false");
  
  // nodes
  std::list<counting_auto_ptr<Node> > nodes = cluster.nodes();
  for (std::list<counting_auto_ptr<Node> >::iterator iter = nodes.begin();
       iter != nodes.end();
       iter++) {
    Node& node = **iter;
    XMLObject n("node");
    n.set_attr("name", node.name());
    sprintf(buff, "%u", node.votes());
    n.set_attr("votes", buff);
    n.set_attr("online", (node.online()) ? "true" : "false");
    n.set_attr("clustered", (node.clustered()) ? "true" : "false");
    clu.add_child(n);
  }
  
  // services
  std::list<counting_auto_ptr<Service> > services = cluster.services();
  for (std::list<counting_auto_ptr<Service> >::iterator iter = services.begin();
       iter != services.end();
       iter++) {
    Service& service = **iter;
    XMLObject s("service");
    s.set_attr("name", service.name());
    s.set_attr("running", (service.running()) ? "true" : "false");
    if (service.running())
      s.set_attr("nodename", service.nodename());
    s.set_attr("failed", (service.failed()) ? "true" : "false");
    s.set_attr("autostart", (service.autostart()) ? "true" : "false");
    clu.add_child(s);
  }
  
  return generateXML(clu);
}

counting_auto_ptr<Cluster> 
ClusterMonitoring::xml2cluster(const std::string& xml)
{
  XMLObject clu = parseXML(xml);
  if (clu.tag() != "cluster")
    throw string("xml2cluster(): invalid xml");
  
  // cluster
  string name = clu.get_attr("name");
  if (name.empty())
    throw string("xml2cluster(): missing cluster name");
  unsigned int minQuorum = 0;
  if (sscanf(clu.get_attr("minQuorum").c_str(), "%u", &minQuorum) != 1)
    throw string("xml2cluster(): invalid value for cluster's minQuorum");
  counting_auto_ptr<Cluster> cluster(new Cluster(name, minQuorum));
  
  // nodes
  for (list<XMLObject>::const_iterator iter = clu.children().begin();
       iter != clu.children().end();
       iter++) {
    const XMLObject& obj = *iter;
    if (obj.tag() == "node") {
      // name
      string node_name = obj.get_attr("name");
      if (node_name.empty())
	throw string("xml2cluster(): node missing 'name' attr");
      // votes
      unsigned int votes;
      if (sscanf(obj.get_attr("votes").c_str(), "%u", &votes) != 1)
	throw string("xml2cluster(): invalid value for node's votes");
      // online
      string online_str = obj.get_attr("online");
      bool online = online_str == "true";
      if (online_str.empty())
	throw string("xml2cluster(): node missing 'online' attr");
      // clustered
      string clustered_str = obj.get_attr("clustered");
      bool clustered = clustered_str == "true";
      if (clustered_str.empty())
	throw string("xml2cluster(): node missing 'clustered' attr");
      // add node to cluster
      cluster->addNode(node_name, votes, online, clustered);
    }
  }
  
  // services
  for (list<XMLObject>::const_iterator iter = clu.children().begin();
       iter != clu.children().end();
       iter++) {
    const XMLObject& obj = *iter;
    if (obj.tag() == "service") {
      // name
      string service_name = obj.get_attr("name");
      if (service_name.empty())
	throw string("xml2cluster(): service missing 'name' attr");
      // running
      string running_str = obj.get_attr("running");
      bool running = running_str == "true";
      if (running_str.empty())
	throw string("xml2cluster(): service missing 'running' attr");
      // nodename
      string nodename = obj.get_attr("nodename");
      if (running)
	if (nodename.empty())
	  throw string("xml2cluster(): running service missing 'nodename' attr");
      // failed
      string failed_str = obj.get_attr("failed");
      bool failed = failed_str == "true";
      if (failed_str.empty())
	throw string("xml2cluster(): service missing 'failed' attr");
      // autostart
      string autostart_str = obj.get_attr("autostart");
      bool autostart = autostart_str == "true";
      if (autostart_str.empty())
	throw string("xml2cluster(): service missing 'autostart' attr");
      // add service to cluster
      cluster->addService(service_name, nodename, failed, autostart);
    }
  }
  
  return cluster;
}
