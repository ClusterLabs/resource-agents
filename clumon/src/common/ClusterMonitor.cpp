#include <sys/types.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>

#include <iostream>
#include <map>

#include "ClusterMonitor.h"
#include "MutexLocker.h"


using namespace std;
using namespace ClusterMonitoring;


class Command
{
public:
  Command(string command);
  virtual ~Command();
  
  string& operator[](string arg);
  
private:
  map<const string, string> values;
  string zeroStr;
};
Command::Command(string command) :
  zeroStr("")
{
  vector<string> pars;
  
  // split
  unsigned int pos = 0;
  while((pos = command.find(' ')) != command.npos) {
    pars.push_back(command.substr(0, pos));
    command = command.substr(pos+1, command.npos);
  }
  pars.push_back(command);
  
  // store value pairs into values
  for(unsigned int i=0; i<pars.size(); i++) {
    pos = pars[i].find("=");
    if(pos != pars[i].npos) {
      string key, value;
      key = pars[i].substr(0, pos);
      value = pars[i].substr(pos+1, pars[i].npos);
      values[key] = value;
    }
  }
}
Command::~Command()
{}
string&
Command::operator[](string arg)
{
  return values[arg];
}



ClusterMonitor::ClusterMonitor(string socket_path) : 
  socket_path(socket_path), 
  sock(-1)
{
  openSocket(1);
  
  // init mutex
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE_NP);
  pthread_mutex_init(&mutex, &attr);
  pthread_mutexattr_destroy(&attr);pthread_mutex_destroy(&mutex);
}

ClusterMonitor::~ClusterMonitor()
{
  closeSocket();
  pthread_mutex_destroy(&mutex);
}




void
ClusterMonitor::openSocket(int num)
{
  if(sock != -1)
    return;
  
  sock = socket(PF_UNIX, SOCK_STREAM, 0);
  if(sock == -1)
    return;
  
  struct sockaddr_un {
    sa_family_t  sun_family;
    char         sun_path[100];
  } addr;
  addr.sun_family = AF_UNIX;
  memcpy(addr.sun_path, socket_path.c_str(), socket_path.size()+1);
  
  if(connect(sock, (struct sockaddr*) &addr, sizeof(addr)))
    closeSocket();
}

void 
ClusterMonitor::closeSocket()
{
  if(sock != -1)
    close(sock);
  sock = -1;
}

bool 
ClusterMonitor::checkSocket()
{
  if(sock == -1)
    return false;
  
  while(true) {
    struct pollfd p;
    p.fd = sock;
    p.events = 0;
    p.revents = 0;
    int s = poll(&p, 1, 1);
    if(s < 0) {
      // poll error
      if(errno == EINTR)
	continue;
      else {
	closeSocket();
	return false;
      }
    } 
    // poll OK
    
    if(s > 0) {
      // socket error condition
      closeSocket();
      return false;
    }
    
    // socket OK
    return true;
    
  } // while
}

bool
ClusterMonitor::send(string msg)
{
  if(!checkSocket())
    openSocket(3);
  if(!checkSocket())
    return false;
  
  int wait = 2000;
  
  while(true) {
    struct pollfd p;
    p.fd = sock;
    p.events = POLLOUT;
    p.revents = 0;
    
    int s = poll(&p, 1, wait);
    if(s == 0)
      return false;
    else if(s < 0) {
      // poll error
      if(errno == EINTR)
	continue;
      else {
	closeSocket();
	return false;
      }
    } // (s < 0)
    // poll ok
    
    if(p.revents & (POLLERR|POLLHUP|POLLNVAL)) {
      // socket error
      closeSocket();
      return false;
    }
    
    // socket ready
    wait = 100;
    int sent = ::send(sock, msg.c_str(), msg.size(), 0);
    if(sent == -1) {
      closeSocket();
      return false;
    }
    else if(sent == (int) msg.size())
      return true;
    else 
      msg = msg.substr(sent, msg.npos);
  } // while
}

string
ClusterMonitor::receive(unsigned int wait)
{
  if(!checkSocket())
    return "";
  
  string data;
  while(true) {
    struct pollfd p;
    p.fd = sock;
    p.events = POLLIN;
    p.revents = 0;
    
    int s = poll(&p, 1, wait);
    if(s == 0)
      return data;
    else if(s < 0) {
      // poll error
      if(errno == EINTR)
	continue;
      else {
	closeSocket();
	return data;
      }
    } // (s < 0)
    // poll ok
    
    if(p.revents & (POLLERR|POLLHUP|POLLNVAL)) {
      // socket error
      closeSocket();
      return data;
    }
    
    // socket ready
    wait = 100;
    char buffer[1024];
    int received = recv(sock, buffer, sizeof(buffer), 0);
    if(received < 1) {
      closeSocket();
      return data;
    }
    else {
      if(buffer[received - 1] == 0)
	received -= 1;
      data.append(buffer, received);
    }
  } // while
}









counting_auto_ptr<Cluster>
ClusterMonitor::getCluster()
{
  MutexLocker locker(&mutex);
  
  counting_auto_ptr<Cluster> cluster;
  
  // ** empty buffers **
  receive(1);
  
  // ** send GET request **
  if(!send("GET"))
    return cluster;
  
  // ** get data **
  try {
    string command;
    vector<string> commands;
    
    bool done = false;
    while(!done) {
      string data = receive(3000);
      if(data.size() == 0)
	done = true;
      else
	command += data;
      
      // ** store data **
      unsigned int pos = 0;
      while((pos = command.find('\n')) != command.npos) {
	commands.push_back(command.substr(0, pos));
	command = command.substr(pos+1, command.npos);
	if(commands.back() == "")
	  break;
      }
      
      // ** if end of commands, replace cluster **
      if(commands.size() != 0)
	if(commands.back() == "") {
	  commands.pop_back();
	  cluster = buildCluster(commands);
	  done = true;
	}
      
    } // while
    
  } catch ( ... ) { }
  
  return cluster;
}


counting_auto_ptr<Cluster> 
ClusterMonitor::buildCluster(vector<string>& commands)
{
  counting_auto_ptr<Cluster> cluster;
  
  for(unsigned int i=0; i<commands.size(); i++) {
    
    Command comm(commands[i]);
    
    if(comm["object"] == "cluster") {
      string& name = comm["name"];
      unsigned int quorum = atoi(comm["minQuorum"].c_str());
      cluster = counting_auto_ptr<Cluster>(new Cluster(name, quorum));
    }
    else if(comm["object"] == "node") {
      string& name = comm["name"];
      unsigned int votes = atoi(comm["votes"].c_str());
      bool online = comm["online"] == "true";
      bool clustered = comm["clustered"] == "true";
      if(cluster.get())
	cluster->addNode(name, votes, online, clustered);
    }
    else if(comm["object"] == "service") {
      string& name = comm["name"];
      string nodeName = comm["nodename"];
      bool failed = comm["failed"] == "true";
      bool autostart = comm["autostart"] == "true";
      if(cluster.get())
	cluster->addService(name, nodeName, failed, autostart);
    }
  }
  
  return cluster;
}


void ClusterMonitor::print()
{
  counting_auto_ptr<Cluster> cluster = getCluster();
  
  if(cluster.get()) {
    
    cout << cluster->name << endl;
    
    cout << cluster->getNodes().size() << endl;
    
    cout << cluster->getServices().size() << endl;
    
  }
}




int 
main(int argc, char** argv)
{
  ClusterMonitor monitor("/tmp/cluster_monitor_sock");
  
  monitor.print();
  
  monitor.print();
  
  monitor.print();
  
}
