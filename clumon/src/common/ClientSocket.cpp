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
#include <sys/un.h>
#include <string.h>
#include <netdb.h>


using namespace ClusterMonitoring;


ClientSocket::ClientSocket() :
  Socket(-1)
{}

ClientSocket::ClientSocket(int sock, unsigned int addr) :
  Socket(sock),
  _addr(addr)
{}

ClientSocket::ClientSocket(const std::string& sock_path) :
  Socket(-1)
{
  _sock = socket(PF_UNIX, SOCK_STREAM, 0);
  if(_sock == -1)
    throw std::string("ClientSocket(string): socket() failed");
  
  struct sockaddr_un {
    sa_family_t  sun_family;
    char         sun_path[100];
  } addr;
  addr.sun_family = AF_UNIX;
  memcpy(addr.sun_path, sock_path.c_str(), sock_path.size()+1);
  
  if(connect(_sock, (struct sockaddr*) &addr, sizeof(addr))) {
    throw std::string("ClientSocket(string): connect() failed");
  }
}

ClientSocket::ClientSocket(const std::string& hostname, unsigned short port) :
  Socket(-1)
{
  _sock = socket(PF_INET, SOCK_STREAM, 0);
  if (_sock == -1)
    throw std::string("ClientSocket(hostname, port): socket() failed");
  
  struct hostent* ent = gethostbyname2(hostname.c_str(), AF_INET);
  if (!ent)
    throw std::string("ClientSocket(hostname, port): gethostbyname() failed");
  
  char** addrs = ent->h_addr_list;
  for (int i=0; addrs[i]; i++) {
    struct sockaddr_in addr_in;
    addr_in.sin_family = AF_INET;
    addr_in.sin_port = htons(port);
    addr_in.sin_addr.s_addr = *((u_int32_t*) addrs[i]);
    if (connect(_sock, (struct sockaddr*) &addr_in, sizeof(addr_in)))
      continue;
    else {
      _addr = addr_in.sin_addr.s_addr;
      return;
    }
  }
  throw std::string("ClientSocket(hostname, port): connect() failed");
}

ClientSocket::ClientSocket(const ClientSocket& s) :
  Socket(s)
{}

ClientSocket& 
ClientSocket::operator= (const ClientSocket& s)
{
  if (&s != this)
    Socket::operator= (s);
  return *this;
}

ClientSocket::~ClientSocket()
{}


bool 
ClientSocket::connected_to(const std::string& hostname)
{
  struct hostent* ent = gethostbyname2(hostname.c_str(), AF_INET);
  if (!ent)
    return false;
  
  char** addrs = ent->h_addr_list;
  for (int i=0; addrs[i]; i++)
    if (*((u_int32_t*) addrs[i]) == _addr)
      return true;
  return false;
}

std::string 
ClientSocket::recv()
{
  if (_sock == -1)
    throw std::string("ClientSocket::recv(): socket already closed");
  
  while (true) {
    char buffer[1024];
    int ret = ::recv(_sock, buffer, sizeof(buffer), 0);
    if (ret == -1) {
      if (errno == EINTR)
	continue;
      throw std::string("ClientSocket::recv(): recv error");
    }
    
    if(ret == 0) {
      close();
      throw std::string("ClientSocket::recv(): socket has been shutdown");
    }
    
    return std::string(buffer, ret);
  }
}

std::string 
ClientSocket::send(const std::string& msg)
{
  if (_sock == -1)
    throw std::string("ClientSocket::send(): socket already closed");
  
  while (true) {
    int ret = ::send(_sock, msg.c_str(), msg.size(), 0);
    if (ret == -1) {
      if (errno == EINTR)
	continue;
      throw std::string("ClientSocket::recv(): socket error");
    }
    
    return msg.substr(ret, msg.npos);
  }
}
