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


#include "Monitor.h"
#include "executils.h"

#include <sys/poll.h>
#include <sys/time.h>

#include <algorithm>

#include <iostream>


using namespace ClusterMonitoring;
using namespace std;



#define RG_STATE_STOPPED                110     /** Resource group is stopped */
#define RG_STATE_STARTING               111     /** Resource is starting */
#define RG_STATE_STARTED                112     /** Resource is started */
#define RG_STATE_STOPPING               113     /** Resource is stopping */
#define RG_STATE_FAILED                 114     /** Resource has failed */
#define RG_STATE_UNINITIALIZED          115     /** Thread not running yet */
#define RG_STATE_CHECK                  116     /** Checking status */
#define RG_STATE_ERROR                  117     /** Recoverable error */
#define RG_STATE_RECOVER                118     /** Pending recovery */
#define RG_STATE_DISABLED               119     /** Resource not allowd to run */




Monitor::Monitor(unsigned short port) :
  _comm(port, *this)
{}

Monitor::~Monitor()
{
  stop();
}


void
Monitor::run()
{
  _comm.start();
  while (!shouldStop()) {
    
    unsigned int time_beg = time();
    
    try {
      // get local info
      string my_nodename, clustername, msg;
      vector<string> nodenames = get_local_info(my_nodename,
						clustername,
						msg);
      
      // publish it
      _comm.update_peers(my_nodename, nodenames);
      _comm.send(msg + '\n');
      
      // merge data from all nodes (removing stale entries) and update _cluster
      {
	MutexLocker l(_mutex);
	_cluster = merge_data(clustername);
      }
    } catch ( ... ) {}
    
    cout << "Monitor::run() iteration took " << time() - time_beg << " seconds" << endl;
    
    // wait some time
    struct pollfd nothing;
    poll(&nothing, 0, 5000);
  }
  _comm.stop();
}


string
Monitor::request(const string& msg)
{
  MutexLocker l(_mutex);
  if (msg == "GET") {
    if (_cluster.get() == NULL) 
      return "\n\n";
    try {
      return cluster2xml(*_cluster) + "\n";
    } catch ( ... ) {
      return "\n\n";
    }
  }
  throw string("invalid request");
}

void 
Monitor::msg_arrived(const string& hostname, const string& msg_in)
{
  string msg(msg_in);
  // strip \n from the beggining
  while (msg.size())
    if (msg[0] == '\n')
      msg = msg.substr(1);
    else
      break;
  
  try {
    XMLObject obj = parseXML(msg);
    if (obj.tag() == "clumond") {
      string type = obj.get_attr("type");
      if (type == "clusterupdate")
	if (obj.children().size() == 1) {
	  const XMLObject& cluster = *(obj.children().begin());
	  if (cluster.tag() == "cluster") {
	    MutexLocker l(_mutex);
	    pair<unsigned int, XMLObject> data(time(), cluster);
	    _cache[hostname] = data;
	  }
	}
      // TODO other msgs
    }
  } catch ( ... ) {}
}

vector<string> 
Monitor::get_local_info(string& nodename,
			string& clustername,
			string& msg)
{
  XMLObject cluster = parse_cluster_conf();
  
  // insert current node info
  const vector<string> clustered_nodes = this->clustered_nodes();
  for (vector<string>::const_iterator iter = clustered_nodes.begin();
       iter != clustered_nodes.end();
       iter++) {
    const string& name = *iter;
    for (list<XMLObject>::const_iterator iter_k = cluster.children().begin();
	 iter_k != cluster.children().end();
	 iter_k++) {
      XMLObject& kid = (XMLObject&) *iter_k;
      if (kid.tag() == "node")
	if (kid.get_attr("name") == name) {
	  kid.set_attr("online", "true");
	  kid.set_attr("clustered", "true");
	}
    }
  }
  
  // insert current service info
  const vector<XMLObject> services_info = this->services_info();
  for (vector<XMLObject>::const_iterator iter_i = services_info.begin();
       iter_i != services_info.end();
       iter_i++) {
    const XMLObject& service = *iter_i;
    for (list<XMLObject>::const_iterator iter_c = cluster.children().begin();
	 iter_c != cluster.children().end();
	 iter_c++) {
      XMLObject& kid = (XMLObject&) *iter_c;
      if (kid.tag() == "service")
	if (kid.get_attr("name") == service.get_attr("name")) {
	  for (map<string, string>::const_iterator iter = service.attrs().begin();
	       iter != service.attrs().end();
	       iter++)
	    kid.set_attr(iter->first, iter->second);
	}
    }
  }
  
  // ** return values **
  
  // nodes
  vector<string> nodes;
  for (list<XMLObject>::const_iterator iter = cluster.children().begin();
       iter != cluster.children().end();
       iter++)
    if (iter->tag() == "node")
      nodes.push_back(iter->get_attr("name"));
  
  // clustername
  clustername = cluster.get_attr("name");
  
  // nodename
  nodename = this->nodename(nodes);
  
  // msg
  XMLObject msg_xml("clumond");
  msg_xml.set_attr("type", "clusterupdate");
  msg_xml.add_child(cluster);
  msg = generateXML(msg_xml);
  
  // return nodes - nodename
  nodes.erase(find(nodes.begin(), nodes.end(), nodename));
  return nodes;
}

XMLObject 
Monitor::parse_cluster_conf()
{
  int status;
  string out, err;
  vector<string> args;
  args.push_back("/etc/cluster/cluster.conf");
  if (execute("/bin/cat", args, out, err, status))
    throw string("parse_cluster_conf(): missing cluster.conf");
  if (status)
    throw string("parse_cluster_conf(): missing cluster.conf");
  
  XMLObject cluster_conf = parseXML(out);
  if (cluster_conf.tag() != "cluster")
    throw string("parse_cluster_conf(): invalid cluster.conf");
  
  XMLObject cluster("cluster");
  for (map<string, string>::const_iterator iter = cluster_conf.attrs().begin();
       iter != cluster_conf.attrs().end();
       iter++)
    cluster.set_attr(iter->first, iter->second);
  
  for (list<XMLObject>::const_iterator iter = cluster_conf.children().begin();
       iter != cluster_conf.children().end();
       iter++) {
    const XMLObject& kid = *iter;
    if (kid.tag() == "clusternodes") {
      for (list<XMLObject>::const_iterator iter_n = kid.children().begin();
	   iter_n != kid.children().end();
	   iter_n++) {
	const XMLObject& node_conf = *iter_n;
	if (node_conf.tag() == "clusternode") {
	  XMLObject node("node");
	  for (map<string, string>::const_iterator iter_a = node_conf.attrs().begin();
	       iter_a != node_conf.attrs().end();
	       iter_a++)
	    node.set_attr(iter_a->first, iter_a->second);
	  cluster.add_child(node);
	}
      }
    } else if (kid.tag() == "rm") {
      for (list<XMLObject>::const_iterator iter_s = kid.children().begin();
	   iter_s != kid.children().end();
	   iter_s++) {
	const XMLObject& service_conf = *iter_s;
	if (service_conf.tag() == "service") {
	  XMLObject service("service");
	  for (map<string, string>::const_iterator iter_a = service_conf.attrs().begin();
	       iter_a != service_conf.attrs().end();
	       iter_a++)
	    service.set_attr(iter_a->first, iter_a->second);
	  cluster.add_child(service);
	}
      }
    } else if (kid.tag() == "cman") {
      cluster.set_attr("locking", "cman");
      if (kid.has_attr("expected_votes"))
	cluster.set_attr("minQuorum", kid.get_attr("expected_votes"));
    } else if (kid.tag() == "gulm") {
      cluster.set_attr("locking", "gulm");
    }
  }
  
  return cluster;
}

counting_auto_ptr<Cluster> 
Monitor::merge_data(const string& clustername)
{
  MutexLocker l(_mutex);
  
  XMLObject cluster("cluster");
  cluster.set_attr("name", clustername);
  cluster.set_attr("config_version", "0");
  unsigned int config_version = 0;
  
  vector<map<string, pair<unsigned int, XMLObject> >::iterator> stales;
  vector<string> online_nodes;
  
  for (map<string, pair<unsigned int, XMLObject> >::iterator iter = _cache.begin();
       iter != _cache.end();
       iter++) {
    if (iter->second.first < time() - 8)
      stales.push_back(iter);
    else {
      online_nodes.push_back(iter->first);
      const XMLObject& cl2 = iter->second.second;
      if (cl2.has_attr("name") &&
	  cl2.get_attr("name") == cluster.get_attr("name")) {
	unsigned int v;
	if (sscanf(cl2.get_attr("config_version").c_str(), "%u", &v) != 1)
	  continue;
	if (v == config_version)
	  cluster.merge(cl2);
	else if (v > config_version) {
	  config_version = v;
	  cluster = cl2;
	}
      }
    }
  }
  for (unsigned int i=0; i<stales.size(); i++)
    _cache.erase(stales[i]);
  
  cout << "merged data: \n" << generateXML(cluster) << endl;
  
  if (_cache.size() == 0)
    return counting_auto_ptr<Cluster>();
  
  // build cluster
  counting_auto_ptr<Cluster> cluster_ret;
  string name = cluster.get_attr("name");
  unsigned int minQuorum = 0;
  if (sscanf(cluster.get_attr("minQuorum").c_str(), "%u", &minQuorum) != 1)
    cluster_ret = counting_auto_ptr<Cluster> (new Cluster(name));
  else
    cluster_ret = counting_auto_ptr<Cluster> (new Cluster(name, minQuorum));
  // nodes
  for (list<XMLObject>::const_iterator iter = cluster.children().begin();
       iter != cluster.children().end();
       iter++) {
    const XMLObject& obj = *iter;
    if (obj.tag() == "node") {
      string node_name = obj.get_attr("name");
      if (node_name.empty())
	throw string("merge_data(): node missing 'name' attr");
      unsigned int votes;
      if (sscanf(obj.get_attr("votes").c_str(), "%u", &votes) != 1)
	votes = 1;
      bool online;
      if (obj.has_attr("online"))
	online = obj.get_attr("online") == "true";
      else
	online = find(online_nodes.begin(), online_nodes.end(), node_name) != online_nodes.end();
      bool clustered = obj.get_attr("clustered") == "true";
      // add node to cluster
      cluster_ret->addNode(node_name, votes, online, clustered);
    }
  }
  // services
  for (list<XMLObject>::const_iterator iter = cluster.children().begin();
       iter != cluster.children().end();
       iter++) {
    const XMLObject& obj = *iter;
    if (obj.tag() == "service") {
      // name
      string service_name = obj.get_attr("name");
      if (service_name.empty())
	throw string("merge_data(): service missing 'name' attr");
      bool running = obj.get_attr("running") == "true";
      string nodename = obj.get_attr("nodename");
      if (running && nodename.empty())
	throw string("merge_data(): running service missing 'nodename' attr");
      bool failed = obj.get_attr("failed") == "true";
      bool autostart = obj.get_attr("autostart") == "1";
      // add service to cluster
      cluster_ret->addService(service_name, nodename, failed, autostart);
    }
  }
  
  return cluster_ret;
}

unsigned int
Monitor::time()
{
  struct timeval t;
  struct timezone z;
  gettimeofday(&t, &z);
  return t.tv_sec;
}

/*
bool 
Monitor::clustered()
{
  string out, err;
  int status;
  vector<string> args;
  args.push_back("members");
  if (execute("/sbin/magma_tool", args, out, err, status))
    throw string("clustered(): missing magma_tool");
  if (status)
    return false;
  
  // look for 'Connect failure' substring
  if (out.find("Connect failure") != out.npos)
    return false;
  else
    return true;
}
*/

/*
bool 
Monitor::quorate()
{
  string out, err;
  int status;
  vector<string> args;
  args.push_back("quorum");
  if (execute("/sbin/magma_tool", args, out, err, status))
    throw string("quorate(): missing magma_tool");
  if (status)
    return false;
  
  // look for 'Quorate' substring
  if (out.find("Quorate") != out.npos)
    return true;
  else
    return false;
}
*/

vector<string> 
Monitor::clustered_nodes()
{
  string out, err;
  int status;
  vector<string> args;
  args.push_back("members");
  if (execute("/sbin/magma_tool", args, out, err, status))
    throw string("clustered_nodes(): missing magma_tool");
  if (status)
    return vector<string>();
  
  // split out by lines
  vector<string> lines;
  while (out.size()) {
    string::size_type idx = out.find('\n');
    lines.push_back(out.substr(0, idx));
    if (idx == out.npos)
      out = "";
    else
      out = out.substr(idx+1);
  }
  
  vector<string> running;
  for (vector<string>::iterator iter = lines.begin();
       iter != lines.end();
       iter++) {
    string& line = *iter;
    if (line.find("Member ID") != line.npos) {
      string t = line.substr(line.find(": ") + 2);
      string::size_type idx = t.find(',');
      string name = t.substr(0, idx);
      string rest = t.substr(idx);
      if (rest.find("UP") != rest.npos)
	running.push_back(name);
    }
  }
  return running;
}

string
Monitor::nodename(const vector<string>& nodenames)
{
  string out, err;
  int status;
  if (execute("/sbin/ifconfig", vector<string>(), out, err, status))
    throw string("nodename(): missing ifconfig");
  if (status)
    throw string("nodename(): ifconfig failed???");
  
  for (vector<string>::const_iterator iter = nodenames.begin();
       iter != nodenames.end();
       iter++) {
    const string& nodename = *iter;
    vector<string> ips = name2IP(nodename);
    for (vector<string>::iterator iter_ip = ips.begin();
	 iter_ip != ips.end();
	 iter_ip++) {
      if (out.find(*iter_ip) != out.npos)
	return nodename;
    }
  }
  return "";
}

vector<XMLObject> 
Monitor::services_info()
{
  vector<XMLObject> services;
  
  try {
    
    unsigned int time_beg = time();
    
    string out, err;
    int status;
    vector<string> args;
    args.push_back("-x");
    if (execute("/usr/sbin/clustat", args, out, err, status))
      throw string("services_info(): missing clustat");
    if (status)
      return vector<XMLObject>();
    
    cout << "clustat returned after " << time() - time_beg << " seconds" << endl;
    
    XMLObject clustat = parseXML(out);
    for (list<XMLObject>::const_iterator iter_c = clustat.children().begin();
	 iter_c != clustat.children().end();
	 iter_c++) {
      if (iter_c->tag() == "groups") {
	const XMLObject& groups = *iter_c;
	for (list<XMLObject>::const_iterator iter = groups.children().begin();
	     iter != groups.children().end();
	     iter++) {
	  const XMLObject& group = *iter;
	  XMLObject service("service");
	  service.set_attr("name", group.get_attr("name"));
	  
	  int state_code;
	  if (sscanf(group.get_attr("state").c_str(), "%i", &state_code) != 1)
	    continue;
	  
	  bool failed, running;
	  switch (state_code) {
	  case RG_STATE_STOPPED:
	  case RG_STATE_STOPPING:
	  case RG_STATE_UNINITIALIZED:
	  case RG_STATE_ERROR:
	  case RG_STATE_RECOVER:
	  case RG_STATE_DISABLED:
	    running = failed = false;
	    break;
	  case RG_STATE_FAILED:
	    running = false;
	    failed = true;
	    break;
	  case RG_STATE_STARTING:
	  case RG_STATE_STARTED:
	  case RG_STATE_CHECK:
	    running = true;
	    failed = false;
	    break;
	  default:
	    continue;
	  }
	  
	  service.set_attr("failed", (failed)? "true" : "false");
	  service.set_attr("running", (running)? "true" : "false");
	  if (running)
	    service.set_attr("nodename", group.get_attr("owner"));
	  
	  services.push_back(service);
	}
      }
    }
  } catch ( ... ) {}
  
  return services;
}
