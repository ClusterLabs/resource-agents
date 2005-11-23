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


#include "Cluster.h"


using namespace ClusterMonitoring;
using namespace std;


Service::Service(const string& name,
		 const string& clustername,
		 const Node& node,
		 bool failed,
		 bool autostart) : 
  _name(name),
  _clustername(clustername),
  _nodename(node.name()),
  _autostart(autostart),
  _failed(failed)
{}

Service::~Service(void)
{}


string
Service::name() const
{
  return _name;
}

string
Service::clustername() const
{
  return _clustername;
}

bool 
Service::running() const
{
  return _nodename.size();
}

string
Service::nodename() const
{
  return _nodename;
}

bool 
Service::failed() const
{
  return _failed;
}

bool 
Service::autostart() const
{
  return _autostart;
}
