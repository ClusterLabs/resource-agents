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


#ifndef Communicator_h
#define Communicator_h

#include "Thread.h"
#include "Mutex.h"
#include "Socket.h"
#include "Peer.h"

#include <string>
#include <vector>
#include <map>


namespace ClusterMonitoring 
{


class CommDP
{
 public:
  virtual void msg_arrived(const std::string& host,
			   const std::string& msg) = 0;
};


class Communicator : public Thread
{
 public:
  Communicator(unsigned short port, CommDP& delivery_point);
  virtual ~Communicator();
  
  void send(const std::string& msg);
  void update_peers(const std::string& self, const std::vector<std::string>& peers);
  
 private:
  unsigned short _port;
  ServerSocket _serv_sock;
  
  CommDP& _delivery_point;
  
  std::map<std::string, Peer> _peers;
  
  Mutex _mutex;
  std::string _my_hostname;
  std::vector<std::string> _out_q;
  std::vector<std::string> _peer_hostnames;
  
  void serve_sockets(std::vector<std::string>&hostnames);
  
  bool time_to_connect();
  unsigned int _connect_time;
  unsigned int _rand_state;
  
  
  void run();
  
  Communicator(const Communicator&);
  Communicator& operator= (const Communicator&);

};  // class Communicator


};  // namespace ClusterMonitoring 


#endif  // Communicator_h
