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


#include <sys/poll.h>
#include <errno.h>
typedef struct pollfd poll_fd;

#include "clumond_globals.h"
#include "Monitor.h"
#include "Socket.h"
#include "array_auto_ptr.h"

#include <map>
#include <iostream>


using namespace ClusterMonitoring;
using namespace std;


class ClientInfo
{
public:
  ClientInfo() {}
  ClientInfo(ClientSocket& sock, string str="") : 
    sock(sock), str(str) {}
  
  ClientSocket sock;
  string str;
};

static void 
serve_clients(Monitor& monitor, ServerSocket& server);


int 
main(int argc, char** argv)
{
  try {
    Monitor monitor(COMMUNICATION_PORT);
    monitor.start();
    ServerSocket server(MONITORING_CLIENT_SOCKET);
    
    serve_clients(monitor, server);
  } catch (string e) {
    cout << e << endl;
  } catch ( ... ) {
    cout << "unhandled exception" << endl;
  }
}

void 
serve_clients(Monitor& monitor, ServerSocket& server)
{
  map<int, ClientInfo> clients;
  
  while (true) {
    unsigned int socks_num = clients.size() + 1;
    
    // prepare poll structs
    array_auto_ptr<poll_fd> poll_data(new poll_fd[socks_num]);
    poll_data[0].fd = server.get_sock();
    poll_data[0].events = POLLIN;
    poll_data[0].revents = 0;
    map<int, ClientInfo>::iterator iter = clients.begin();
    for (unsigned int i=1; i<socks_num; i++) {
      poll_data[i].fd = iter->first;
      poll_data[i].events = POLLIN;
      if ( ! iter->second.str.empty())
	poll_data[i].events = POLLOUT;
      poll_data[i].revents = 0;
      iter++;
    }
    
    // wait for events
    int ret = poll(poll_data.get(), socks_num, 500);
    if (ret == 0)
      continue;
    else if (ret == -1) {
      if (errno == EINTR)
	continue;
      else
	throw string("serve_clients(): poll() error");
    }
    
    // process events
    for (unsigned int i=0; i<socks_num; i++) {
      poll_fd& poll_info = poll_data[i];
      
      // server socket
      if (poll_info.fd == server.get_sock()) {
	if (poll_info.revents & POLLIN) {
	  try {
	    ClientSocket sock = server.accept();
	    clients[sock.get_sock()] = ClientInfo(sock);
	  } catch ( ... ) {}
	}
	if (poll_info.revents & (POLLERR | POLLHUP | POLLNVAL))
	  throw string("serve_clients(): server socket error????");
      }
      // client socket
      else {
	if (poll_info.revents & POLLIN) {
	  ClientInfo& info = clients[poll_info.fd];
	  try {
	    info.str = monitor.request(info.sock.recv());
	  } catch ( ... ) {
	    clients.erase(poll_info.fd);
	    continue;
	  }
	}
	if (poll_info.revents & (POLLERR | POLLHUP | POLLNVAL)) {
	  clients.erase(poll_info.fd);
	  continue;
	}
	if (poll_info.revents & POLLOUT) {
	  ClientInfo& info = clients[poll_info.fd];
	  try {
	    info.str = info.sock.send(info.str);
	  } catch ( ... ) {
	    clients.erase(poll_info.fd);
	    continue;
	  }
	}
      }  // client socket
    }  // process events
  } // while
}
