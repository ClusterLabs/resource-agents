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
#include "array_auto_ptr.h"
#include "Logger.h"

#include <sys/poll.h>
#include <sys/time.h>
#include <stdlib.h>
#include <errno.h>

typedef struct pollfd poll_fd;

#include <map>
#include <algorithm>


using namespace ClusterMonitoring;
using namespace std;



Communicator::Communicator(unsigned short port, 
			   CommDP& delivery_point) :
  _port(port), 
  _serv_sock(_port),
  _delivery_point(delivery_point)
{
  struct timeval t;
  struct timezone z;
  gettimeofday(&t, &z);
  _connect_time = t.tv_sec;
  _rand_state = t.tv_usec;
  log(string("Communicator created, port ") + _port, LogCommunicator);
}

Communicator::~Communicator()
{
  stop();
  log("Communicator deleted", LogCommunicator);
}
  
void 
Communicator::send(const std::string& msg)
{
  MutexLocker l(_mutex);
  _out_q.push_back(msg);
}

void 
Communicator::update_peers(const string& self, 
			   const std::vector<std::string>& hosts)
{
  MutexLocker l(_mutex);
  _my_hostname = self;
  _peer_hostnames.clear();
  for (unsigned int i=0; i<hosts.size(); i++)
    _peer_hostnames.push_back(hosts[i]);
}

void
Communicator::run()
{
  while (!shouldStop()) {
    vector<string> names;
    vector<string> que;
    string my_hostname;
    {
      MutexLocker l(_mutex);
      my_hostname = _my_hostname;
      for (unsigned int i=0; i<_peer_hostnames.size(); i++)
	names.push_back(_peer_hostnames[i]);
      for (unsigned int i=0; i<_out_q.size(); i++)
	que.push_back(_out_q[i]);
      _out_q.clear();
    }
    
    // remove non-peers
    vector<string> remove_us;
    for (map<string, Peer>::iterator iter = _peers.begin(); 
	 iter != _peers.end(); 
	 iter++) {
      const string& name = iter->first;
      if (find(names.begin(), names.end(), name) == names.end())
	remove_us.push_back(name);
    }
    for (vector<string>::iterator iter = remove_us.begin();
	 iter != remove_us.end();
	 iter++) {
      log("dropping connection with " + *iter, LogCommunicator);
      _peers.erase(*iter);
    }
    
    // connect to peers
    if (time_to_connect()) {
      for (vector<string>::iterator iter = names.begin();
	   iter != names.end();
	   iter ++) {
	const string& name = *iter;
	if (_peers.find(name) == _peers.end())
	  try {
	    _peers.insert(pair<string, Peer>(name, Peer(name, _port)));
	    log("connected to " + name + ", socket " + _peers[name].get_sock_fd(), LogCommunicator);
	  } catch ( ... ) {}
      }
    }
    
    // buffer msgs
    for (vector<string>::iterator iter_q = que.begin(); 
	 iter_q != que.end(); 
	 iter_q++) {
      string& msg = *iter_q;
      _delivery_point.msg_arrived(my_hostname, msg);
      for (map<string, Peer>::iterator iter_p = _peers.begin(); 
	   iter_p != _peers.end(); 
	   iter_p++)
	iter_p->second.append(msg);
    }
    
    serve_sockets(names);
    
  }  // while(!shouldStop())
}

void 
Communicator::serve_sockets(vector<string>& names)
{
  map<int, Peer> fd_peer;
  for (map<string, Peer>::iterator iter = _peers.begin();
       iter != _peers.end();
       iter++) {
    Peer& peer = iter->second;
    fd_peer.insert(pair<int, Peer>(peer.get_sock_fd(), peer));
  }
  unsigned int socks_num = fd_peer.size() + 1;
  
  // prepare poll structs
  array_auto_ptr<poll_fd> poll_data(new poll_fd[socks_num]);
  poll_data[0].fd = _serv_sock.get_sock();
  poll_data[0].events = POLLIN;
  poll_data[0].revents = 0;
  map<int, Peer>::iterator iter = fd_peer.begin();
  for (unsigned int i=1; i<socks_num; i++, iter++) {
    poll_data[i].fd = iter->first;
    poll_data[i].events = POLLIN;
    if ( ! iter->second.outq_empty())
      poll_data[i].events = POLLOUT;
    poll_data[i].revents = 0;
  }
  
  // wait for events
  int ret = poll(poll_data.get(), socks_num, 500);
  if (ret == 0)
    return;
  else if (ret == -1) {
    if (errno == EINTR)
      return;
    else
      throw string("Communicator::run(): poll() error");
  }
  
  // process events
  for (unsigned int i=0; i<socks_num; i++) {
    poll_fd& poll_info = poll_data[i];
    
    // server socket
    if (poll_info.fd == _serv_sock.get_sock()) {
      if (poll_info.revents & POLLIN) {
	try {
	  ClientSocket sock = _serv_sock.accept();
	  string hostname;
	  for (vector<string>::iterator iter = names.begin();
	       iter != names.end();
	       iter++) {
	    string& name = *iter;
	    if (sock.connected_to(name))
	      hostname = name;
	  }
	  if (hostname.size()) {
	    _peers.insert(pair<string, Peer>(hostname, Peer(hostname, sock)));
	    log("accepted connection from " + hostname + ", socket " + sock.get_sock(), LogCommunicator);
	  }
	} catch ( ... ) {}
      }
      if (poll_info.revents & (POLLERR | POLLHUP | POLLNVAL))
	throw string("Communicator::run(): server socket error????");
    }
    
    // client socket
    else {
      Peer& peer = fd_peer[poll_info.fd];
      if (poll_info.revents & POLLIN) {
	vector<string> msgs;
	try {
	  msgs = peer.receive();
	} catch ( ... ) {
	  log("error receiving data from " + peer.hostname(), LogCommunicator);
	  _peers.erase(peer.hostname());
	  continue;
	}
	for (unsigned int i=0; i<msgs.size(); i++)
	  _delivery_point.msg_arrived(peer.hostname(), msgs[i]);
      }
      if (poll_info.revents & (POLLERR | POLLHUP | POLLNVAL)) {
	_peers.erase(peer.hostname());
	continue;
      }
      if (poll_info.revents & POLLOUT) {
	try {
	  peer.send();
	} catch ( ... ) {
	  log("error sending data to " + peer.hostname(), LogCommunicator);
	  _peers.erase(peer.hostname());
	  continue;
	}
      }
    }  // client socket
  }  // process events
}

bool
Communicator::time_to_connect()
{
  struct timeval t;
  struct timezone z;
  gettimeofday(&t, &z);
  unsigned int time = t.tv_sec;
  
  if (time > _connect_time) {
    _connect_time = time + rand_r(&_rand_state) % 15;
    return true;
  }
  return false;
}
