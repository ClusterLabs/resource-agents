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


#ifndef Logger_h
#define Logger_h

#include "counting_auto_ptr.h"
#include <string>


namespace ClusterMonitoring {


enum LogLevel {LogNone         = 0, 
	       LogBasic        = 1,
	       LogMonitor      = 2,
	       LogSocket       = 4,
	       LogCommunicator = 8,
	       LogTransfer     = 16,
	       LogExit         = 32,
	       LogThread       = 64,
	       LogTime         = 128,
	       LogExecute      = 256,
	       LogAll          = ~0 };
 

class Logger
{
 public:
  Logger();
  Logger(const std::string& filepath, const std::string& domain, LogLevel level);
  Logger(int fd, const std::string& domain, LogLevel level);
  virtual ~Logger();
  
  void log(const std::string& msg, LogLevel level=LogBasic);
  void operator<< (const std::string& msg) { log(msg); }
  
 private:
  int _fd;
  std::string _domain;
  int _level;
  
  void close_fd();
  
  Logger(const Logger&);
  Logger& operator= (const Logger&);
};  // class Logger
 
 
// helper functions
std::string operator+ (const std::string&, int);
std::string operator+ (int, const std::string&);
void log(const std::string& msg, LogLevel level=LogBasic);
void set_logger(counting_auto_ptr<Logger>);
 
 
};  // namespace ClusterMonitoring


#endif  // Logger_h
