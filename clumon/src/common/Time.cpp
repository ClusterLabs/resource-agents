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


#include "Time.h"

#include <sys/time.h>


unsigned int 
ClusterMonitoring::time_sec()
{
  struct timeval t;
  struct timezone z;
  gettimeofday(&t, &z);
  return t.tv_sec;
}

unsigned int 
ClusterMonitoring::time_mil()
{
  struct timeval t;
  struct timezone z;
  gettimeofday(&t, &z);
  return t.tv_sec*1000 + t.tv_usec/1000;
}

std::string 
ClusterMonitoring::time_formated()
{
  char time[64];
  time_t t = time_sec();
  ctime_r(&t, time);
  std::string m(time);
  return m.substr(0, m.size()-1);
}
