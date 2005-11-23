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


#include "Communicator.h"
#include "Cluster.h"
#include "XML.h"
#include "executils.h"


#include <sys/poll.h>
#include <unistd.h>


#include <iostream>
#include <vector>
#include <set>


using namespace ClusterMonitoring;
using namespace std;


class DP : public CommDP
{
public:
  virtual void msg_arrived(const string& msg) { cout << msg << endl; }
};


int 
main(int argc, char** argv)
{
  try {
    /*
    int status;
    string out, err;
    vector<string> args;
    args.push_back("/etc/cluster/cluster.conf");
    if (execute("/bin/cat", args, out, err, status) == 0) {
      if (status)
	cout << "cat error" << endl;
      else
	cout << out << endl;
    }
    else
      cout << "execute() error" << endl;
    return 0;
    */
      
    /*
    XMLObject root("cluster");
    
    XMLObject kid1("node");
    kid1.attrs["name"] = "node1";
    kid1.attrs["pc"] = "node1";
    root.kids.insert(kid1);
    
    XMLObject kid2("node");
    kid2.attrs["name"] = "node2";
    XMLObject serv1("service");
    serv1.attrs["name"] = "serv1";
    kid2.kids.insert(serv1);
    XMLObject serv2("service");
    serv2.attrs["name"] = "serv2";
    kid2.kids.insert(serv2);
    
    root.kids.insert(kid2);
    
    string xml = generateXML(root);
    cout << xml << endl;
    cout << generateXML(parseXML(xml)) << endl;
    */
    
    /*
    Cluster cluster("test cluster", 10);
    cluster.addNode("node1", 2, true, true);
    cluster.addService("serv1", "node1", false, true);
    cluster.addService("serv2", "node1", false, true);
    cluster.addService("serv3", "", true, true);
    
    string xml = cluster2xml(cluster);
    cout << xml << endl;
    counting_auto_ptr<Cluster> clu = xml2cluster(xml);
    cout << cluster2xml(*clu) << endl;
    */
    
    
    /*
    ClientSocket sock = ServerSocket(15002).accept();
    cout << sock.connected_to("localhost") << endl;
    return 0;
    */
    
    /*
    DP dp;
    
    Communicator comm(15002, dp);
    
    comm.start();
    
    vector<string> peers;
    peers.push_back("localhost");
    
    comm.update_peers(peers);
    
    poll(NULL, 0, 150000);
    */
    
  } catch ( string e ) {
    cout << e << endl;
    throw;
  }
}
