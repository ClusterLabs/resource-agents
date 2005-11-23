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


#include "Socket.h"

#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>


using namespace ClusterMonitoring;


Socket::Socket(int sock) : 
  _sock(sock)
{
  try {
    _counter = new int(1);
  } catch ( ... ) {
    close();
    throw std::string("Socket(int sock) new failed");
  }
}

Socket::Socket(const Socket& s) :
  _sock(s._sock),
  _counter(s._counter)
{
  *_counter += 1;
}

Socket& 
Socket::operator= (const Socket& s)
{
  if (&s != this) {
    _sock = s._sock;
    _counter = s._counter;
    *_counter += 1;
  }
  return *this;
}

Socket::~Socket()
{
  *_counter -= 1;
  if (*_counter == 0) {
    close();
    delete _counter;
  }
}

void
Socket::close()
{
  if (_sock != -1) {
    shutdown(_sock, SHUT_RDWR);
    while (true) {
      int ret = ::close(_sock);
      if (ret) {
	if (errno == EINTR)
	  continue;
      }
      break;
    }
  }
  _sock = -1;
}


bool 
Socket::operator== (const Socket& obj)
{
  return obj._sock == _sock;
}

int 
Socket::get_sock()
{
  return _sock;
}


std::vector<std::string>
ClusterMonitoring::name2IP(const std::string& hostname)
{
  char buff[INET_ADDRSTRLEN+1];
  std::vector<std::string> addrs;
  struct hostent* ent = gethostbyname2(hostname.c_str(), AF_INET);
  if (!ent)
    return addrs;
  char** addrs_b = ent->h_addr_list;
  for (int i=0; addrs_b[i]; i++) {
    struct in_addr addr;
    addr.s_addr = *((u_int32_t*) addrs_b[i]);
    if (inet_ntop(AF_INET, &addr, buff, sizeof(buff)))
      addrs.push_back(buff);
  }
  return addrs;
}
