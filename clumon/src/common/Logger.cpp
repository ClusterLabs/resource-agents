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


#include "Logger.h"
#include "Time.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>


using namespace ClusterMonitoring;
using namespace std;


static counting_auto_ptr<Logger> logger(new Logger());


Logger::Logger() :
  _fd(-1),
  _domain_c(NULL)
{}

Logger::Logger(const std::string& filepath, 
	       const std::string& domain, 
	       LogLevel level) :
  _level(level)
{
  const char* c_str = domain.c_str();
  const char* path_c = filepath.c_str();
  
  _domain_c = (char*) malloc(domain.size()+1);
  if (_domain_c == NULL)
    throw string("Logger::Logger(): malloc() failed");
  strcpy(_domain_c, c_str);
  
  _fd = open(path_c, 
	     O_CREAT|O_WRONLY|O_APPEND, 
	     S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
  if (_fd == -1) {
    free(_domain_c);
    throw string("Logger::Logger(): open() failed");
  }
}

Logger::Logger(int fd, const std::string& domain, LogLevel level) :
  _fd(fd),
  _level(level)
{
  const char* c_str = domain.c_str();
  
  _domain_c = (char*) malloc(domain.size()+1);
  if (_domain_c == NULL) {
    close_fd();
    throw string("Logger::Logger(): malloc() failed");
  }
  strcpy(_domain_c, c_str);
}

void
Logger::close_fd()
{
  if (_fd > -1)
    fsync(_fd);
  if (_fd > 2) {
    int e;
    do {
      e = close(_fd);
    } while (e == -1 && (errno == EINTR));
    _fd = -1;
  }
}

Logger::~Logger()
{
  close_fd();
  free(_domain_c);
}

void 
Logger::log(const std::string& msg, LogLevel level)
{
  log_sigsafe(msg.c_str(), level);
}

void 
Logger::log_sigsafe(const char* msg, LogLevel level)
{
  if (_fd > 0 && _level & level) {
    char time[64];
    time_t t = time_sec();
    ctime_r(&t, time);
    time[sizeof(time)-1] = 0;
    for (int i=0; time[i]; i++)
      if (time[i] == '\n') {
	time[i] = 0;
	break;
      }
    
    char m[2048];
    if (_fd > 2 && (_domain_c != NULL))
      snprintf(m, sizeof(m), "%s %s: %s\n", time, _domain_c, msg);
    else
      snprintf(m, sizeof(m), "%s: %s\n", time, msg);
    m[sizeof(m)-1] = 0;
    
    int l, e;
    for (l=0; m[l]; l++) ;
    do {
      e = write(_fd, m, l);
    } while (e == -1 && errno == EINTR);
  }
}
  


// ### helper functions ###

std::string 
ClusterMonitoring::operator+ (const std::string& s, int i)
{
  char buff[128];
  snprintf(buff, sizeof(buff), "%d", i);
  return s + buff;
}

std::string 
ClusterMonitoring::operator+ (int i, const std::string& s)
{
  char buff[128];
  snprintf(buff, sizeof(buff), "%d", i);
  return string(buff) + s;
}

void 
ClusterMonitoring::log(const std::string& msg, LogLevel level)
{
  logger->log(msg, level);
}

void 
ClusterMonitoring::log_sigsafe(const char* msg, LogLevel level)
{
  logger->log_sigsafe(msg, level);
}

void 
ClusterMonitoring::set_logger(counting_auto_ptr<Logger> l)
{
  if (l.get() == NULL)
    l = counting_auto_ptr<Logger>(new Logger());
  logger = l;
}
