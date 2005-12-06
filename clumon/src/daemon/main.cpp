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


#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <pthread.h>

#include <sys/poll.h>
#include <errno.h>
typedef struct pollfd poll_fd;

extern "C" {
#include "signals.h"
void daemon_init(char *prog);
}

#include "clumond_globals.h"
#include "Monitor.h"
#include "Socket.h"
#include "array_auto_ptr.h"
#include "Logger.h"

#include <map>
#include <iostream>


using namespace ClusterMonitoring;
using namespace std;


static bool shutdown_pending = false;
static void shutdown(int);
static void segfault(int);
static void sigchild(int);
static void serve_clients(Monitor& monitor, ServerSocket& server);

class ClientInfo
{
public:
  ClientInfo() {}
  ClientInfo(ClientSocket& sock, string str="") : 
    sock(sock), str(str) {}
  
  ClientSocket sock;
  string str;
};



int 
main(int argc, char** argv)
{
  bool debug=false, foreground=false;
  int v_level = -1;
  
  int rv;
  while ((rv = getopt(argc, argv, "fdv:")) != EOF)
    switch (rv) {
    case 'd':
      debug = true;
      break;
    case 'f':
      foreground = true;
      break;
    case 'v':
      sscanf(optarg, "%d", &v_level);
      break;
    default:
      break;
    }
  
  if (v_level < 0) {
    cout << "Setting verbosity level to LogBasic" << endl;
    v_level = LogBasic;
  }
  if (foreground)
    set_logger(counting_auto_ptr<Logger>(new Logger(1, "clumond", LogLevel(v_level))));
  else
    set_logger(counting_auto_ptr<Logger>(new Logger(LOG_FILE, "clumond", LogLevel(v_level))));
  
  log("started");
  try {
    ServerSocket server(MONITORING_CLIENT_SOCKET);
    Monitor monitor(COMMUNICATION_PORT);
    
    if (!foreground && (geteuid() == 0))
      daemon_init(argv[0]);
    setup_signal(SIGINT, shutdown);
    setup_signal(SIGTERM, shutdown);
    setup_signal(SIGCHLD, sigchild);
    setup_signal(SIGPIPE, SIG_IGN);
    if (debug)
      setup_signal(SIGSEGV, segfault);
    else
      unblock_signal(SIGSEGV);
    
    serve_clients(monitor, server);
  } catch (string e) {
    log("unhandled exception in main(): " + e);
    log("died");
    return 1;
  } catch ( ... ) {
    log("unhandled unknown exception in main()");
    log("died");
    return 1;
  }
  
  unlink("/var/run/clumond.pid");
  log("exited");
  return 0;
}

void 
serve_clients(Monitor& monitor, ServerSocket& server)
{
  map<int, ClientInfo> clients;
  
  log("Starting monitor", LogMonitor);
  monitor.start();
  
  while (!shutdown_pending) {
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
	poll_data[i].events |= POLLOUT;
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

void 
shutdown(int)
{
  log("exit requested", LogExit);
  shutdown_pending = true;
}

void
segfault(int)
{
  char msg[128];
  snprintf(msg, sizeof(msg)-1, "PID %d Thread %d: SIGSEGV, waiting forensics", 
	   getpid(), (int) pthread_self());
  log(msg);
  while(1)
    sleep(60);
}

void 
sigchild(int)
{
  // do nothing
}
