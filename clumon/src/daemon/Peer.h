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


#ifndef Peer_h
#define Peer_h

#include "counting_auto_ptr.h"
#include "Socket.h"

#include <vector>
#include <string>


namespace ClusterMonitoring
{


class Peer
{
 public:
  Peer();
  Peer(const std::string& hostname, const ClientSocket&);
  Peer(const std::string& hostname, unsigned short port);
  virtual ~Peer();
  
  void send();
  std::vector<std::string> receive();
  
  bool outq_empty() { return _out->empty(); }
  
  void append(const std::string& msg);
  
  int get_sock_fd();
  
  std::string hostname();
  
  bool operator== (const Peer&) const;
  
 private:
  counting_auto_ptr<ClientSocket> _sock;
  const std::string _hostname;
  
  counting_auto_ptr<std::string> _in;
  counting_auto_ptr<std::string> _out;
  
};  // class Peer


};  // namespace ClusterMonitoring 


#endif  // Peer
