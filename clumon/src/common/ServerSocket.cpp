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
#include "Logger.h"

#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <string.h>


using namespace ClusterMonitoring;


ServerSocket::ServerSocket(const std::string& sock_path) :
  Socket(-1), 
  _unix_sock(true), 
  _sock_path(sock_path)
{
  _sock = socket(PF_UNIX, SOCK_STREAM, 0);
  if (_sock == -1) {
    std::string m = std::string("ServerSocket(sock_path=") + sock_path + "): socket() failed, errno=" + errno;
    throw m;
  }
  
  struct sockaddr_un {
    sa_family_t  sun_family;
    char         sun_path[100];
  } addr;
  addr.sun_family = AF_UNIX;
  memcpy(addr.sun_path, sock_path.c_str(), sock_path.size()+1);
  
  unlink(_sock_path.c_str());
  if (bind(_sock, (struct sockaddr*) &addr, sizeof(addr))) {
    std::string m = std::string("ServerSocket(sock_path=") + sock_path + "): bind() failed, errno=" + errno;
    throw m;
  }
  
  if (listen(_sock, 5)) {
    std::string m = std::string("ServerSocket(sock_path=") + sock_path + "): listen() failed, errno=" + errno;
    throw m;
  }
  
  std::string msg = std::string("created unix server socket, ") + _sock + ", " + sock_path;
  log(msg, LogSocket);
}

ServerSocket::ServerSocket(unsigned short port) :
  Socket(-1), 
  _unix_sock(false), 
  _sock_path("")
{
  _sock = socket(PF_INET, SOCK_STREAM, 0);
  if (_sock == -1) {
    std::string m = std::string("ServerSocket(port=") + port + "): socket() failed, errno=" + errno;
    throw m;
  }
  
  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = INADDR_ANY;
  if (bind(_sock, (struct sockaddr*) &addr, sizeof(addr))) {
    std::string m = std::string("ServerSocket(port=") + port + "): bind() failed, errno=" + errno;
    throw m;
  }
  
  if (listen(_sock, 5)) {
    std::string m = std::string("ServerSocket(port=") + port + "): listen() failed, errno=" + errno;
    throw m;
  }
  
  std::string msg = std::string("created tcp server socket, ") + _sock + ", port " + port;
  log(msg, LogSocket);
}

ServerSocket::ServerSocket(const ServerSocket& s) :
  Socket(s), 
  _unix_sock(s._unix_sock), 
  _sock_path(s._sock_path)
{}

ServerSocket& 
ServerSocket::operator= (const ServerSocket& s)
{
  if (&s != this) {
    this->Socket::operator= (s);
    _unix_sock = s._unix_sock;
    _sock_path = s._sock_path;
  }
  return *this;
}

ServerSocket::~ServerSocket()
{
  if (_unix_sock && (*_counter == 1))
    unlink(_sock_path.c_str());
}

ClientSocket 
ServerSocket::accept()
{
  while (true) {
    struct sockaddr_in addr_in;
    socklen_t size = sizeof(addr_in);
    
    int ret = ::accept(_sock, (struct sockaddr*) &addr_in, &size);
    if (ret == -1) {
      if (errno == EINTR)
	continue;
      throw std::string("ServerSocket(string): accept() failed");
    }
    log("ServerSocket: accepted connection", LogSocket);
    return ClientSocket(ret, addr_in.sin_addr.s_addr);
  }
}
