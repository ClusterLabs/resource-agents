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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>


using namespace ClusterMonitoring;
using namespace std;


static counting_auto_ptr<Logger> logger(new Logger());


Logger::Logger() :
  _fd(-1)
{}

Logger::Logger(const std::string& filepath, 
	       const std::string& domain, 
	       LogLevel level) :
  _domain(domain),
  _level(level)
{
  _fd = open(filepath.c_str(), 
	     O_CREAT|O_WRONLY|O_APPEND, 
	     S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
}

Logger::Logger(int fd, const std::string& domain, LogLevel level) :
  _fd(fd),
  _level(level)
{
  try {
    _domain = domain;
  } catch ( ... ) {
    close_fd();
    throw;
  }
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
}

void 
Logger::log(const std::string& msg, LogLevel level)
{
  if (_fd > 0 && _level & level) {
    std::string m;
    if (_fd > 2)
      m += _domain;
    m += ": " + msg + '\n';
    int e;
    do {
      e = write(_fd, m.c_str(), m.size());
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
ClusterMonitoring::set_logger(counting_auto_ptr<Logger> l)
{
  logger = l;
}
