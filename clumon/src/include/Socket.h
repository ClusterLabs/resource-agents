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


#ifndef Socket_h
#define Socket_h

#include "counting_auto_ptr.h"

#include <string>
#include <vector>


// NOT THREAD SAFE
// provide external locking


namespace ClusterMonitoring 
{


std::vector<std::string> name2IP(const std::string& hostname);


class Socket
{
 public:
  Socket(const Socket&);
  Socket& operator= (const Socket&);
  virtual ~Socket();
  
  virtual bool operator== (const Socket&);
  virtual bool server() = 0;
  
  int get_sock();
  bool valid() { return _sock != -1; }
  
 protected:
  Socket(int sock);  // takes ownership of sock
  int _sock;
  void close();
  counting_auto_ptr<int> _counter;
  
 private:
  void decrease_counter();
};  // class Socket


class ServerSocket;

class ClientSocket : public Socket
{
 public:
  ClientSocket();
  ClientSocket(const std::string& sock_path);  // UNIX socket
  ClientSocket(const std::string& hostname, unsigned short port);  // TCP socket
  ClientSocket(const ClientSocket&);
  ClientSocket& operator= (const ClientSocket&);
  virtual ~ClientSocket();
  
  std::string recv();
  std::string send(const std::string& msg);  // return what is left to send
  
  virtual bool server() { return false; }
  
  virtual bool connected_to(const std::string& hostname);
  
 protected:
  unsigned int _addr;
  
  ClientSocket(int sock, unsigned int addr=0);  // takes ownership of sock
  friend class ServerSocket;
};  // ClientSocket


class ServerSocket : public Socket
{
 public:
  ServerSocket(const std::string& sock_path); // UNIX socket
  ServerSocket(unsigned short port); // TCP socket
  ServerSocket(const ServerSocket&);
  ServerSocket& operator= (const ServerSocket&);
  virtual ~ServerSocket();
  
  ClientSocket accept();
  
  virtual bool server() { return true; }
  
 private:
  bool _unix_sock;
  std::string _sock_path;
  
};  // ServerSocket


};  // namespace ClusterMonitoring 


#endif  // Socket_h
