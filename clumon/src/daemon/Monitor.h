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


#ifndef Monitor_h
#define Monitor_h

#include <string>
#include <map>

#include "XML.h"
#include "Thread.h"
#include "Communicator.h"
#include "Cluster.h"


namespace ClusterMonitoring
{


class Monitor : public Thread, public CommDP
{
 public:
  Monitor(unsigned short port);
  virtual ~Monitor();
  
  std::string request(const std::string&);
  
  virtual void msg_arrived(const std::string& host,
			   const std::string& msg);
  
 protected:
  void run();
 private:
  
  Mutex _mutex; // _cluster and _cache
  counting_auto_ptr<Cluster> _cluster;
  std::map<std::string, std::pair<unsigned int, XMLObject> > _cache;
  
  Communicator _comm;
  
  // return (nodenames - my_nodename)
  std::vector<std::string> get_local_info(std::string& nodename,
					  std::string& clustername,
					  std::string& msg);
  counting_auto_ptr<Cluster> merge_data(const std::string& clustername);
  
  unsigned int time();
  XMLObject parse_cluster_conf();
  //  bool clustered();
  //  bool quorate();
  std::string nodename(const std::vector<std::string>& nodenames);
  std::vector<std::string> clustered_nodes();
  std::vector<XMLObject> services_info();
  
};  // class Monitor


};  // namespace ClusterMonitoring


#endif  // Monitor_h
